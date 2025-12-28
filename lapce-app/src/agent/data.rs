//! Agent data structures and state management

use chrono::{DateTime, Utc};
use floem::reactive::{RwSignal, Scope, SignalGet, SignalUpdate, SignalWith};
use im::Vector;
use uuid::Uuid;

/// The AI provider for agent interactions
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum AgentProvider {
    #[default]
    Claude,
    Codex,
    Gemini,
}

impl AgentProvider {
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
        }
    }
}

/// Agent mode state
#[derive(Clone)]
pub struct AgentData {
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
            left_sidebar_open: cx.create_rw_signal(true),
            right_sidebar_open: cx.create_rw_signal(true),
            chats: cx.create_rw_signal(sample_chats),
            current_chat_id: cx.create_rw_signal(None),
            provider: cx.create_rw_signal(AgentProvider::default()),
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
}
