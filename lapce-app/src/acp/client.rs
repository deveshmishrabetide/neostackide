//! ACP Client implementation for Lapce
//!
//! This module implements the `Client` trait from the ACP protocol,
//! handling all requests from agents (file operations, terminal, permissions, etc.)

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

use crossbeam_channel::Sender;
use parking_lot::RwLock;

use super::types::*;
use super::{
    Client,
    ReadTextFileRequest, ReadTextFileResponse,
    WriteTextFileRequest, WriteTextFileResponse,
    CreateTerminalRequest, CreateTerminalResponse,
    TerminalOutputRequest, TerminalOutputResponse,
    ReleaseTerminalRequest, ReleaseTerminalResponse,
    WaitForTerminalExitRequest, WaitForTerminalExitResponse,
    KillTerminalCommandRequest, KillTerminalCommandResponse,
    RequestPermissionRequest, RequestPermissionResponse,
    RequestPermissionOutcome, SelectedPermissionOutcome,
    PermissionOptionId,
    SessionNotification, SessionUpdate, ContentBlock,
    TerminalId, TerminalExitStatus,
    AcpResult, AcpError,
};
use agent_client_protocol::ToolCallStatus;

/// Convert ContentBlock to text (helper function)
fn content_block_to_text(content: &ContentBlock) -> String {
    match content {
        ContentBlock::Text(text_content) => text_content.text.clone(),
        ContentBlock::Image(_) => "[Image]".to_string(),
        ContentBlock::Audio(_) => "[Audio]".to_string(),
        ContentBlock::ResourceLink(link) => format!("[Link: {}]", link.uri),
        ContentBlock::Resource(_) => "[Resource]".to_string(),
        _ => "[Unknown content]".to_string(),
    }
}

/// Lapce's implementation of the ACP Client trait.
///
/// This handles all callbacks from the agent, including:
/// - File read/write operations
/// - Terminal creation and management
/// - Permission requests
/// - Session notifications (agent output)
#[derive(Clone)]
pub struct LapceAcpClient {
    /// Working directory for file operations
    workspace_path: PathBuf,
    /// Channel to send notifications to the UI
    notification_tx: Sender<AgentNotification>,
    /// Active terminals created by the agent
    terminals: Arc<RwLock<HashMap<String, TerminalState>>>,
    /// Pending permission requests
    permission_requests: Arc<RwLock<HashMap<String, tokio::sync::oneshot::Sender<PermissionResponse>>>>,
    /// Current session ID
    session_id: Arc<RwLock<Option<SessionId>>>,
}

/// State of a terminal created by an agent
struct TerminalState {
    /// Accumulated output
    output: String,
    /// Exit status if the command has finished
    exit_status: Option<TerminalExitStatus>,
}

impl LapceAcpClient {
    /// Create a new ACP client
    pub fn new(workspace_path: PathBuf, notification_tx: Sender<AgentNotification>) -> Self {
        Self {
            workspace_path,
            notification_tx,
            terminals: Arc::new(RwLock::new(HashMap::new())),
            permission_requests: Arc::new(RwLock::new(HashMap::new())),
            session_id: Arc::new(RwLock::new(None)),
        }
    }

    /// Set the current session ID
    pub fn set_session_id(&self, session_id: SessionId) {
        *self.session_id.write() = Some(session_id);
    }

    /// Send a notification to the UI
    fn notify(&self, notification: AgentNotification) {
        let _ = self.notification_tx.send(notification);
    }

    /// Resolve a path relative to the workspace
    fn resolve_path(&self, path: &std::path::Path) -> PathBuf {
        if path.is_absolute() {
            path.to_path_buf()
        } else {
            self.workspace_path.join(path)
        }
    }

    /// Register a permission request and wait for the response
    pub fn register_permission_request(
        &self,
        id: String,
    ) -> tokio::sync::oneshot::Receiver<PermissionResponse> {
        tracing::info!("[ACP Client] register_permission_request: id={}", id);
        let (tx, rx) = tokio::sync::oneshot::channel();
        let pending_count = {
            let mut requests = self.permission_requests.write();
            requests.insert(id.clone(), tx);
            requests.len()
        };
        tracing::info!("[ACP Client] register_permission_request: registered, pending_count={}", pending_count);
        rx
    }

    /// Respond to a permission request (called from UI)
    pub fn respond_to_permission(&self, id: &str, response: PermissionResponse) {
        tracing::info!("[ACP Client] respond_to_permission: id={}, approved={}, cancelled={}, selected_option={:?}",
            id, response.approved, response.cancelled, response.selected_option);

        let pending_before = self.permission_requests.read().len();
        tracing::info!("[ACP Client] respond_to_permission: pending_requests before remove = {}", pending_before);

        if let Some(tx) = self.permission_requests.write().remove(id) {
            tracing::info!("[ACP Client] respond_to_permission: found request, sending response...");
            match tx.send(response) {
                Ok(()) => tracing::info!("[ACP Client] respond_to_permission: response sent successfully"),
                Err(_) => tracing::error!("[ACP Client] respond_to_permission: FAILED to send response - receiver dropped"),
            }
        } else {
            tracing::error!("[ACP Client] respond_to_permission: NO REQUEST FOUND for id={}", id);
            let pending_ids: Vec<String> = self.permission_requests.read().keys().cloned().collect();
            tracing::error!("[ACP Client] respond_to_permission: pending request IDs: {:?}", pending_ids);
        }

        let pending_after = self.permission_requests.read().len();
        tracing::info!("[ACP Client] respond_to_permission: pending_requests after = {}", pending_after);
    }
}

#[async_trait::async_trait(?Send)]
impl Client for LapceAcpClient {
    /// Handle session notifications from the agent.
    /// This is the main channel for agent output.
    async fn session_notification(
        &self,
        args: SessionNotification,
    ) -> AcpResult<()> {
        tracing::info!("[ACP Client] session_notification received: {:?}", std::mem::discriminant(&args.update));

        match args.update {
            SessionUpdate::AgentMessageChunk(chunk) => {
                // Extract text from content block
                let text = content_block_to_text(&chunk.content);
                tracing::debug!("[ACP Client] AgentMessageChunk: {} chars", text.len());

                // Only send TextChunk for streaming display
                // The UI will accumulate chunks and create a complete Message when done
                self.notify(AgentNotification::TextChunk { text });
            }

            SessionUpdate::ToolCall(tool_call) => {
                tracing::info!("[ACP Client] ToolCall: id={}, title={}",
                    tool_call.tool_call_id.0, tool_call.title);
                let input = tool_call.raw_input
                    .as_ref()
                    .and_then(|v| serde_json::to_string_pretty(v).ok());

                self.notify(AgentNotification::ToolStarted {
                    tool_id: tool_call.tool_call_id.0.to_string(),
                    name: tool_call.title.clone(),
                    input,
                });
            }

            SessionUpdate::ToolCallUpdate(update) => {
                tracing::info!("[ACP Client] ToolCallUpdate: id={}, status={:?}, title={:?}",
                    update.tool_call_id.0,
                    update.fields.status,
                    update.fields.title);

                let success = update.fields.status
                    .map(|s| matches!(s, ToolCallStatus::Completed))
                    .unwrap_or(true);

                tracing::info!("[ACP Client] ToolCallUpdate: computed success={}", success);

                // Extract text from tool content
                let output = update.fields.content.as_ref().map(|blocks| {
                    tracing::info!("[ACP Client] ToolCallUpdate: has {} content blocks", blocks.len());
                    blocks.iter().filter_map(|block| {
                        match block {
                            super::ToolCallContent::Content(content) => {
                                // Content has a single `content` field (ContentBlock)
                                match &content.content {
                                    ContentBlock::Text(t) => Some(t.text.clone()),
                                    _ => None,
                                }
                            }
                            super::ToolCallContent::Diff(diff) => {
                                // Show diff summary
                                Some(format!("[Diff: {}]", diff.path.display()))
                            }
                            super::ToolCallContent::Terminal(term) => {
                                Some(format!("[Terminal: {}]", term.terminal_id.0))
                            }
                            _ => None,
                        }
                    }).collect::<Vec<_>>().join("\n")
                });

                tracing::info!("[ACP Client] ToolCallUpdate: output has {} chars",
                    output.as_ref().map(|o| o.len()).unwrap_or(0));

                tracing::info!("[ACP Client] ToolCallUpdate: sending ToolCompleted notification for {}",
                    update.tool_call_id.0);
                self.notify(AgentNotification::ToolCompleted {
                    tool_id: update.tool_call_id.0.to_string(),
                    name: update.fields.title.clone().unwrap_or_else(|| update.tool_call_id.0.to_string()),
                    success,
                    output,
                });
            }

            SessionUpdate::Plan(plan) => {
                // Convert ACP plan entries to our PlanEntry type
                let entries: Vec<PlanEntry> = plan
                    .entries
                    .iter()
                    .map(|e| PlanEntry {
                        content: e.content.clone(),
                        status: match e.status {
                            agent_client_protocol::PlanEntryStatus::Pending => PlanEntryStatus::Pending,
                            agent_client_protocol::PlanEntryStatus::InProgress => PlanEntryStatus::InProgress,
                            agent_client_protocol::PlanEntryStatus::Completed => PlanEntryStatus::Completed,
                            _ => PlanEntryStatus::Pending, // Handle any future variants
                        },
                    })
                    .collect();

                self.notify(AgentNotification::PlanUpdated { entries });
            }

            SessionUpdate::AgentThoughtChunk(chunk) => {
                // Agent's internal reasoning - display in muted style
                let text = content_block_to_text(&chunk.content);
                self.notify(AgentNotification::ThinkingChunk { text });
            }

            SessionUpdate::SessionInfoUpdate(info) => {
                // Session metadata updated (title, etc.)
                use super::MaybeUndefined;
                let title = match info.title {
                    MaybeUndefined::Value(t) => Some(t),
                    MaybeUndefined::Null => Some(String::new()), // Cleared
                    MaybeUndefined::Undefined => None, // No change
                };
                if title.is_some() {
                    self.notify(AgentNotification::SessionInfoUpdated { title });
                }
            }

            _ => {
                // Handle any other updates gracefully (AvailableCommandsUpdate, CurrentModeUpdate, etc.)
            }
        }

        Ok(())
    }

    /// Read a text file requested by the agent
    async fn read_text_file(
        &self,
        args: ReadTextFileRequest,
    ) -> AcpResult<ReadTextFileResponse> {
        let path = self.resolve_path(&args.path);

        match std::fs::read_to_string(&path) {
            Ok(contents) => Ok(ReadTextFileResponse::new(contents)),
            Err(e) => Err(AcpError::new(
                -32603, // Internal error
                format!("Failed to read file {}: {}", path.display(), e)
            )),
        }
    }

    /// Write a text file requested by the agent
    async fn write_text_file(
        &self,
        args: WriteTextFileRequest,
    ) -> AcpResult<WriteTextFileResponse> {
        let path = self.resolve_path(&args.path);

        // Ensure parent directory exists
        if let Some(parent) = path.parent() {
            if !parent.exists() {
                if let Err(e) = std::fs::create_dir_all(parent) {
                    return Err(AcpError::new(
                        -32603, // Internal error
                        format!("Failed to create directory: {}", e)
                    ));
                }
            }
        }

        match std::fs::write(&path, &args.content) {
            Ok(()) => Ok(WriteTextFileResponse::default()),
            Err(e) => Err(AcpError::new(
                -32603, // Internal error
                format!("Failed to write file {}: {}", path.display(), e)
            )),
        }
    }

    /// Create a terminal for the agent to run commands
    async fn create_terminal(
        &self,
        _args: CreateTerminalRequest,
    ) -> AcpResult<CreateTerminalResponse> {
        let terminal_id = format!("term-{}", uuid::Uuid::new_v4());

        self.terminals.write().insert(
            terminal_id.clone(),
            TerminalState {
                output: String::new(),
                exit_status: None,
            },
        );

        Ok(CreateTerminalResponse::new(TerminalId::new(terminal_id)))
    }

    /// Get output from a terminal
    async fn terminal_output(
        &self,
        args: TerminalOutputRequest,
    ) -> AcpResult<TerminalOutputResponse> {
        let terminals = self.terminals.read();
        let terminal_id_str = args.terminal_id.0.to_string();

        match terminals.get(&terminal_id_str) {
            Some(state) => {
                let mut response = TerminalOutputResponse::new(state.output.clone(), false);
                if let Some(ref exit_status) = state.exit_status {
                    response = response.exit_status(exit_status.clone());
                }
                Ok(response)
            }
            None => Err(AcpError::new(
                -32602, // Invalid params
                format!("Terminal {} not found", terminal_id_str)
            )),
        }
    }

    /// Release a terminal
    async fn release_terminal(
        &self,
        args: ReleaseTerminalRequest,
    ) -> AcpResult<ReleaseTerminalResponse> {
        let terminal_id_str = args.terminal_id.0.to_string();
        self.terminals.write().remove(&terminal_id_str);
        Ok(ReleaseTerminalResponse::default())
    }

    /// Wait for a terminal command to exit
    async fn wait_for_terminal_exit(
        &self,
        args: WaitForTerminalExitRequest,
    ) -> AcpResult<WaitForTerminalExitResponse> {
        // For now, just return immediately
        // TODO: Actually wait for the terminal to exit
        let terminals = self.terminals.read();
        let terminal_id_str = args.terminal_id.0.to_string();

        match terminals.get(&terminal_id_str) {
            Some(state) => {
                let exit_status = state.exit_status.clone().unwrap_or_else(|| {
                    TerminalExitStatus::new().exit_code(0)
                });
                Ok(WaitForTerminalExitResponse::new(exit_status))
            }
            None => Err(AcpError::new(
                -32602, // Invalid params
                format!("Terminal {} not found", terminal_id_str)
            )),
        }
    }

    /// Kill a terminal command
    async fn kill_terminal_command(
        &self,
        args: KillTerminalCommandRequest,
    ) -> AcpResult<KillTerminalCommandResponse> {
        let terminal_id_str = args.terminal_id.0.to_string();
        // Mark terminal as exited
        if let Some(state) = self.terminals.write().get_mut(&terminal_id_str) {
            state.exit_status = Some(TerminalExitStatus::new().signal("SIGKILL"));
        }
        Ok(KillTerminalCommandResponse::default())
    }

    /// Handle permission request from agent
    async fn request_permission(
        &self,
        args: RequestPermissionRequest,
    ) -> AcpResult<RequestPermissionResponse> {
        tracing::info!("[ACP Client] request_permission CALLED: tool_call_id={:?}",
            args.tool_call.tool_call_id);
        tracing::info!("[ACP Client] request_permission: {} options available", args.options.len());
        for (i, opt) in args.options.iter().enumerate() {
            tracing::info!("[ACP Client] request_permission: option[{}]: id={}, name={}, kind={:?}",
                i, opt.option_id.0, opt.name, opt.kind);
        }

        // Create a unique ID for this request
        let request_id = format!("perm-{}", uuid::Uuid::new_v4());
        tracing::info!("[ACP Client] request_permission: generated request_id={}", request_id);

        // Convert options
        let options: Vec<super::types::PermissionOption> = args
            .options
            .iter()
            .map(|opt| super::types::PermissionOption {
                id: opt.option_id.0.to_string(),
                label: opt.name.clone(),
                recommended: matches!(opt.kind, super::PermissionOptionKind::AllowAlways),
            })
            .collect();

        tracing::info!("[ACP Client] request_permission: sending PermissionRequest notification to UI");
        // Send permission request to UI
        self.notify(AgentNotification::PermissionRequest(super::types::PermissionRequest {
            id: request_id.clone(),
            description: format!("Tool call: {:?}", args.tool_call.tool_call_id),
            options,
        }));

        tracing::info!("[ACP Client] request_permission: registering permission request and waiting for response...");
        // Wait for response from UI
        let rx = self.register_permission_request(request_id.clone());

        tracing::info!("[ACP Client] request_permission: awaiting oneshot channel for {}...", request_id);
        match rx.await {
            Ok(response) => {
                tracing::info!("[ACP Client] request_permission: GOT RESPONSE from UI: approved={}, cancelled={}, selected_option={:?}",
                    response.approved, response.cancelled, response.selected_option);

                let outcome = if response.cancelled {
                    tracing::info!("[ACP Client] request_permission: outcome = Cancelled (response.cancelled=true)");
                    RequestPermissionOutcome::Cancelled
                } else if response.approved {
                    // Use the selected option or a default
                    let option_id = response.selected_option
                        .map(|s| PermissionOptionId::new(s))
                        .unwrap_or_else(|| PermissionOptionId::new("allow-once"));
                    tracing::info!("[ACP Client] request_permission: outcome = Selected({})", option_id.0);
                    RequestPermissionOutcome::Selected(
                        SelectedPermissionOutcome::new(option_id)
                    )
                } else {
                    tracing::info!("[ACP Client] request_permission: outcome = Cancelled (not approved)");
                    RequestPermissionOutcome::Cancelled
                };
                tracing::info!("[ACP Client] request_permission: returning RequestPermissionResponse");
                Ok(RequestPermissionResponse::new(outcome))
            }
            Err(e) => {
                // Channel was dropped, treat as cancelled
                tracing::error!("[ACP Client] request_permission: CHANNEL ERROR: {:?}", e);
                Ok(RequestPermissionResponse::new(RequestPermissionOutcome::Cancelled))
            }
        }
    }
}
