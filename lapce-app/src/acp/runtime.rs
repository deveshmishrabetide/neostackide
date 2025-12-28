//! Agent runtime - runs tokio in a background thread
//!
//! This module handles all async ACP operations in a dedicated thread,
//! communicating with the UI via channels.

use std::path::PathBuf;
use std::process::Stdio;
use std::sync::Arc;

use crossbeam_channel::Sender;
use parking_lot::RwLock;
use tokio::process::Command;
use tokio::runtime::Runtime;
use tokio_util::compat::{TokioAsyncReadCompatExt, TokioAsyncWriteCompatExt};

use super::client::LapceAcpClient;
use super::rpc::{AgentRpc, AgentRpcHandler, AgentResponse};
use super::types::*;
use super::{
    acp, ClientSideConnection, ProtocolVersion, Implementation,
    ClientCapabilities, FileSystemCapability, CancelNotification,
};

/// State of an active agent connection
struct ActiveConnection {
    conn: ClientSideConnection,
    session_id: String,
    #[allow(dead_code)]
    child: tokio::process::Child,
}

/// The agent runtime that processes RPC commands
pub struct AgentRuntime {
    /// RPC handler for receiving commands
    rpc: AgentRpcHandler,
    /// Channel to send notifications to UI
    notification_tx: Sender<AgentNotification>,
    /// Currently active connection
    connection: Arc<RwLock<Option<ActiveConnection>>>,
    /// The ACP client (handles callbacks from agent)
    client: Arc<LapceAcpClient>,
}

impl AgentRuntime {
    /// Create a new agent runtime
    pub fn new(
        rpc: AgentRpcHandler,
        notification_tx: Sender<AgentNotification>,
        workspace_path: PathBuf,
    ) -> Self {
        // Create the client with a sender for events
        let client = Arc::new(LapceAcpClient::new(
            workspace_path,
            notification_tx.clone(),
        ));

        Self {
            rpc,
            notification_tx,
            connection: Arc::new(RwLock::new(None)),
            client,
        }
    }

    /// Run the main loop (call this from a dedicated thread)
    pub fn run(self) {
        // Create a tokio runtime for this thread
        let rt = Runtime::new().expect("Failed to create tokio runtime");

        // Process RPC commands
        for msg in self.rpc.rx().iter() {
            match msg {
                AgentRpc::Connect { config, workspace_path } => {
                    let connection = self.connection.clone();
                    let notification_tx = self.notification_tx.clone();
                    let client = self.client.clone();

                    rt.block_on(async {
                        match Self::do_connect(config, workspace_path, client).await {
                            Ok((conn, session_id, child)) => {
                                *connection.write() = Some(ActiveConnection {
                                    conn,
                                    session_id: session_id.clone(),
                                    child,
                                });
                                let _ = notification_tx.send(AgentNotification::Connected {
                                    session_id,
                                });
                            }
                            Err(e) => {
                                let _ = notification_tx.send(AgentNotification::Error {
                                    message: e.to_string(),
                                });
                            }
                        }
                    });
                }

                AgentRpc::Prompt { id, session_id: _, prompt } => {
                    let connection = self.connection.clone();
                    let rpc = self.rpc.clone();
                    let notification_tx = self.notification_tx.clone();

                    rt.block_on(async {
                        let result = Self::do_prompt(&connection, prompt).await;
                        match result {
                            Ok(stop_reason) => {
                                rpc.handle_response(id, Ok(AgentResponse::PromptCompleted {
                                    stop_reason,
                                }));
                            }
                            Err(e) => {
                                let _ = notification_tx.send(AgentNotification::Error {
                                    message: e.clone(),
                                });
                                rpc.handle_response(id, Err(e));
                            }
                        }
                    });
                }

                AgentRpc::Cancel { session_id: _ } => {
                    let connection = self.connection.clone();
                    let notification_tx = self.notification_tx.clone();

                    rt.block_on(async {
                        if let Err(e) = Self::do_cancel(&connection).await {
                            let _ = notification_tx.send(AgentNotification::Error {
                                message: e,
                            });
                        }
                    });
                }

                AgentRpc::PermissionResponse { request_id, response } => {
                    self.client.respond_to_permission(&request_id, response);
                }

                AgentRpc::Disconnect => {
                    *self.connection.write() = None;
                    let _ = self.notification_tx.send(AgentNotification::Disconnected);
                }

                AgentRpc::Shutdown => {
                    *self.connection.write() = None;
                    break;
                }
            }
        }
    }

    async fn do_connect(
        config: AgentConfig,
        workspace_path: PathBuf,
        client: Arc<LapceAcpClient>,
    ) -> anyhow::Result<(ClientSideConnection, String, tokio::process::Child)> {
        // Spawn the agent process
        let mut cmd = Command::new(&config.command);
        cmd.args(&config.args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);

        if let Some(cwd) = &config.cwd {
            cmd.current_dir(cwd);
        } else {
            cmd.current_dir(&workspace_path);
        }

        for (key, value) in &config.env {
            cmd.env(key, value);
        }

        let mut child = cmd.spawn()?;

        let stdin = child.stdin.take().ok_or_else(|| anyhow::anyhow!("No stdin"))?;
        let stdout = child.stdout.take().ok_or_else(|| anyhow::anyhow!("No stdout"))?;

        // Create the ACP connection - use a reference to the client
        let client_ref = (*client).clone();
        let (conn, io_task) = ClientSideConnection::new(
            client_ref,
            stdin.compat_write(),
            stdout.compat(),
            |fut| {
                tokio::spawn(fut);
            },
        );

        // Spawn the I/O handler
        tokio::spawn(async move {
            if let Err(e) = io_task.await {
                tracing::error!("ACP I/O error: {}", e);
            }
        });

        // Initialize the connection
        let init_request = acp::InitializeRequest::new(ProtocolVersion::V1)
            .client_capabilities(
                ClientCapabilities::new()
                    .fs(
                        FileSystemCapability::new()
                            .read_text_file(true)
                            .write_text_file(true)
                    )
                    .terminal(true)
            )
            .client_info(
                Implementation::new("lapce", env!("CARGO_PKG_VERSION"))
                    .title("Lapce Editor".to_string())
            );

        let _init_response = conn.initialize(init_request).await?;

        // Create a new session
        let session_request = acp::NewSessionRequest::new(workspace_path);
        let session_response = conn.new_session(session_request).await?;
        let session_id = session_response.session_id.0.to_string();

        Ok((conn, session_id, child))
    }

    async fn do_prompt(
        connection: &Arc<RwLock<Option<ActiveConnection>>>,
        prompt: String,
    ) -> Result<Option<String>, String> {
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Not connected")?;

        let prompt_request = acp::PromptRequest::new(
            acp::SessionId::new(&active.session_id),
            vec![prompt.into()],
        );

        // Drop the guard before await
        let session_id = active.session_id.clone();
        drop(conn_guard);

        // Re-acquire for the call
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Connection lost")?;

        let response = active.conn
            .prompt(prompt_request)
            .await
            .map_err(|e| e.to_string())?;

        Ok(response.stop_reason.map(|r| format!("{:?}", r)))
    }

    async fn do_cancel(
        connection: &Arc<RwLock<Option<ActiveConnection>>>,
    ) -> Result<(), String> {
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Not connected")?;

        let cancel = CancelNotification::new(
            acp::SessionId::new(&active.session_id)
        );

        active.conn
            .cancel(cancel)
            .await
            .map_err(|e| e.to_string())?;

        Ok(())
    }
}

/// Start the agent runtime in a background thread
pub fn start_agent_runtime(
    rpc: AgentRpcHandler,
    notification_tx: Sender<AgentNotification>,
    workspace_path: PathBuf,
) {
    std::thread::Builder::new()
        .name("AgentRuntime".to_string())
        .spawn(move || {
            let runtime = AgentRuntime::new(rpc, notification_tx, workspace_path);
            runtime.run();
        })
        .expect("Failed to spawn agent runtime thread");
}
