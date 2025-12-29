//! NeoStack Proxy HTTP client
//!
//! Implements the OpenRouter-compatible chat completions API for AI inference.
//! Uses the authenticated user's JWT token from Lapce's auth system.

use serde::{Deserialize, Serialize};

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
        let mut all_messages = Vec::new();

        // Add system prompt if provided
        if let Some(system) = system_prompt {
            all_messages.push(ChatMessage {
                role: "system".to_string(),
                content: system,
            });
        }

        all_messages.extend(messages);

        let request = ChatCompletionRequest {
            model: model.to_string(),
            messages: all_messages,
            stream: true,
            max_tokens: Some(4096),
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
    pub content: String,
}

impl ChatMessage {
    pub fn new(role: impl Into<String>, content: impl Into<String>) -> Self {
        Self {
            role: role.into(),
            content: content.into(),
        }
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
}

/// A single SSE event from the stream
#[derive(Debug, Clone)]
pub enum StreamEvent {
    /// Text content delta
    TextDelta(String),
    /// Stream finished
    Done,
    /// Error occurred
    Error(String),
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
}
