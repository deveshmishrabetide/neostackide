//! SSE (Server-Sent Events) streaming parser
//!
//! Parses the streaming response from OpenAI-compatible chat completions API.

use super::provider::{StreamChunk, StreamEvent};

/// Parse a streaming response from the AI provider
pub struct SseStream {
    response: reqwest::Response,
    buffer: String,
}

impl SseStream {
    /// Create a new SSE stream from a reqwest response
    pub fn new(response: reqwest::Response) -> Self {
        Self {
            response,
            buffer: String::new(),
        }
    }

    /// Parse a single SSE line into an event
    fn parse_line(&self, line: &str) -> Option<StreamEvent> {
        let line = line.trim();

        // Skip empty lines and comments
        if line.is_empty() || line.starts_with(':') {
            return None;
        }

        // Handle "data: [DONE]" marker
        if line == "data: [DONE]" {
            return Some(StreamEvent::Done);
        }

        // Parse "data: {...}" lines
        if let Some(data) = line.strip_prefix("data: ") {
            match serde_json::from_str::<StreamChunk>(data) {
                Ok(chunk) => {
                    // Extract text content from the first choice
                    if let Some(choice) = chunk.choices.first() {
                        if choice.finish_reason.is_some() {
                            return Some(StreamEvent::Done);
                        }
                        if let Some(ref content) = choice.delta.content {
                            if !content.is_empty() {
                                return Some(StreamEvent::TextDelta(content.clone()));
                            }
                        }
                    }
                }
                Err(e) => {
                    tracing::warn!("Failed to parse SSE chunk: {} - data: {}", e, data);
                }
            }
        }

        None
    }

    /// Get the next event from the stream
    pub async fn next(&mut self) -> Option<StreamEvent> {
        loop {
            // First, try to parse any complete lines from the buffer
            if let Some(newline_pos) = self.buffer.find('\n') {
                let line = self.buffer[..newline_pos].to_string();
                self.buffer = self.buffer[newline_pos + 1..].to_string();

                if let Some(event) = self.parse_line(&line) {
                    return Some(event);
                }
                // Line didn't produce an event, continue processing buffer
                continue;
            }

            // Buffer doesn't have a complete line, try to read more data
            match self.response.chunk().await {
                Ok(Some(bytes)) => {
                    if let Ok(text) = std::str::from_utf8(&bytes) {
                        self.buffer.push_str(text);
                    }
                    // Continue loop to process the new data
                }
                Ok(None) => {
                    // Stream ended - process any remaining buffer
                    if !self.buffer.is_empty() {
                        let remaining = std::mem::take(&mut self.buffer);
                        for line in remaining.lines() {
                            if let Some(event) = self.parse_line(line) {
                                return Some(event);
                            }
                        }
                    }
                    return None;
                }
                Err(e) => {
                    tracing::error!("SSE stream error: {}", e);
                    return Some(StreamEvent::Error(e.to_string()));
                }
            }
        }
    }
}
