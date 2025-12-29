//! Agent runtime - runs tokio in a background thread
//!
//! This module handles all async ACP operations in a dedicated thread,
//! communicating with the UI via channels.

use std::path::PathBuf;
use std::process::Stdio;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

use crossbeam_channel::Sender;
use parking_lot::RwLock;
use tokio::process::Command;
use tokio::runtime::Runtime;
use tokio::task::LocalSet;
use tokio_util::compat::{TokioAsyncReadCompatExt, TokioAsyncWriteCompatExt};

use super::client::LapceAcpClient;
use super::neostack::{NeoStackAgent, NeoStackAgentImpl};
use super::rpc::{AgentRpc, AgentRpcHandler, AgentResponse};
use super::types::*;
use super::{
    acp, Agent, AgentSideConnection, ClientSideConnection, ProtocolVersion, Implementation,
    ClientCapabilities, FileSystemCapability, CancelNotification,
    McpServer, McpServerHttp,
};
use crate::mcp::get_mcp_port;

/// Result of a successful connection with session and model info
struct ConnectionResult {
    conn: ClientSideConnection,
    session_id: String,
    /// Child process for subprocess-based agents (None for embedded agents)
    child: Option<tokio::process::Child>,
    /// Available models from the agent
    models: Vec<ModelInfo>,
    /// Currently selected model ID
    current_model_id: Option<String>,
}

/// State of an active agent connection
struct ActiveConnection {
    conn: ClientSideConnection,
    session_id: String,
    /// Child process for subprocess-based agents (None for embedded agents)
    #[allow(dead_code)]
    child: Option<tokio::process::Child>,
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
    ///
    /// This uses a fully async architecture where:
    /// - RPC messages are polled non-blocking
    /// - Long-running operations (like prompts) are spawned as async tasks
    /// - The LocalSet is continuously driven to allow tasks to make progress
    /// - This enables PermissionResponse messages to be processed while a prompt is running
    pub fn run(self) {
        // Create a tokio runtime for this thread
        let rt = Runtime::new().expect("Failed to create tokio runtime");

        // Run everything in the LocalSet
        rt.block_on(async {
            let local = LocalSet::new();
            local.run_until(self.run_async()).await;
        });
    }

    /// The main async event loop
    async fn run_async(self) {
        tracing::info!("[AgentRuntime] Starting async event loop");

        loop {
            // Try to receive an RPC message with a short timeout
            // This allows us to also drive spawned tasks
            match self.rpc.rx().recv_timeout(Duration::from_millis(10)) {
                Ok(msg) => {
                    tracing::debug!("[AgentRuntime] Received RPC message");
                    let should_break = self.handle_rpc_message(msg).await;
                    if should_break {
                        tracing::info!("[AgentRuntime] Shutdown requested, breaking loop");
                        break;
                    }
                }
                Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
                    // No message, yield to let spawned tasks make progress
                    tokio::task::yield_now().await;
                }
                Err(crossbeam_channel::RecvTimeoutError::Disconnected) => {
                    tracing::info!("[AgentRuntime] RPC channel disconnected, shutting down");
                    break;
                }
            }
        }

        tracing::info!("[AgentRuntime] Event loop ended");
    }

    /// Handle a single RPC message. Returns true if we should break the loop.
    async fn handle_rpc_message(&self, msg: AgentRpc) -> bool {
        match msg {
            AgentRpc::Connect { config, workspace_path } => {
                tracing::info!("[AgentRuntime] Received Connect request for {}", config.command);
                let connection = self.connection.clone();
                let notification_tx = self.notification_tx.clone();
                let client = self.client.clone();

                match Self::do_connect(config, workspace_path, client, None).await {
                    Ok(result) => {
                        tracing::info!("[AgentRuntime] Connection successful, session_id: {}", result.session_id);
                        let session_id = result.session_id.clone();
                        let models = result.models.clone();
                        let current_model_id = result.current_model_id.clone();

                        *connection.write() = Some(ActiveConnection {
                            conn: result.conn,
                            session_id: session_id.clone(),
                            child: result.child,
                        });

                        // Send connected notification
                        let _ = notification_tx.send(AgentNotification::Connected {
                            session_id,
                        });

                        // Send available models notification
                        if !models.is_empty() {
                            let _ = notification_tx.send(AgentNotification::ModelsAvailable {
                                models,
                                current_model_id,
                            });
                        }
                    }
                    Err(e) => {
                        tracing::error!("[AgentRuntime] Connection failed: {}", e);
                        let _ = notification_tx.send(AgentNotification::Error {
                            message: e.to_string(),
                        });
                    }
                }
            }

            AgentRpc::ResumeSession { config, workspace_path, agent_session_id } => {
                tracing::info!("[AgentRuntime] Received ResumeSession request for {} with session {}", config.command, agent_session_id);
                let connection = self.connection.clone();
                let notification_tx = self.notification_tx.clone();
                let client = self.client.clone();

                match Self::do_connect(config, workspace_path, client, Some(agent_session_id)).await {
                    Ok(result) => {
                        tracing::info!("[AgentRuntime] Session resumed, session_id: {}", result.session_id);
                        let session_id = result.session_id.clone();
                        let models = result.models.clone();
                        let current_model_id = result.current_model_id.clone();

                        *connection.write() = Some(ActiveConnection {
                            conn: result.conn,
                            session_id: session_id.clone(),
                            child: result.child,
                        });

                        // Send connected notification
                        let _ = notification_tx.send(AgentNotification::Connected {
                            session_id,
                        });

                        // Send available models notification
                        if !models.is_empty() {
                            let _ = notification_tx.send(AgentNotification::ModelsAvailable {
                                models,
                                current_model_id,
                            });
                        }
                    }
                    Err(e) => {
                        tracing::error!("[AgentRuntime] Session resume failed: {}", e);
                        let _ = notification_tx.send(AgentNotification::Error {
                            message: e.to_string(),
                        });
                    }
                }
            }

            AgentRpc::Prompt { id, session_id: _, prompt } => {
                tracing::info!("[AgentRuntime] Prompt received, spawning async task");
                let connection = self.connection.clone();
                let rpc = self.rpc.clone();
                let notification_tx = self.notification_tx.clone();

                // Spawn as async task so we can continue processing RPC messages
                // (especially PermissionResponse) while the prompt is running
                tokio::task::spawn_local(async move {
                    tracing::info!("[AgentRuntime] Prompt async task started");
                    let result = Self::do_prompt(&connection, prompt).await;
                    tracing::info!("[AgentRuntime] Prompt async task: do_prompt returned: {:?}", result.is_ok());
                    match result {
                        Ok(stop_reason) => {
                            // Send turn completed notification to flush streaming buffer
                            let _ = notification_tx.send(AgentNotification::TurnCompleted {
                                stop_reason: stop_reason.clone(),
                            });
                            rpc.handle_response(id, Ok(AgentResponse::PromptCompleted {
                                stop_reason,
                            }));
                        }
                        Err(e) => {
                            // Also send turn completed on error to flush any partial content
                            let _ = notification_tx.send(AgentNotification::TurnCompleted {
                                stop_reason: None,
                            });
                            let _ = notification_tx.send(AgentNotification::Error {
                                message: e.clone(),
                            });
                            rpc.handle_response(id, Err(e));
                        }
                    }
                    tracing::info!("[AgentRuntime] Prompt async task completed");
                });
            }

            AgentRpc::Cancel { session_id: _ } => {
                tracing::info!("[AgentRuntime] Cancel received");
                let connection = self.connection.clone();
                let notification_tx = self.notification_tx.clone();

                if let Err(e) = Self::do_cancel(&connection).await {
                    let _ = notification_tx.send(AgentNotification::Error {
                        message: e,
                    });
                }
            }

            AgentRpc::SetModel { session_id, model_id } => {
                tracing::info!("[AgentRuntime] SetModel received: {}", model_id);
                let connection = self.connection.clone();
                let notification_tx = self.notification_tx.clone();

                match Self::do_set_model(&connection, session_id, model_id.clone()).await {
                    Ok(()) => {
                        tracing::info!("[AgentRuntime] Model set to: {}", model_id);
                    }
                    Err(e) => {
                        let _ = notification_tx.send(AgentNotification::Error {
                            message: e,
                        });
                    }
                }
            }

            AgentRpc::PermissionResponse { request_id, response } => {
                tracing::info!("[AgentRuntime] PermissionResponse received: request_id={}, approved={}, cancelled={}, selected_option={:?}",
                    request_id, response.approved, response.cancelled, response.selected_option);
                tracing::info!("[AgentRuntime] Calling client.respond_to_permission...");
                self.client.respond_to_permission(&request_id, response);
                tracing::info!("[AgentRuntime] client.respond_to_permission returned");
            }

            AgentRpc::Disconnect => {
                tracing::info!("[AgentRuntime] Disconnect received");
                *self.connection.write() = None;
                let _ = self.notification_tx.send(AgentNotification::Disconnected);
            }

            AgentRpc::Shutdown => {
                tracing::info!("[AgentRuntime] Shutdown received");
                *self.connection.write() = None;
                return true; // Signal to break the loop
            }
        }

        false // Continue the loop
    }

    async fn do_connect(
        config: AgentConfig,
        workspace_path: PathBuf,
        client: Arc<LapceAcpClient>,
        resume_session_id: Option<String>,
    ) -> anyhow::Result<ConnectionResult> {
        // Check if this is the embedded NeoStack agent
        if config.command == "neostack-embedded" {
            return Self::do_connect_embedded(workspace_path, client, resume_session_id).await;
        }

        tracing::info!("do_connect: Spawning agent process: {}, resume: {:?}", config.command, resume_session_id);

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
        tracing::info!("do_connect: Agent process spawned successfully");

        // Spawn a task to capture and log stderr
        if let Some(stderr) = child.stderr.take() {
            tokio::task::spawn_local(async move {
                use tokio::io::AsyncBufReadExt;
                let reader = tokio::io::BufReader::new(stderr);
                let mut lines = reader.lines();
                while let Ok(Some(line)) = lines.next_line().await {
                    tracing::warn!("Agent stderr: {}", line);
                }
            });
        }

        let stdin = child.stdin.take().ok_or_else(|| anyhow::anyhow!("No stdin"))?;
        let stdout = child.stdout.take().ok_or_else(|| anyhow::anyhow!("No stdout"))?;

        tracing::info!("do_connect: Creating ACP connection...");

        // Create the ACP connection - use a reference to the client
        let client_ref = (*client).clone();
        let (conn, io_task) = ClientSideConnection::new(
            client_ref,
            stdin.compat_write(),
            stdout.compat(),
            |fut| {
                tokio::task::spawn_local(fut);
            },
        );

        // Spawn the I/O handler locally (not Send)
        tokio::task::spawn_local(async move {
            if let Err(e) = io_task.await {
                tracing::error!("ACP I/O error: {}", e);
            }
        });

        tracing::info!("do_connect: Sending initialize request...");

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
        tracing::info!("do_connect: Initialize response received");

        // Create or resume session
        let (session_id, models, current_model_id) = if let Some(ref session_id_to_resume) = resume_session_id {
            // Try to load/resume the existing session
            tracing::info!("do_connect: Attempting to load session: {}", session_id_to_resume);

            // First, try load_session (for persisted sessions)
            let load_request = acp::LoadSessionRequest::new(
                acp::SessionId::new(session_id_to_resume.as_str()),
                workspace_path.clone(),
            );

            match conn.load_session(load_request).await {
                Ok(response) => {
                    tracing::info!("do_connect: Session loaded successfully via load_session");
                    let (models, current_model_id) = Self::extract_models_from_response(
                        response.models.as_ref()
                    );
                    (session_id_to_resume.clone(), models, current_model_id)
                }
                Err(e) => {
                    tracing::warn!("do_connect: load_session failed: {}, trying resume_session", e);

                    // Try resume_session as fallback
                    let resume_request = acp::ResumeSessionRequest::new(
                        acp::SessionId::new(session_id_to_resume.as_str()),
                        workspace_path.clone(),
                    );

                    match conn.resume_session(resume_request).await {
                        Ok(response) => {
                            tracing::info!("do_connect: Session resumed via resume_session");
                            let (models, current_model_id) = Self::extract_models_from_response(
                                response.models.as_ref()
                            );
                            (session_id_to_resume.clone(), models, current_model_id)
                        }
                        Err(e2) => {
                            tracing::warn!("do_connect: resume_session failed: {}, falling back to new_session with _meta", e2);

                            // Final fallback: use new_session with _meta workaround (for older agents)
                            let mut meta = serde_json::Map::new();
                            let mut claude_code = serde_json::Map::new();
                            let mut options = serde_json::Map::new();
                            options.insert("resume".to_string(), serde_json::Value::String(session_id_to_resume.clone()));
                            claude_code.insert("options".to_string(), serde_json::Value::Object(options));
                            meta.insert("claudeCode".to_string(), serde_json::Value::Object(claude_code));

                            let mcp_servers = Self::get_mcp_servers();
                            let session_request = acp::NewSessionRequest::new(workspace_path.clone())
                                .meta(meta)
                                .mcp_servers(mcp_servers);
                            let session_response = conn.new_session(session_request).await?;
                            let new_session_id = session_response.session_id.0.to_string();
                            tracing::info!("do_connect: Session resumed via _meta fallback, new id: {}", new_session_id);

                            let (models, current_model_id) = Self::extract_models_from_response(
                                session_response.models.as_ref()
                            );
                            (new_session_id, models, current_model_id)
                        }
                    }
                }
            }
        } else {
            // Create a new session
            tracing::info!("do_connect: Creating new session...");

            // Add MCP server if available
            let mcp_servers = Self::get_mcp_servers();
            let session_request = acp::NewSessionRequest::new(workspace_path.clone())
                .mcp_servers(mcp_servers);

            let session_response = conn.new_session(session_request).await?;
            let session_id = session_response.session_id.0.to_string();
            tracing::info!("do_connect: Session created with id: {}", session_id);

            let (models, current_model_id) = Self::extract_models_from_response(
                session_response.models.as_ref()
            );
            (session_id, models, current_model_id)
        };

        Ok(ConnectionResult {
            conn,
            session_id,
            child: Some(child),
            models,
            current_model_id,
        })
    }

    /// Connect to the embedded NeoStack agent using in-memory duplex channels
    async fn do_connect_embedded(
        workspace_path: PathBuf,
        client: Arc<LapceAcpClient>,
        resume_session_id: Option<String>,
    ) -> anyhow::Result<ConnectionResult> {
        tracing::info!("do_connect_embedded: Creating embedded NeoStack agent, resume: {:?}", resume_session_id);

        // Create duplex channels for bidirectional communication
        // Client writes to client_write, agent reads from agent_read
        // Agent writes to agent_write, client reads from client_read
        let (client_read, agent_write) = tokio::io::duplex(8192);
        let (agent_read, client_write) = tokio::io::duplex(8192);

        // Create the embedded NeoStack agent
        let neostack_agent = Rc::new(NeoStackAgent::new(workspace_path.clone()));
        let agent_impl = NeoStackAgentImpl::new(neostack_agent.clone());

        // Create the agent-side ACP connection
        let (agent_conn, agent_io) = AgentSideConnection::new(
            agent_impl,
            agent_write.compat_write(),
            agent_read.compat(),
            |fut| {
                tokio::task::spawn_local(fut);
            },
        );

        // Store the connection in the agent for sending notifications
        neostack_agent.set_connection(agent_conn);

        // Spawn the agent I/O handler
        tokio::task::spawn_local(async move {
            if let Err(e) = agent_io.await {
                tracing::error!("NeoStack agent I/O error: {}", e);
            }
        });

        tracing::info!("do_connect_embedded: Creating client-side ACP connection...");

        // Create the client-side ACP connection
        let client_ref = (*client).clone();
        let (conn, io_task) = ClientSideConnection::new(
            client_ref,
            client_write.compat_write(),
            client_read.compat(),
            |fut| {
                tokio::task::spawn_local(fut);
            },
        );

        // Spawn the client I/O handler
        tokio::task::spawn_local(async move {
            if let Err(e) = io_task.await {
                tracing::error!("ACP client I/O error: {}", e);
            }
        });

        tracing::info!("do_connect_embedded: Sending initialize request...");

        // Initialize the connection (same as subprocess path)
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
        tracing::info!("do_connect_embedded: Initialize response received");

        // Create or resume session
        let (session_id, models, current_model_id) = if let Some(ref session_id_to_resume) = resume_session_id {
            tracing::info!("do_connect_embedded: Resuming session: {}", session_id_to_resume);

            let resume_request = acp::ResumeSessionRequest::new(
                acp::SessionId::new(session_id_to_resume.as_str()),
                workspace_path.clone(),
            );

            match conn.resume_session(resume_request).await {
                Ok(response) => {
                    let (models, current_model_id) = Self::extract_models_from_response(
                        response.models.as_ref()
                    );
                    (session_id_to_resume.clone(), models, current_model_id)
                }
                Err(e) => {
                    tracing::warn!("do_connect_embedded: resume failed: {}, creating new session", e);
                    let mcp_servers = Self::get_mcp_servers();
                    let session_request = acp::NewSessionRequest::new(workspace_path.clone())
                        .mcp_servers(mcp_servers);
                    let session_response = conn.new_session(session_request).await?;
                    let session_id = session_response.session_id.0.to_string();
                    let (models, current_model_id) = Self::extract_models_from_response(
                        session_response.models.as_ref()
                    );
                    (session_id, models, current_model_id)
                }
            }
        } else {
            tracing::info!("do_connect_embedded: Creating new session...");
            let mcp_servers = Self::get_mcp_servers();
            let session_request = acp::NewSessionRequest::new(workspace_path.clone())
                .mcp_servers(mcp_servers);
            let session_response = conn.new_session(session_request).await?;
            let session_id = session_response.session_id.0.to_string();
            tracing::info!("do_connect_embedded: Session created with id: {}", session_id);

            let (models, current_model_id) = Self::extract_models_from_response(
                session_response.models.as_ref()
            );
            (session_id, models, current_model_id)
        };

        Ok(ConnectionResult {
            conn,
            session_id,
            child: None, // No child process for embedded agent
            models,
            current_model_id,
        })
    }

    /// Extract models from a session response's model state
    fn extract_models_from_response(
        model_state: Option<&acp::SessionModelState>,
    ) -> (Vec<ModelInfo>, Option<String>) {
        if let Some(state) = model_state {
            let models: Vec<ModelInfo> = state.available_models.iter().map(|m| {
                ModelInfo {
                    id: m.model_id.0.to_string(),
                    name: m.name.clone(),
                    description: m.description.clone(),
                }
            }).collect();
            let current_id = Some(state.current_model_id.0.to_string());
            (models, current_id)
        } else {
            (vec![], None)
        }
    }

    /// Get MCP servers to pass to the agent session
    fn get_mcp_servers() -> Vec<McpServer> {
        let mcp_port = get_mcp_port();
        if mcp_port > 0 {
            tracing::info!("Adding NeoStack MCP server on port {}", mcp_port);
            vec![McpServer::Http(McpServerHttp::new(
                "neostack",
                format!("http://127.0.0.1:{}/mcp", mcp_port),
            ))]
        } else {
            vec![]
        }
    }

    async fn do_prompt(
        connection: &Arc<RwLock<Option<ActiveConnection>>>,
        prompt: String,
    ) -> Result<Option<String>, String> {
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Not connected")?;

        let session_id = active.session_id.clone();
        let prompt_request = acp::PromptRequest::new(
            acp::SessionId::new(session_id.as_str()),
            vec![prompt.into()],
        );

        // Drop the guard before await since we can't hold it across await
        drop(conn_guard);

        // Re-acquire for the call
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Connection lost")?;

        let response = active.conn
            .prompt(prompt_request)
            .await
            .map_err(|e| e.to_string())?;

        Ok(Some(format!("{:?}", response.stop_reason)))
    }

    async fn do_cancel(
        connection: &Arc<RwLock<Option<ActiveConnection>>>,
    ) -> Result<(), String> {
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Not connected")?;
        let session_id = active.session_id.clone();

        let cancel = CancelNotification::new(
            acp::SessionId::new(session_id.as_str())
        );

        active.conn
            .cancel(cancel)
            .await
            .map_err(|e| e.to_string())?;

        Ok(())
    }

    async fn do_set_model(
        connection: &Arc<RwLock<Option<ActiveConnection>>>,
        session_id: String,
        model_id: String,
    ) -> Result<(), String> {
        let conn_guard = connection.read();
        let active = conn_guard.as_ref().ok_or("Not connected")?;

        let request = acp::SetSessionModelRequest::new(
            acp::SessionId::new(session_id.as_str()),
            acp::ModelId::new(model_id.as_str()),
        );

        active.conn
            .set_session_model(request)
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
