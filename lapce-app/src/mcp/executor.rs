//! MCP Tool Executor
//!
//! Executes tools by forwarding to UE5 via the bridge.

use serde_json::{json, Value};
use super::tools::get_tool_by_name;
use super::types::ToolResult;
use crate::bridge::BridgeRpcHandler;

/// Execute a tool by name
pub async fn execute_tool(
    bridge: &BridgeRpcHandler,
    tool_name: &str,
    args: Value,
) -> Result<ToolResult, String> {
    // Verify tool exists
    let _tool = get_tool_by_name(tool_name)
        .ok_or_else(|| format!("Tool not found: {}", tool_name))?;

    tracing::debug!("[MCP] Executing tool: {} with args: {}", tool_name, args);

    // All our tools are bridge tools - forward to UE5
    execute_bridge_tool(bridge, tool_name, args).await
}

/// Execute a bridge (UE5) tool
async fn execute_bridge_tool(
    bridge: &BridgeRpcHandler,
    tool_name: &str,
    args: Value,
) -> Result<ToolResult, String> {
    // Check if UE5 is connected
    if !bridge.is_connected() {
        return Ok(ToolResult::error(
            "UE5 is not connected. Please connect Unreal Editor first."
        ));
    }

    // Map tool names to bridge commands
    // All asset tools use the execute_tool command
    let bridge_args = json!({
        "tool": tool_name,
        "args": args
    });

    // Send command to UE5
    match bridge.send_command(None, "execute_tool", Some(bridge_args)).await {
        Ok(event) => {
            if event.success {
                let result_data = event.data.unwrap_or(Value::Null);
                Ok(ToolResult::success(result_data))
            } else {
                Ok(ToolResult::error(
                    event.error.unwrap_or_else(|| "Unknown error".to_string())
                ))
            }
        }
        Err(e) => Ok(ToolResult::error(e)),
    }
}
