//! ACP (Agent Client Protocol) integration for Lapce
//!
//! This module provides the client-side implementation of ACP,
//! allowing Lapce to communicate with AI coding agents like
//! Claude Code, Gemini CLI, Codex, etc.
//!
//! ## Architecture
//!
//! The ACP integration follows Lapce's proxy pattern:
//! - `AgentRpcHandler` - Sends commands to the agent runtime (like `ProxyRpcHandler`)
//! - `AgentRuntime` - Runs tokio in a background thread, handles async ACP operations
//! - `AgentNotification` - Events from agent to UI (via channel → reactive signal)
//! - `LapceAcpClient` - Implements the ACP `Client` trait for agent callbacks

mod client;
mod connection;
pub mod rpc;
pub mod runtime;
mod types;

pub use client::LapceAcpClient;
pub use connection::{AgentConnection, AgentManager};
pub use rpc::AgentRpcHandler;
pub use runtime::start_agent_runtime;
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
