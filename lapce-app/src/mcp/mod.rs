//! MCP (Model Context Protocol) Server
//!
//! Exposes editor tools via MCP for Claude Code and other AI agents.
//!
//! Tools exposed:
//! - create_asset: Create UE5 assets (Blueprint, Widget, etc.)
//! - read_asset: Read asset structure
//! - edit_blueprint: Edit Blueprint components/variables/functions
//! - find_node: Search for Blueprint nodes
//! - edit_graph: Edit Blueprint graphs

mod executor;
mod server;
mod tools;
mod types;

pub use server::{start_mcp_server, get_mcp_port};
pub use types::{ToolResult, McpNotification};
