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
    /// Create a config for Claude Code
    pub fn claude_code() -> Self {
        Self {
            name: "Claude Code".to_string(),
            command: "claude".to_string(),
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
#[derive(Debug, Clone)]
pub struct AgentMessage {
    /// Who sent the message
    pub role: MessageRole,
    /// The message content
    pub content: MessageContent,
    /// Timestamp when the message was created
    pub timestamp: std::time::Instant,
}

/// Role of the message sender
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageRole {
    /// Message from the user
    User,
    /// Message from the agent
    Agent,
    /// System message (errors, status updates)
    System,
}

/// Content of a message
#[derive(Debug, Clone)]
pub enum MessageContent {
    /// Plain text
    Text(String),
    /// Code block with optional language
    Code { language: Option<String>, code: String },
    /// A file diff
    Diff { path: PathBuf, diff: String },
    /// Tool use notification
    ToolUse { name: String, status: ToolUseStatus },
    /// Error message
    Error(String),
    /// Thinking/processing indicator
    Thinking(String),
}

/// Status of a tool use
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolUseStatus {
    /// Tool is currently executing
    InProgress,
    /// Tool completed successfully
    Success,
    /// Tool failed
    Failed,
    /// User cancelled the tool
    Cancelled,
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

/// Events emitted by the ACP client for UI updates
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
