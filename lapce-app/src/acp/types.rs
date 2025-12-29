//! Types for ACP integration

use std::path::PathBuf;
use serde::{Deserialize, Serialize};

/// Unique identifier for an agent session
pub type SessionId = String;

/// Configuration for an ACP agent
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    /// Display name for the agent
    pub name: String,
    /// Command to run the agent
    pub command: String,
    /// Arguments to pass to the agent
    #[serde(default)]
    pub args: Vec<String>,
    /// Environment variables to set
    #[serde(default)]
    pub env: Vec<(String, String)>,
    /// Working directory (defaults to workspace root)
    pub cwd: Option<PathBuf>,
}

impl AgentConfig {
    /// Create a config for Claude Code (via ACP adapter)
    /// Uses the @zed-industries/claude-code-acp npm package
    pub fn claude_code() -> Self {
        Self {
            name: "Claude Code".to_string(),
            command: "claude-code-acp".to_string(),
            args: vec![],
            env: vec![],
            cwd: None,
        }
    }

    /// Create a config for Gemini CLI
    pub fn gemini_cli() -> Self {
        Self {
            name: "Gemini CLI".to_string(),
            command: "gemini".to_string(),
            args: vec![],
            env: vec![],
            cwd: None,
        }
    }

    /// Create a config for OpenAI Codex
    pub fn codex() -> Self {
        Self {
            name: "Codex".to_string(),
            command: "codex".to_string(),
            args: vec![],
            env: vec![],
            cwd: None,
        }
    }

    /// Create a config for embedded NeoStack Agent
    /// Uses in-memory duplex channels instead of subprocess
    pub fn neostack() -> Self {
        Self {
            name: "NeoStack Agent".to_string(),
            command: "neostack-embedded".to_string(), // Special marker for embedded agent
            args: vec![],
            env: vec![],
            cwd: None,
        }
    }

    /// Create a custom agent config
    pub fn custom(name: impl Into<String>, command: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            command: command.into(),
            args: vec![],
            env: vec![],
            cwd: None,
        }
    }

    /// Add arguments to the config
    pub fn with_args(mut self, args: Vec<String>) -> Self {
        self.args = args;
        self
    }

    /// Add environment variables to the config
    pub fn with_env(mut self, env: Vec<(String, String)>) -> Self {
        self.env = env;
        self
    }

    /// Set working directory
    pub fn with_cwd(mut self, cwd: PathBuf) -> Self {
        self.cwd = Some(cwd);
        self
    }
}

/// Status of an agent connection
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AgentStatus {
    /// Not connected to any agent
    Disconnected,
    /// Currently connecting to agent
    Connecting,
    /// Connected and ready
    Connected,
    /// Agent is processing a prompt
    Processing,
    /// An error occurred
    Error,
}

/// A message in the agent conversation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentMessage {
    /// Unique ID for this message (used for stable UI keying)
    pub id: u64,
    /// Who sent the message
    pub role: MessageRole,
    /// The message parts (text, tool calls, etc.) in order
    pub parts: Vec<MessagePart>,
    /// Timestamp when the message was created (not serialized)
    #[serde(skip, default = "std::time::Instant::now")]
    pub timestamp: std::time::Instant,
}

/// Role of the message sender
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum MessageRole {
    /// Message from the user
    User,
    /// Message from the agent
    Agent,
    /// System message (errors, status updates)
    System,
}

/// State of a tool call execution
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ToolCallState {
    /// Tool is currently executing
    Running,
    /// Tool completed successfully
    Completed,
    /// Tool failed
    Failed,
}

/// A part of a message (text, tool call, etc.)
/// Messages contain multiple parts to support interleaved content.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum MessagePart {
    /// Plain text content
    Text(String),

    /// Thinking/reasoning block (collapsible)
    Reasoning(String),

    /// Tool call with merged result (single collapsible unit)
    ToolCall {
        /// Unique identifier for this tool call
        tool_call_id: String,
        /// Name of the tool being called
        name: String,
        /// Pretty-printed JSON input arguments
        args: Option<String>,
        /// Current execution state
        state: ToolCallState,
        /// Result output (populated when completed)
        output: Option<String>,
        /// Whether the result was an error
        is_error: bool,
    },

    /// Error message
    Error(String),
}

/// A request for permission from the agent
#[derive(Debug, Clone)]
pub struct PermissionRequest {
    /// Unique ID for this request
    pub id: String,
    /// Description of what permission is being requested
    pub description: String,
    /// The options available
    pub options: Vec<PermissionOption>,
}

/// An option in a permission request
#[derive(Debug, Clone)]
pub struct PermissionOption {
    /// ID of this option
    pub id: String,
    /// Display label
    pub label: String,
    /// Whether this is the recommended option
    pub recommended: bool,
}

/// Response to a permission request
#[derive(Debug, Clone)]
pub struct PermissionResponse {
    /// Whether permission was granted
    pub approved: bool,
    /// Whether the request was cancelled
    pub cancelled: bool,
    /// Which option was selected (if applicable)
    pub selected_option: Option<String>,
}

/// Information about an available model
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModelInfo {
    /// Unique model ID (used when setting model)
    pub id: String,
    /// Display name for the model
    pub name: String,
    /// Optional description
    pub description: Option<String>,
}

/// Status of a plan/todo entry
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlanEntryStatus {
    /// Task not started yet
    Pending,
    /// Task currently being worked on
    InProgress,
    /// Task completed
    Completed,
}

/// A single entry in the agent's execution plan (todo item)
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlanEntry {
    /// Human-readable description of the task
    pub content: String,
    /// Current status of the task
    pub status: PlanEntryStatus,
}

/// Statistics about the current plan
#[derive(Debug, Clone, Default, PartialEq)]
pub struct PlanStats {
    pub total: usize,
    pub completed: usize,
    pub in_progress: usize,
    pub pending: usize,
    /// Progress percentage (0-100)
    pub progress: f32,
}

impl PlanStats {
    pub fn from_entries(entries: &[PlanEntry]) -> Self {
        let total = entries.len();
        let completed = entries.iter().filter(|e| e.status == PlanEntryStatus::Completed).count();
        let in_progress = entries.iter().filter(|e| e.status == PlanEntryStatus::InProgress).count();
        let pending = entries.iter().filter(|e| e.status == PlanEntryStatus::Pending).count();
        let progress = if total > 0 {
            (completed as f32 / total as f32) * 100.0
        } else {
            0.0
        };
        Self { total, completed, in_progress, pending, progress }
    }
}

/// Events emitted by the ACP client for UI updates (legacy)
#[derive(Debug, Clone)]
pub enum AcpEvent {
    /// Agent status changed
    StatusChanged(AgentStatus),
    /// New message received
    MessageReceived(AgentMessage),
    /// Message chunk received (for streaming)
    MessageChunk { text: String },
    /// Tool use started
    ToolUseStarted { name: String, input: String },
    /// Tool use completed
    ToolUseCompleted { name: String, success: bool },
    /// Permission requested
    PermissionRequested(PermissionRequest),
    /// Session created
    SessionCreated { session_id: SessionId },
    /// Error occurred
    Error(String),
}

/// Notifications from the agent runtime to the UI
///
/// This is the new, cleaner notification type following Lapce's pattern.
#[derive(Debug, Clone)]
pub enum AgentNotification {
    /// Successfully connected to agent
    Connected { session_id: String },
    /// Disconnected from agent
    Disconnected,
    /// Agent status changed
    StatusChanged(AgentStatus),
    /// Available models from the agent
    ModelsAvailable {
        /// List of available models
        models: Vec<ModelInfo>,
        /// Currently selected model ID
        current_model_id: Option<String>,
    },
    /// Plan/todo list updated
    PlanUpdated {
        /// The todo entries
        entries: Vec<PlanEntry>,
    },
    /// Session info updated (title, etc.)
    SessionInfoUpdated {
        /// New title (if changed)
        title: Option<String>,
    },
    /// Text chunk received (for streaming display)
    TextChunk { text: String },
    /// Thinking/reasoning chunk received (muted display)
    ThinkingChunk { text: String },
    /// Complete message received (add to history)
    Message(AgentMessage),
    /// Tool call started
    ToolStarted {
        tool_id: String,
        name: String,
        input: Option<String>,
    },
    /// Tool call completed
    ToolCompleted {
        tool_id: String,
        name: String,
        success: bool,
        output: Option<String>,
    },
    /// Permission request from agent
    PermissionRequest(PermissionRequest),
    /// Turn/response completed (flush streaming buffer)
    TurnCompleted {
        stop_reason: Option<String>,
    },
    /// Error occurred
    Error { message: String },
}
