//! Agent connection management
//!
//! This module handles spawning agent processes and managing
//! the ACP connection lifecycle.

use std::path::PathBuf;
use std::process::Stdio;
use std::sync::Arc;

use anyhow::{Context, Result};
use crossbeam_channel::{Receiver, Sender, unbounded};
use floem::reactive::{RwSignal, SignalUpdate, SignalGet};
use parking_lot::RwLock;
use tokio::process::{Child, Command};
use tokio_util::compat::{TokioAsyncReadCompatExt, TokioAsyncWriteCompatExt};

use super::client::LapceAcpClient;
use super::types::*;
use super::{
    acp, Agent, ClientSideConnection, ProtocolVersion, Implementation,
    ClientCapabilities, FileSystemCapability, CancelNotification,
};

pub use super::types::AgentConfig;
pub use super::types::AgentStatus;

/// Manages the connection to an ACP agent
pub struct AgentConnection {
    /// Configuration for the agent
    config: AgentConfig,
    /// Current status
    status: RwSignal<AgentStatus>,
    /// Event receiver for UI (notifications from agent)
    event_rx: Receiver<AgentNotification>,
    /// Event sender (held by client)
    event_tx: Sender<AgentNotification>,
    /// The ACP client implementation
    client: Arc<LapceAcpClient>,
    /// Current session ID
    session_id: RwSignal<Option<SessionId>>,
    /// Workspace path
    workspace_path: PathBuf,
    /// Connection handle (for sending prompts)
    connection: Arc<RwLock<Option<ConnectionHandle>>>,
}

/// Handle to the active connection for sending messages
struct ConnectionHandle {
    /// The ACP connection
    conn: ClientSideConnection,
    /// The agent child process
    _child: Child,
}

impl AgentConnection {
    /// Create a new agent connection (not yet connected)
    pub fn new(
        config: AgentConfig,
        workspace_path: PathBuf,
        status: RwSignal<AgentStatus>,
        session_id: RwSignal<Option<SessionId>>,
    ) -> Self {
        let (event_tx, event_rx) = unbounded();
        let client = Arc::new(LapceAcpClient::new(workspace_path.clone(), event_tx.clone()));

        Self {
            config,
            status,
            event_rx,
            event_tx,
            client,
            session_id,
            workspace_path,
            connection: Arc::new(RwLock::new(None)),
        }
    }

    /// Get the event receiver for UI updates
    pub fn event_rx(&self) -> &Receiver<AgentNotification> {
        &self.event_rx
    }

    /// Get the current status
    pub fn status(&self) -> AgentStatus {
        self.status.get()
    }

    /// Get the current session ID
    pub fn get_session_id(&self) -> Option<SessionId> {
        self.session_id.get()
    }

    /// Check if connected
    pub fn is_connected(&self) -> bool {
        self.connection.read().is_some()
    }

    /// Respond to a permission request
    pub fn respond_to_permission(&self, id: &str, response: PermissionResponse) {
        self.client.respond_to_permission(id, response);
    }

    /// Connect to the agent and start a session
    pub async fn connect(&self) -> Result<()> {
        self.status.set(AgentStatus::Connecting);
        self.notify(AgentNotification::StatusChanged(AgentStatus::Connecting));

        // Spawn the agent process
        let mut cmd = Command::new(&self.config.command);
        cmd.args(&self.config.args)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);

        // Set working directory
        if let Some(cwd) = &self.config.cwd {
            cmd.current_dir(cwd);
        } else {
            cmd.current_dir(&self.workspace_path);
        }

        // Set environment variables
        for (key, value) in &self.config.env {
            cmd.env(key, value);
        }

        let mut child = cmd.spawn().context("Failed to spawn agent process")?;

        let stdin = child.stdin.take().context("Failed to get stdin")?;
        let stdout = child.stdout.take().context("Failed to get stdout")?;

        // Create the connection
        let client = LapceAcpClient::new(self.workspace_path.clone(), self.event_tx.clone());

        let (conn, io_task) = ClientSideConnection::new(
            client,
            stdin.compat_write(),
            stdout.compat(),
            |fut| {
                tokio::task::spawn_local(fut);
            },
        );

        // Spawn the I/O handler
        tokio::task::spawn_local(async move {
            if let Err(e) = io_task.await {
                tracing::error!("ACP I/O error: {}", e);
            }
        });

        // Initialize the connection using builder pattern
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

        let init_response = conn
            .initialize(init_request)
            .await
            .context("Failed to initialize ACP connection")?;

        tracing::info!(
            "Connected to agent: {} v{}",
            init_response
                .agent_info
                .as_ref()
                .map(|i| i.name.as_str())
                .unwrap_or("unknown"),
            init_response
                .agent_info
                .as_ref()
                .map(|i| i.version.as_str())
                .unwrap_or("unknown"),
        );

        // Create a new session using builder pattern
        let session_request = acp::NewSessionRequest::new(self.workspace_path.clone());

        let session_response = conn
            .new_session(session_request)
            .await
            .context("Failed to create session")?;

        let session_id = session_response.session_id.clone();
        self.session_id.set(Some(session_id.0.to_string()));
        self.client.set_session_id(session_id.0.to_string());

        // Store the connection handle
        *self.connection.write() = Some(ConnectionHandle { conn, _child: child });

        self.status.set(AgentStatus::Connected);
        self.notify(AgentNotification::StatusChanged(AgentStatus::Connected));
        self.notify(AgentNotification::Connected { session_id: session_id.0.to_string() });

        Ok(())
    }

    /// Disconnect from the agent
    pub fn disconnect(&self) {
        *self.connection.write() = None;
        self.session_id.set(None);
        self.status.set(AgentStatus::Disconnected);
        self.notify(AgentNotification::Disconnected);
    }

    /// Send a prompt to the agent
    pub async fn send_prompt(&self, prompt: String) -> Result<()> {
        let conn_guard = self.connection.read();
        let _handle = conn_guard
            .as_ref()
            .context("Not connected to agent")?;

        let session_id_str = self.session_id.get().context("No active session")?;

        self.status.set(AgentStatus::Processing);
        self.notify(AgentNotification::StatusChanged(AgentStatus::Processing));

        // Send user message event
        self.notify(AgentNotification::Message(AgentMessage {
            id: 0, // Will be assigned by AgentData
            role: MessageRole::User,
            content: MessageContent::Text(prompt.clone()),
            timestamp: std::time::Instant::now(),
        }));

        // Drop the guard before await
        drop(conn_guard);

        // Re-acquire for the actual call
        let conn_guard = self.connection.read();
        let handle = conn_guard.as_ref().context("Connection lost")?;

        // Create prompt request using builder pattern
        let prompt_request = acp::PromptRequest::new(
            acp::SessionId::new(session_id_str.as_str()),
            vec![prompt.into()],
        );

        let response = handle
            .conn
            .prompt(prompt_request)
            .await
            .context("Failed to send prompt")?;

        self.status.set(AgentStatus::Connected);
        self.notify(AgentNotification::StatusChanged(AgentStatus::Connected));

        tracing::debug!("Prompt completed with stop reason: {:?}", response.stop_reason);

        Ok(())
    }

    /// Cancel the current prompt
    pub async fn cancel(&self) -> Result<()> {
        let conn_guard = self.connection.read();
        let handle = conn_guard.as_ref().context("Not connected to agent")?;
        let session_id_str = self.session_id.get().context("No active session")?;

        // Create cancel notification
        let cancel_notification = CancelNotification::new(
            acp::SessionId::new(session_id_str.as_str())
        );

        handle
            .conn
            .cancel(cancel_notification)
            .await
            .context("Failed to cancel")?;

        self.status.set(AgentStatus::Connected);
        self.notify(AgentNotification::StatusChanged(AgentStatus::Connected));

        Ok(())
    }

    fn notify(&self, notification: AgentNotification) {
        let _ = self.event_tx.send(notification);
    }
}

/// Manager for multiple agent connections
pub struct AgentManager {
    /// Available agent configurations
    available_agents: Vec<AgentConfig>,
    /// Current active connection
    active_connection: RwSignal<Option<Arc<AgentConnection>>>,
    /// Workspace path
    workspace_path: PathBuf,
}

impl AgentManager {
    /// Create a new agent manager
    pub fn new(workspace_path: PathBuf, active_connection: RwSignal<Option<Arc<AgentConnection>>>) -> Self {
        Self {
            available_agents: vec![
                AgentConfig::claude_code(),
                AgentConfig::gemini_cli(),
                AgentConfig::codex(),
            ],
            active_connection,
            workspace_path,
        }
    }

    /// Get available agent configurations
    pub fn available_agents(&self) -> &[AgentConfig] {
        &self.available_agents
    }

    /// Add a custom agent configuration
    pub fn add_agent(&mut self, config: AgentConfig) {
        self.available_agents.push(config);
    }

    /// Get the active connection
    pub fn active_connection(&self) -> Option<Arc<AgentConnection>> {
        self.active_connection.get()
    }

    /// Create a new connection to an agent
    pub fn create_connection(
        &self,
        config: AgentConfig,
        status: RwSignal<AgentStatus>,
        session_id: RwSignal<Option<SessionId>>,
    ) -> Arc<AgentConnection> {
        let conn = Arc::new(AgentConnection::new(
            config,
            self.workspace_path.clone(),
            status,
            session_id,
        ));
        self.active_connection.set(Some(conn.clone()));
        conn
    }
}
