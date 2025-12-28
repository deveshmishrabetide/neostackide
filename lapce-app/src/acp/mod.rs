//! ACP (Agent Client Protocol) integration for Lapce
//!
//! This module provides the client-side implementation of ACP,
//! allowing Lapce to communicate with AI coding agents like
//! Claude Code, Gemini CLI, Codex, etc.

mod client;
mod connection;
mod types;

pub use client::LapceAcpClient;
pub use connection::{AgentConnection, AgentManager};
pub use types::*;

// Re-export key ACP types for convenience
pub use agent_client_protocol::{
    self as acp,
    Agent,
    Client,
    ClientSideConnection,
    InitializeRequest,
    NewSessionRequest,
    PromptRequest,
    CancelNotification,
    SessionNotification,
    SessionUpdate,
    ContentBlock,
    ContentChunk,
    TextContent,
    ToolCall,
    ToolCallUpdate,
    ReadTextFileRequest,
    ReadTextFileResponse,
    WriteTextFileRequest,
    WriteTextFileResponse,
    CreateTerminalRequest,
    CreateTerminalResponse,
    TerminalOutputRequest,
    TerminalOutputResponse,
    ReleaseTerminalRequest,
    ReleaseTerminalResponse,
    WaitForTerminalExitRequest,
    WaitForTerminalExitResponse,
    KillTerminalCommandRequest,
    KillTerminalCommandResponse,
    RequestPermissionRequest,
    RequestPermissionResponse,
    RequestPermissionOutcome,
    SelectedPermissionOutcome,
    PermissionOption,
    PermissionOptionId,
    PermissionOptionKind,
    ProtocolVersion,
    Implementation,
    ClientCapabilities,
    FileSystemCapability,
    TerminalId,
    TerminalExitStatus,
    Error as AcpError,
    Result as AcpResult,
};
