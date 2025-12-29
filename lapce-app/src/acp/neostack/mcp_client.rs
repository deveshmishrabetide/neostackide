//! MCP HTTP Client
//!
//! HTTP client for communicating with MCP servers.

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::sync::atomic::{AtomicI64, Ordering};

/// MCP HTTP Client
pub struct McpClient {
    client: reqwest::Client,
    base_url: String,
    request_id: AtomicI64,
}

impl McpClient {
    /// Create a new MCP client for the given URL
    pub fn new(base_url: String) -> Self {
        Self {
            client: reqwest::Client::new(),
            base_url,
            request_id: AtomicI64::new(1),
        }
    }

    /// Get the next request ID
    fn next_id(&self) -> i64 {
        self.request_id.fetch_add(1, Ordering::Relaxed)
    }

    /// Send a JSON-RPC request
    async fn request(&self, method: &str, params: Option<Value>) -> anyhow::Result<Value> {
        let id = self.next_id();
        let request = JsonRpcRequest {
            jsonrpc: "2.0".to_string(),
            id: Some(id),
            method: method.to_string(),
            params,
        };

        let response = self.client
            .post(&self.base_url)
            .json(&request)
            .send()
            .await?;

        if !response.status().is_success() {
            anyhow::bail!("MCP request failed: {}", response.status());
        }

        let rpc_response: JsonRpcResponse = response.json().await?;

        if let Some(error) = rpc_response.error {
            anyhow::bail!("MCP error {}: {}", error.code, error.message);
        }

        Ok(rpc_response.result.unwrap_or(Value::Null))
    }

    /// Initialize the MCP connection
    pub async fn initialize(&self) -> anyhow::Result<()> {
        let params = json!({
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {
                "name": "neostack-agent",
                "version": env!("CARGO_PKG_VERSION")
            }
        });

        self.request("initialize", Some(params)).await?;
        Ok(())
    }

    /// List available tools
    pub async fn list_tools(&self) -> anyhow::Result<Vec<McpToolInfo>> {
        let result = self.request("tools/list", None).await?;

        let tools: ToolsListResult = serde_json::from_value(result)?;
        Ok(tools.tools)
    }

    /// Call a tool
    pub async fn call_tool(&self, name: &str, arguments: Value) -> anyhow::Result<ToolCallResponse> {
        let params = json!({
            "name": name,
            "arguments": arguments
        });

        let result = self.request("tools/call", Some(params)).await?;
        let response: ToolCallResponse = serde_json::from_value(result)?;
        Ok(response)
    }
}

// =============================================================================
// JSON-RPC Types
// =============================================================================

#[derive(Debug, Serialize)]
struct JsonRpcRequest {
    jsonrpc: String,
    id: Option<i64>,
    method: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    params: Option<Value>,
}

#[derive(Debug, Deserialize)]
struct JsonRpcResponse {
    #[allow(dead_code)]
    jsonrpc: String,
    #[allow(dead_code)]
    id: Option<Value>,
    result: Option<Value>,
    error: Option<JsonRpcError>,
}

#[derive(Debug, Deserialize)]
struct JsonRpcError {
    code: i32,
    message: String,
}

// =============================================================================
// MCP Types
// =============================================================================

#[derive(Debug, Deserialize)]
struct ToolsListResult {
    tools: Vec<McpToolInfo>,
}

/// Information about an MCP tool
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct McpToolInfo {
    pub name: String,
    pub description: String,
    #[serde(rename = "inputSchema")]
    pub input_schema: Value,
}

/// Response from a tool call
#[derive(Debug, Clone, Deserialize)]
pub struct ToolCallResponse {
    pub content: Vec<ToolContent>,
    #[serde(rename = "isError")]
    pub is_error: Option<bool>,
}

/// Content item in a tool response
#[derive(Debug, Clone, Deserialize)]
pub struct ToolContent {
    #[serde(rename = "type")]
    pub content_type: String,
    pub text: String,
}

impl ToolCallResponse {
    /// Get the text content from the response
    pub fn text(&self) -> String {
        self.content
            .iter()
            .filter(|c| c.content_type == "text")
            .map(|c| c.text.clone())
            .collect::<Vec<_>>()
            .join("\n")
    }

    /// Check if this was an error response
    pub fn is_error(&self) -> bool {
        self.is_error.unwrap_or(false)
    }
}

// =============================================================================
// Convert MCP tools to OpenAI format
// =============================================================================

/// OpenAI-compatible tool definition
#[derive(Debug, Clone, Serialize)]
pub struct OpenAiTool {
    #[serde(rename = "type")]
    pub tool_type: String,
    pub function: OpenAiFunction,
}

/// OpenAI function definition
#[derive(Debug, Clone, Serialize)]
pub struct OpenAiFunction {
    pub name: String,
    pub description: String,
    pub parameters: Value,
}

impl From<McpToolInfo> for OpenAiTool {
    fn from(tool: McpToolInfo) -> Self {
        OpenAiTool {
            tool_type: "function".to_string(),
            function: OpenAiFunction {
                name: tool.name,
                description: tool.description,
                parameters: tool.input_schema,
            },
        }
    }
}
