//! SSE (Server-Sent Events) streaming parser
//!
//! Parses the streaming response from OpenAI-compatible chat completions API.

use std::collections::HashMap;

use super::provider::{FunctionCall, StreamChunk, StreamEvent, ToolCall, ToolCallDelta};

/// Parse a streaming response from the AI provider
pub struct SseStream {
    response: reqwest::Response,
    buffer: String,
    /// Accumulated tool calls by index
    tool_calls: HashMap<usize, ToolCallBuilder>,
}

/// Builder for accumulating tool call chunks
#[derive(Debug, Default)]
struct ToolCallBuilder {
    id: String,
    call_type: String,
    function_name: String,
    function_arguments: String,
}

impl ToolCallBuilder {
    fn build(self) -> ToolCall {
        ToolCall {
            id: self.id,
            call_type: self.call_type,
            function: FunctionCall {
                name: self.function_name,
                arguments: self.function_arguments,
            },
        }
    }
}

impl SseStream {
    /// Create a new SSE stream from a reqwest response
    pub fn new(response: reqwest::Response) -> Self {
        Self {
            response,
            buffer: String::new(),
            tool_calls: HashMap::new(),
        }
    }

    /// Parse a single SSE line into an event
    fn parse_line(&mut self, line: &str) -> Option<StreamEvent> {
        let line = line.trim();

        // Skip empty lines and comments
        if line.is_empty() || line.starts_with(':') {
            return None;
        }

        // Handle "data: [DONE]" marker
        if line == "data: [DONE]" {
            return self.finish();
        }

        // Parse "data: {...}" lines
        if let Some(data) = line.strip_prefix("data: ") {
            match serde_json::from_str::<StreamChunk>(data) {
                Ok(chunk) => {
                    // Extract content from the first choice
                    if let Some(choice) = chunk.choices.first() {
                        // Check for finish reason
                        if let Some(ref reason) = choice.finish_reason {
                            if reason == "tool_calls" {
                                return self.finish();
                            }
                            return self.finish();
                        }

                        // Handle text content
                        if let Some(ref content) = choice.delta.content {
                            if !content.is_empty() {
                                return Some(StreamEvent::TextDelta(content.clone()));
                            }
                        }

                        // Handle tool calls
                        if let Some(ref tool_calls) = choice.delta.tool_calls {
                            for tc in tool_calls {
                                let builder = self.tool_calls.entry(tc.index).or_default();

                                // Update builder with new data
                                if let Some(ref id) = tc.id {
                                    builder.id = id.clone();
                                }
                                if let Some(ref call_type) = tc.call_type {
                                    builder.call_type = call_type.clone();
                                }
                                if let Some(ref func) = tc.function {
                                    if let Some(ref name) = func.name {
                                        builder.function_name = name.clone();
                                    }
                                    if let Some(ref args) = func.arguments {
                                        builder.function_arguments.push_str(args);
                                    }
                                }

                                // Emit delta event
                                return Some(StreamEvent::ToolCallDelta(ToolCallDelta {
                                    index: tc.index,
                                    id: tc.id.clone(),
                                    call_type: tc.call_type.clone(),
                                    function_name: tc.function.as_ref().and_then(|f| f.name.clone()),
                                    function_arguments: tc.function.as_ref().and_then(|f| f.arguments.clone()),
                                }));
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

    /// Finish the stream and return appropriate event
    fn finish(&mut self) -> Option<StreamEvent> {
        if self.tool_calls.is_empty() {
            Some(StreamEvent::Done)
        } else {
            // Build all accumulated tool calls
            let mut calls: Vec<_> = self.tool_calls
                .drain()
                .collect();
            calls.sort_by_key(|(idx, _)| *idx);
            let tool_calls: Vec<ToolCall> = calls
                .into_iter()
                .map(|(_, builder)| builder.build())
                .collect();
            Some(StreamEvent::DoneWithToolCalls(tool_calls))
        }
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
                    // Return any accumulated tool calls
                    return self.finish();
                }
                Err(e) => {
                    tracing::error!("SSE stream error: {}", e);
                    return Some(StreamEvent::Error(e.to_string()));
                }
            }
        }
    }
}
