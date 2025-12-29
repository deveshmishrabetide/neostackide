//! NeoStack Proxy HTTP client
//!
//! Implements the OpenRouter-compatible chat completions API for AI inference.
//! Uses the authenticated user's JWT token from Lapce's auth system.

use serde::{Deserialize, Serialize};
use serde_json::Value;

use super::mcp_client::OpenAiTool;
use crate::auth::storage::TokenStorage;

/// NeoStack Proxy client for AI inference
///
/// Uses the NeoStack backend at api.neostack.dev which proxies to OpenRouter
/// with credit tracking and billing. Automatically uses the user's authenticated
/// JWT token from Lapce's auth system.
pub struct NeostackProvider {
    client: reqwest::Client,
    base_url: String,
}

impl NeostackProvider {
    /// Create a new provider with default settings
    ///
    /// Authentication is handled automatically via the user's logged-in session.
    /// The JWT token is read from Lapce's encrypted token storage.
    pub fn new() -> Self {
        let base_url = std::env::var("NEOSTACK_PROXY_URL")
            .unwrap_or_else(|_| "https://api.neostack.dev".to_string());

        Self {
            client: reqwest::Client::new(),
            base_url,
        }
    }

    /// Get the current auth token from storage
    fn get_auth_token(&self) -> Option<String> {
        // Try to load from encrypted storage
        if let Ok(storage) = TokenStorage::new() {
            if let Ok(Some(stored_auth)) = storage.load() {
                // Check if token is still valid
                if !stored_auth.tokens.is_access_expired() {
                    return Some(stored_auth.tokens.access_token);
                }
            }
        }

        // Fall back to environment variable for development/testing
        std::env::var("NEOSTACK_API_KEY")
            .or_else(|_| std::env::var("OPENROUTER_API_KEY"))
            .ok()
    }

    /// Create a streaming chat completion request
    pub async fn chat_stream(
        &self,
        model: &str,
        messages: Vec<ChatMessage>,
        system_prompt: Option<String>,
    ) -> anyhow::Result<reqwest::Response> {
        self.chat_stream_with_tools(model, messages, system_prompt, vec![]).await
    }

    /// Create a streaming chat completion request with tools
    pub async fn chat_stream_with_tools(
        &self,
        model: &str,
        messages: Vec<ChatMessage>,
        system_prompt: Option<String>,
        tools: Vec<OpenAiTool>,
    ) -> anyhow::Result<reqwest::Response> {
        let mut all_messages = Vec::new();

        // Add system prompt if provided
        if let Some(system) = system_prompt {
            all_messages.push(ChatMessage::system(system));
        }

        all_messages.extend(messages);

        let has_tools = !tools.is_empty();
        let request = ChatCompletionRequest {
            model: model.to_string(),
            messages: all_messages,
            stream: true,
            max_tokens: Some(4096),
            tools: if has_tools { Some(tools) } else { None },
            tool_choice: if has_tools { Some("auto".to_string()) } else { None },
        };

        let mut req = self.client
            .post(format!("{}/proxy/chat/completions", self.base_url))
            .header("Content-Type", "application/json");

        // Get auth token - JWT from user session or API key fallback
        if let Some(token) = self.get_auth_token() {
            if token.starts_with("nsk_") {
                // NeoStack API key (for development)
                req = req.header("X-API-Key", &token);
            } else {
                // JWT Bearer token (production - from authenticated session)
                req = req.header("Authorization", format!("Bearer {}", token));
            }
        } else {
            anyhow::bail!("Not authenticated. Please log in to use NeoStack Agent.");
        }

        let response = req
            .json(&request)
            .send()
            .await?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();

            // Provide helpful error messages
            let error_msg = match status.as_u16() {
                402 => "Insufficient credits. Please top up at neostack.dev".to_string(),
                401 | 403 => "Session expired. Please log in again.".to_string(),
                _ => format!("API request failed: {} - {}", status, body),
            };

            anyhow::bail!("{}", error_msg);
        }

        Ok(response)
    }
}

impl Default for NeostackProvider {
    fn default() -> Self {
        Self::new()
    }
}

/// A message in the chat conversation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChatMessage {
    pub role: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub content: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool_calls: Option<Vec<ToolCall>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool_call_id: Option<String>,
}

impl ChatMessage {
    pub fn new(role: impl Into<String>, content: impl Into<String>) -> Self {
        Self {
            role: role.into(),
            content: Some(content.into()),
            tool_calls: None,
            tool_call_id: None,
        }
    }

    pub fn system(content: impl Into<String>) -> Self {
        Self::new("system", content)
    }

    pub fn user(content: impl Into<String>) -> Self {
        Self::new("user", content)
    }

    pub fn assistant(content: impl Into<String>) -> Self {
        Self::new("assistant", content)
    }

    pub fn assistant_with_tool_calls(tool_calls: Vec<ToolCall>) -> Self {
        Self {
            role: "assistant".to_string(),
            content: None,
            tool_calls: Some(tool_calls),
            tool_call_id: None,
        }
    }

    pub fn tool_result(tool_call_id: impl Into<String>, content: impl Into<String>) -> Self {
        Self {
            role: "tool".to_string(),
            content: Some(content.into()),
            tool_calls: None,
            tool_call_id: Some(tool_call_id.into()),
        }
    }

    /// Get the text content (for backwards compatibility)
    pub fn text(&self) -> String {
        self.content.clone().unwrap_or_default()
    }
}

/// A tool call from the assistant
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ToolCall {
    pub id: String,
    #[serde(rename = "type")]
    pub call_type: String,
    pub function: FunctionCall,
}

/// Function call details
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FunctionCall {
    pub name: String,
    pub arguments: String,
}

impl ToolCall {
    /// Parse the arguments as JSON
    pub fn parse_arguments(&self) -> Result<Value, serde_json::Error> {
        serde_json::from_str(&self.function.arguments)
    }
}

/// Request body for chat completions
#[derive(Debug, Serialize)]
struct ChatCompletionRequest {
    model: String,
    messages: Vec<ChatMessage>,
    stream: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    max_tokens: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    tools: Option<Vec<OpenAiTool>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    tool_choice: Option<String>,
}

/// A single SSE event from the stream
#[derive(Debug, Clone)]
pub enum StreamEvent {
    /// Text content delta
    TextDelta(String),
    /// Tool call started or updated
    ToolCallDelta(ToolCallDelta),
    /// Stream finished
    Done,
    /// Stream finished with tool calls
    DoneWithToolCalls(Vec<ToolCall>),
    /// Error occurred
    Error(String),
}

/// Delta for a tool call being streamed
#[derive(Debug, Clone, Default)]
pub struct ToolCallDelta {
    pub index: usize,
    pub id: Option<String>,
    pub call_type: Option<String>,
    pub function_name: Option<String>,
    pub function_arguments: Option<String>,
}

/// Response chunk from streaming API
#[derive(Debug, Deserialize)]
pub struct StreamChunk {
    pub choices: Vec<StreamChoice>,
}

#[derive(Debug, Deserialize)]
pub struct StreamChoice {
    pub delta: StreamDelta,
    pub finish_reason: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct StreamDelta {
    pub content: Option<String>,
    pub role: Option<String>,
    pub tool_calls: Option<Vec<StreamToolCall>>,
}

/// Tool call in streaming response
#[derive(Debug, Clone, Deserialize)]
pub struct StreamToolCall {
    pub index: usize,
    pub id: Option<String>,
    #[serde(rename = "type")]
    pub call_type: Option<String>,
    pub function: Option<StreamFunctionCall>,
}

/// Function call in streaming response
#[derive(Debug, Clone, Deserialize)]
pub struct StreamFunctionCall {
    pub name: Option<String>,
    pub arguments: Option<String>,
}
