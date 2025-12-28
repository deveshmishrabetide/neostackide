//! Agent data structures and state management

use std::sync::Arc;

use chrono::{DateTime, Utc};
use floem::reactive::{RwSignal, Scope, SignalGet, SignalUpdate, SignalWith};
use im::{HashMap, Vector};
use uuid::Uuid;

use crate::acp::{
    AgentConnection, AgentMessage, AgentStatus, AcpEvent, PermissionRequest,
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
    scope: Scope,

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

    /// Active agent connection
    pub agent_connection: RwSignal<Option<Arc<AgentConnection>>>,

    /// Input text for the chat
    pub input_value: RwSignal<String>,

    /// Error message to display
    pub error_message: RwSignal<Option<String>>,
}

impl AgentData {
    pub fn new(cx: Scope) -> Self {
        // Create some sample chats for development
        let sample_chats = Vector::from(vec![
            Chat::new("Implementing user auth", AgentProvider::Claude),
            Chat::new("Fix database migration", AgentProvider::Codex),
            Chat::new("Refactor API endpoints", AgentProvider::Claude),
        ]);

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
            agent_connection: cx.create_rw_signal(None),
            input_value: cx.create_rw_signal(String::new()),
            error_message: cx.create_rw_signal(None),
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
    }

    /// Select a chat by ID
    pub fn select_chat(&self, chat_id: &str) {
        self.current_chat_id.set(Some(chat_id.to_string()));
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
        current_id
            .and_then(|id| {
                self.messages.with(|msgs| msgs.get(&id).cloned())
            })
            .unwrap_or_default()
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
    pub fn flush_streaming_text(&self, chat_id: &str) {
        let text = self.streaming_text.with(|s| s.get(chat_id).cloned());
        if let Some(text) = text {
            if !text.is_empty() {
                self.add_message(chat_id, AgentMessage {
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

    /// Handle an ACP event
    pub fn handle_acp_event(&self, chat_id: &str, event: AcpEvent) {
        match event {
            AcpEvent::StatusChanged(status) => {
                self.agent_status.set(status);
            }
            AcpEvent::MessageReceived(message) => {
                self.add_message(chat_id, message);
            }
            AcpEvent::MessageChunk { text } => {
                self.append_streaming_text(chat_id, &text);
            }
            AcpEvent::ToolUseStarted { name, input: _ } => {
                self.add_message(chat_id, AgentMessage {
                    role: crate::acp::MessageRole::Agent,
                    content: crate::acp::MessageContent::ToolUse {
                        name,
                        status: crate::acp::ToolUseStatus::InProgress,
                    },
                    timestamp: std::time::Instant::now(),
                });
            }
            AcpEvent::ToolUseCompleted { name, success } => {
                let status = if success {
                    crate::acp::ToolUseStatus::Success
                } else {
                    crate::acp::ToolUseStatus::Failed
                };
                self.add_message(chat_id, AgentMessage {
                    role: crate::acp::MessageRole::Agent,
                    content: crate::acp::MessageContent::ToolUse { name, status },
                    timestamp: std::time::Instant::now(),
                });
            }
            AcpEvent::PermissionRequested(request) => {
                self.add_permission_request(chat_id, request);
            }
            AcpEvent::SessionCreated { session_id: _ } => {
                // Session created, ready to chat
            }
            AcpEvent::Error(error) => {
                self.set_error(Some(error.clone()));
                self.chats.update(|chats| {
                    if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                        chat.status = ChatStatus::Error;
                    }
                });
            }
        }
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
