//! Agent data structures and state management

use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};

use chrono::{DateTime, Utc};
use floem::ext_event::create_signal_from_channel;
use floem::reactive::{ReadSignal, RwSignal, Scope, SignalGet, SignalUpdate, SignalWith};
use im::{HashMap, Vector};
use uuid::Uuid;

use crate::acp::{
    AgentConfig, AgentMessage, AgentNotification, AgentRpcHandler, AgentStatus,
    MessageContent, MessageRole, PermissionRequest, ToolUseStatus,
    start_agent_runtime,
};

/// The AI provider for agent interactions
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum AgentProvider {
    #[default]
    Claude,
    Codex,
    Gemini,
}

impl AgentProvider {
    /// Get all available providers
    pub fn all() -> &'static [AgentProvider] {
        &[
            AgentProvider::Claude,
            AgentProvider::Codex,
            AgentProvider::Gemini,
        ]
    }

    pub fn display_name(&self) -> &'static str {
        match self {
            AgentProvider::Claude => "Claude",
            AgentProvider::Codex => "Codex",
            AgentProvider::Gemini => "Gemini",
        }
    }
}

/// Status of a chat session
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ChatStatus {
    #[default]
    Idle,
    Streaming,
    Completed,
    Error,
}

/// A single chat session
#[derive(Debug, Clone)]
pub struct Chat {
    pub id: String,
    pub title: String,
    pub provider: AgentProvider,
    pub status: ChatStatus,
    pub created_at: DateTime<Utc>,
    pub unread: bool,
    /// Total cost in USD for this chat
    pub cost_usd: f64,
    /// Total token count
    pub token_count: u64,
}

impl Chat {
    pub fn new(title: impl Into<String>, provider: AgentProvider) -> Self {
        Self {
            id: Uuid::new_v4().to_string(),
            title: title.into(),
            provider,
            status: ChatStatus::Idle,
            created_at: Utc::now(),
            unread: false,
            cost_usd: 0.0,
            token_count: 0,
        }
    }
}

/// Agent mode state
#[derive(Clone)]
pub struct AgentData {
    /// Scope for creating signals
    pub scope: Scope,

    /// Whether the left sidebar is open
    pub left_sidebar_open: RwSignal<bool>,

    /// Whether the right sidebar is open
    pub right_sidebar_open: RwSignal<bool>,

    /// All chat sessions
    pub chats: RwSignal<Vector<Chat>>,

    /// Currently selected chat ID
    pub current_chat_id: RwSignal<Option<String>>,

    /// Currently selected provider
    pub provider: RwSignal<AgentProvider>,

    /// Messages per chat (chat_id -> messages)
    pub messages: RwSignal<HashMap<String, Vector<AgentMessage>>>,

    /// Current streaming text buffer per chat
    pub streaming_text: RwSignal<HashMap<String, String>>,

    /// Whether currently streaming per chat
    pub is_streaming: RwSignal<HashMap<String, bool>>,

    /// Pending permission requests per chat
    pub pending_permissions: RwSignal<HashMap<String, Vector<PermissionRequest>>>,

    /// Agent connection status
    pub agent_status: RwSignal<AgentStatus>,

    /// Current session ID (from agent connection)
    pub session_id: RwSignal<Option<String>>,

    /// RPC handler for communicating with agent runtime
    pub rpc: Arc<AgentRpcHandler>,

    /// Notification signal from agent runtime
    pub notification: ReadSignal<Option<AgentNotification>>,

    /// Input text for the chat
    pub input_value: RwSignal<String>,

    /// Error message to display
    pub error_message: RwSignal<Option<String>>,

    /// Pending prompt to send when connection is established
    pub pending_prompt: RwSignal<Option<String>>,

    /// Counter for generating unique message IDs
    message_id_counter: Arc<AtomicU64>,
}

impl AgentData {
    pub fn new(cx: Scope, workspace_path: PathBuf) -> Self {
        // Create some sample chats for development
        let sample_chats = Vector::from(vec![
            Chat::new("Implementing user auth", AgentProvider::Claude),
            Chat::new("Fix database migration", AgentProvider::Codex),
            Chat::new("Refactor API endpoints", AgentProvider::Claude),
        ]);

        // Create the RPC handler and notification channels
        // We use crossbeam for the runtime (thread-safe), and mpsc for Floem signal
        let rpc = Arc::new(AgentRpcHandler::new());
        let (runtime_tx, runtime_rx) = crossbeam_channel::unbounded();
        let (ui_tx, ui_rx) = std::sync::mpsc::channel();

        // Bridge the crossbeam channel to mpsc for Floem
        std::thread::spawn({
            move || {
                for notification in runtime_rx {
                    if ui_tx.send(notification).is_err() {
                        break;
                    }
                }
            }
        });

        // Start the agent runtime in a background thread
        start_agent_runtime(
            (*rpc).clone(),
            runtime_tx,
            workspace_path,
        );

        // Create a signal from the mpsc channel (Floem requires mpsc)
        let notification = create_signal_from_channel(ui_rx);

        Self {
            scope: cx,
            left_sidebar_open: cx.create_rw_signal(true),
            right_sidebar_open: cx.create_rw_signal(true),
            chats: cx.create_rw_signal(sample_chats),
            current_chat_id: cx.create_rw_signal(None),
            provider: cx.create_rw_signal(AgentProvider::default()),
            messages: cx.create_rw_signal(HashMap::new()),
            streaming_text: cx.create_rw_signal(HashMap::new()),
            is_streaming: cx.create_rw_signal(HashMap::new()),
            pending_permissions: cx.create_rw_signal(HashMap::new()),
            agent_status: cx.create_rw_signal(AgentStatus::Disconnected),
            session_id: cx.create_rw_signal(None),
            rpc,
            notification,
            input_value: cx.create_rw_signal(String::new()),
            error_message: cx.create_rw_signal(None),
            pending_prompt: cx.create_rw_signal(None),
            message_id_counter: Arc::new(AtomicU64::new(0)),
        }
    }

    /// Generate a unique message ID
    fn next_message_id(&self) -> u64 {
        self.message_id_counter.fetch_add(1, Ordering::Relaxed)
    }

    /// Connect to an agent with the given provider
    pub fn connect(&self, provider: AgentProvider) {
        let config = match provider {
            AgentProvider::Claude => AgentConfig::claude_code(),
            AgentProvider::Codex => AgentConfig::codex(),
            AgentProvider::Gemini => AgentConfig::gemini_cli(),
        };

        tracing::info!("Connecting to agent: {:?} (command: {})", provider, config.command);
        self.agent_status.set(AgentStatus::Connecting);
        self.rpc.connect(config, PathBuf::from("."));
    }

    /// Send a prompt to the agent
    pub fn send_prompt(&self, prompt: String) {
        if let Some(session_id) = self.session_id.get_untracked() {
            // Connected - send immediately
            self.do_send_prompt(session_id, prompt);
        } else {
            // Check if we're currently connecting
            let status = self.agent_status.get_untracked();
            match status {
                AgentStatus::Connecting => {
                    // Queue the prompt to send when connection completes
                    self.pending_prompt.set(Some(prompt));
                }
                AgentStatus::Disconnected | AgentStatus::Error => {
                    // Not connected and not connecting - try to connect first
                    self.pending_prompt.set(Some(prompt));
                    self.ensure_connected();
                }
                _ => {
                    self.set_error(Some("Not connected to agent".to_string()));
                }
            }
        }
    }

    /// Internal: actually send the prompt (when we have a session_id)
    fn do_send_prompt(&self, session_id: String, prompt: String) {
        tracing::info!("do_send_prompt called with session_id: {}", session_id);

        // Mark current chat as streaming
        if let Some(chat_id) = self.current_chat_id.get_untracked() {
            tracing::info!("Current chat_id: {}", chat_id);
            self.set_streaming(&chat_id, true);

            // Check if this is the first message - update chat title
            let is_first_message = self.messages.with(|msgs| {
                msgs.get(&chat_id).map(|m| m.is_empty()).unwrap_or(true)
            });

            if is_first_message {
                // Use first ~50 chars of the prompt as the chat title
                let title = if prompt.len() > 50 {
                    format!("{}...", &prompt[..47])
                } else {
                    prompt.clone()
                };
                self.update_chat_title(&chat_id, &title);
            }

            // Add user message
            tracing::info!("Adding user message to chat: {}", chat_id);
            self.add_message(&chat_id, AgentMessage {
                id: self.next_message_id(),
                role: MessageRole::User,
                content: MessageContent::Text(prompt.clone()),
                timestamp: std::time::Instant::now(),
            });

            // Log the message count after adding
            let count = self.messages.with(|msgs| {
                msgs.get(&chat_id).map(|m| m.len()).unwrap_or(0)
            });
            tracing::info!("Messages in chat after adding: {}", count);
        } else {
            tracing::warn!("No current chat_id when trying to send prompt");
        }

        self.agent_status.set(AgentStatus::Processing);
        self.rpc.prompt_async(session_id, prompt, |_result| {
            // Response will come through notification channel
        });
    }

    /// Cancel the current operation
    pub fn cancel(&self) {
        if let Some(session_id) = self.session_id.get_untracked() {
            self.rpc.cancel(session_id);
        }
    }

    /// Create a new chat and make it the current one
    pub fn new_chat(&self) {
        let provider = self.provider.get_untracked();
        let chat = Chat::new("New chat", provider);
        let chat_id = chat.id.clone();

        self.chats.update(|chats| {
            chats.push_front(chat);
        });
        self.current_chat_id.set(Some(chat_id));

        // Auto-connect if not already connected
        self.ensure_connected();
    }

    /// Select a chat by ID
    pub fn select_chat(&self, chat_id: &str) {
        self.current_chat_id.set(Some(chat_id.to_string()));

        // Auto-connect if not already connected
        self.ensure_connected();
    }

    /// Ensure we are connected to an agent, connecting if necessary
    fn ensure_connected(&self) {
        let status = self.agent_status.get_untracked();
        match status {
            AgentStatus::Disconnected | AgentStatus::Error => {
                // Need to connect
                let provider = self.provider.get_untracked();
                self.connect(provider);
            }
            AgentStatus::Connecting | AgentStatus::Connected | AgentStatus::Processing => {
                // Already connected or connecting, nothing to do
            }
        }
    }

    /// Get the current chat
    pub fn current_chat(&self) -> Option<Chat> {
        let current_id = self.current_chat_id.get();
        current_id.and_then(|id| {
            self.chats.with(|chats: &Vector<Chat>| {
                chats.iter().find(|c| c.id == id).cloned()
            })
        })
    }

    /// Get messages for the current chat
    pub fn current_messages(&self) -> Vector<AgentMessage> {
        let current_id = self.current_chat_id.get();
        let messages = current_id.clone()
            .and_then(|id| {
                self.messages.with(|msgs| msgs.get(&id).cloned())
            })
            .unwrap_or_default();
        tracing::debug!("current_messages() returning {} messages for chat {:?}", messages.len(), current_id);
        messages
    }

    /// Check if current chat is streaming
    pub fn is_current_streaming(&self) -> bool {
        let current_id = self.current_chat_id.get();
        current_id
            .map(|id| {
                self.is_streaming.with(|s| s.get(&id).copied().unwrap_or(false))
            })
            .unwrap_or(false)
    }

    /// Get streaming text for current chat
    pub fn current_streaming_text(&self) -> String {
        let current_id = self.current_chat_id.get();
        current_id
            .and_then(|id| {
                self.streaming_text.with(|s| s.get(&id).cloned())
            })
            .unwrap_or_default()
    }

    /// Get pending permissions for current chat
    pub fn current_pending_permissions(&self) -> Vector<PermissionRequest> {
        let current_id = self.current_chat_id.get();
        current_id
            .and_then(|id| {
                self.pending_permissions.with(|p| p.get(&id).cloned())
            })
            .unwrap_or_default()
    }

    /// Add a message to a chat
    pub fn add_message(&self, chat_id: &str, message: AgentMessage) {
        self.messages.update(|msgs| {
            let chat_messages = msgs.entry(chat_id.to_string()).or_insert_with(Vector::new);
            chat_messages.push_back(message);
        });
    }

    /// Update streaming state for a chat
    pub fn set_streaming(&self, chat_id: &str, streaming: bool) {
        self.is_streaming.update(|s| {
            s.insert(chat_id.to_string(), streaming);
        });

        // Update chat status
        self.chats.update(|chats| {
            if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                chat.status = if streaming { ChatStatus::Streaming } else { ChatStatus::Idle };
            }
        });
    }

    /// Append text to streaming buffer
    pub fn append_streaming_text(&self, chat_id: &str, text: &str) {
        self.streaming_text.update(|s| {
            let entry = s.entry(chat_id.to_string()).or_insert_with(String::new);
            entry.push_str(text);
        });
    }

    /// Flush streaming text to a message
    /// Uses untracked reads to avoid creating reactive dependencies
    pub fn flush_streaming_text(&self, chat_id: &str) {
        let text = self.streaming_text.with_untracked(|s| s.get(chat_id).cloned());
        if let Some(text) = text {
            if !text.is_empty() {
                self.add_message(chat_id, AgentMessage {
                    id: self.next_message_id(),
                    role: crate::acp::MessageRole::Agent,
                    content: crate::acp::MessageContent::Text(text),
                    timestamp: std::time::Instant::now(),
                });
            }
        }
        self.streaming_text.update(|s| {
            s.remove(chat_id);
        });
    }

    /// Add a permission request
    pub fn add_permission_request(&self, chat_id: &str, request: PermissionRequest) {
        self.pending_permissions.update(|p| {
            let perms = p.entry(chat_id.to_string()).or_insert_with(Vector::new);
            perms.push_back(request);
        });
    }

    /// Remove a permission request
    pub fn remove_permission_request(&self, chat_id: &str, request_id: &str) {
        self.pending_permissions.update(|p| {
            if let Some(perms) = p.get_mut(chat_id) {
                perms.retain(|r| r.id != request_id);
            }
        });
    }

    /// Set error message
    pub fn set_error(&self, error: Option<String>) {
        self.error_message.set(error);
    }

    /// Clear error message
    pub fn clear_error(&self) {
        self.error_message.set(None);
    }

    /// Handle a notification from the agent runtime
    pub fn handle_notification(&self, notification: AgentNotification) {
        let chat_id = self.current_chat_id.get_untracked();

        match notification {
            AgentNotification::Connected { session_id } => {
                tracing::info!("Agent connected with session_id: {}", session_id);
                self.session_id.set(Some(session_id.clone()));
                self.agent_status.set(AgentStatus::Connected);

                // Send any pending prompt that was queued while connecting
                if let Some(prompt) = self.pending_prompt.get_untracked() {
                    tracing::info!("Sending queued prompt after connection");
                    self.pending_prompt.set(None);
                    self.do_send_prompt(session_id, prompt);
                }
            }
            AgentNotification::Disconnected => {
                tracing::info!("Agent disconnected");
                self.session_id.set(None);
                self.agent_status.set(AgentStatus::Disconnected);
            }
            AgentNotification::StatusChanged(status) => {
                self.agent_status.set(status);
            }
            AgentNotification::TextChunk { text } => {
                if let Some(ref chat_id) = chat_id {
                    self.append_streaming_text(chat_id, &text);
                }
            }
            AgentNotification::Message(mut message) => {
                if let Some(ref chat_id) = chat_id {
                    // Assign an ID if the message doesn't have one
                    if message.id == 0 {
                        message.id = self.next_message_id();
                    }
                    self.add_message(chat_id, message);
                }
            }
            AgentNotification::ToolStarted { tool_id: _, name, input: _ } => {
                if let Some(ref chat_id) = chat_id {
                    self.add_message(chat_id, AgentMessage {
                        id: self.next_message_id(),
                        role: MessageRole::Agent,
                        content: MessageContent::ToolUse {
                            name,
                            status: ToolUseStatus::InProgress,
                        },
                        timestamp: std::time::Instant::now(),
                    });
                }
            }
            AgentNotification::ToolCompleted { tool_id: _, name, success, output: _ } => {
                if let Some(ref chat_id) = chat_id {
                    let status = if success {
                        ToolUseStatus::Success
                    } else {
                        ToolUseStatus::Failed
                    };
                    self.add_message(chat_id, AgentMessage {
                        id: self.next_message_id(),
                        role: MessageRole::Agent,
                        content: MessageContent::ToolUse { name, status },
                        timestamp: std::time::Instant::now(),
                    });
                }
            }
            AgentNotification::PermissionRequest(request) => {
                if let Some(ref chat_id) = chat_id {
                    self.add_permission_request(chat_id, request);
                }
            }
            AgentNotification::TurnCompleted { stop_reason: _ } => {
                if let Some(ref chat_id) = chat_id {
                    // Flush the streaming buffer to create a complete message
                    self.flush_streaming_text(chat_id);
                    // End streaming state
                    self.set_streaming(chat_id, false);
                    // Update chat status to completed
                    self.chats.update(|chats| {
                        if let Some(chat) = chats.iter_mut().find(|c| &c.id == chat_id) {
                            chat.status = ChatStatus::Completed;
                        }
                    });
                }
                // Update agent status back to connected/ready
                self.agent_status.set(AgentStatus::Connected);
            }
            AgentNotification::Error { message } => {
                tracing::error!("Agent error: {}", message);
                self.set_error(Some(message.clone()));
                if let Some(ref chat_id) = chat_id {
                    self.chats.update(|chats| {
                        if let Some(chat) = chats.iter_mut().find(|c| &c.id == chat_id) {
                            chat.status = ChatStatus::Error;
                        }
                    });
                    // End streaming on error
                    self.set_streaming(chat_id, false);
                }
            }
        }
    }

    /// Set up notification processing effect (call once after creation)
    pub fn setup_notification_effect(&self) {
        let agent = self.clone();
        let notification = self.notification;

        self.scope.create_effect(move |_| {
            if let Some(notification) = notification.get() {
                agent.handle_notification(notification);
            }
        });
    }

    /// Update chat title based on first message
    pub fn update_chat_title(&self, chat_id: &str, title: &str) {
        self.chats.update(|chats| {
            if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                // Truncate to ~50 chars
                let truncated = if title.len() > 50 {
                    format!("{}...", &title[..47])
                } else {
                    title.to_string()
                };
                chat.title = truncated;
            }
        });
    }
}
