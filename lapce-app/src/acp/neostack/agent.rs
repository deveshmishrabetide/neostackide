//! Agent trait implementation for NeoStack
//!
//! Implements the ACP `Agent` trait for the embedded NeoStack agent.

use std::rc::Rc;

use agent_client_protocol::{
    Agent, AgentCapabilities, AuthenticateRequest, AuthenticateResponse, CancelNotification,
    Client, ContentBlock, ContentChunk, Error, ExtNotification, ExtRequest, ExtResponse,
    Implementation, InitializeRequest, InitializeResponse, LoadSessionRequest,
    LoadSessionResponse, McpServer, ModelId, ModelInfo, NewSessionRequest, NewSessionResponse,
    PromptRequest, PromptResponse, ProtocolVersion, ResumeSessionRequest, ResumeSessionResponse,
    Result, SessionId, SessionMode, SessionModeId, SessionModeState, SessionModelState,
    SessionNotification, SessionUpdate, SetSessionModeRequest, SetSessionModeResponse,
    SetSessionModelRequest, SetSessionModelResponse, StopReason, TextContent,
    ToolCall as AcpToolCall, ToolCallId, ToolCallUpdate, ToolCallUpdateFields, ToolCallStatus,
};
use serde_json::value::RawValue;
use uuid::Uuid;

use super::mcp_client::{McpClient, OpenAiTool};
use super::provider::{ChatMessage, StreamEvent, ToolCall};
use super::streaming::SseStream;
use super::{NeoStackAgent, Session};

/// Wrapper to implement Agent trait
///
/// We need this because Agent trait requires `?Send` and we use Rc internally
pub struct NeoStackAgentImpl {
    agent: Rc<NeoStackAgent>,
}

/// Maximum number of ReAct loop iterations
const MAX_ITERATIONS: usize = 10;

impl NeoStackAgentImpl {
    pub fn new(agent: Rc<NeoStackAgent>) -> Self {
        Self { agent }
    }

    /// Extract text content from prompt blocks
    fn extract_text(&self, prompt: &[ContentBlock]) -> String {
        prompt
            .iter()
            .filter_map(|block| match block {
                ContentBlock::Text(text) => Some(text.text.clone()),
                _ => None,
            })
            .collect::<Vec<_>>()
            .join("\n")
    }

    /// Get MCP URL from session's MCP servers
    fn get_mcp_url(&self, mcp_servers: &[McpServer]) -> Option<String> {
        for server in mcp_servers {
            if let McpServer::Http(http) = server {
                return Some(http.url.clone());
            }
        }
        None
    }

    /// Initialize MCP client and get available tools
    async fn get_mcp_tools(&self, mcp_url: &str) -> Vec<OpenAiTool> {
        let client = McpClient::new(mcp_url.to_string());

        // Initialize and list tools
        if let Err(e) = client.initialize().await {
            tracing::warn!("Failed to initialize MCP: {}", e);
            return vec![];
        }

        match client.list_tools().await {
            Ok(tools) => {
                tracing::info!("MCP tools available: {}", tools.len());
                tools.into_iter().map(|t| t.into()).collect()
            }
            Err(e) => {
                tracing::warn!("Failed to list MCP tools: {}", e);
                vec![]
            }
        }
    }

    /// Execute tool calls via MCP and return results
    async fn execute_tool_calls(
        &self,
        mcp_url: &str,
        tool_calls: &[ToolCall],
        session_id: &str,
        conn: &Rc<agent_client_protocol::AgentSideConnection>,
    ) -> Vec<ChatMessage> {
        let client = McpClient::new(mcp_url.to_string());
        let mut results = Vec::new();

        for tc in tool_calls {
            let tool_call_id = ToolCallId::new(tc.id.as_str());

            // Notify tool call started
            let _ = conn.session_notification(SessionNotification::new(
                SessionId::new(session_id.to_string()),
                SessionUpdate::ToolCall(
                    AcpToolCall::new(tool_call_id.clone(), &tc.function.name)
                        .raw_input(serde_json::from_str(&tc.function.arguments).ok())
                ),
            )).await;

            // Execute the tool
            let result_text = match tc.parse_arguments() {
                Ok(args) => {
                    match client.call_tool(&tc.function.name, args).await {
                        Ok(response) => {
                            let text = response.text();
                            let status = if response.is_error() {
                                ToolCallStatus::Failed
                            } else {
                                ToolCallStatus::Completed
                            };

                            // Notify tool completed
                            let _ = conn.session_notification(SessionNotification::new(
                                SessionId::new(session_id.to_string()),
                                SessionUpdate::ToolCallUpdate(
                                    ToolCallUpdate::new(
                                        tool_call_id.clone(),
                                        ToolCallUpdateFields::new().status(status)
                                    )
                                ),
                            )).await;

                            text
                        }
                        Err(e) => {
                            // Notify tool failed
                            let _ = conn.session_notification(SessionNotification::new(
                                SessionId::new(session_id.to_string()),
                                SessionUpdate::ToolCallUpdate(
                                    ToolCallUpdate::new(
                                        tool_call_id.clone(),
                                        ToolCallUpdateFields::new().status(ToolCallStatus::Failed)
                                    )
                                ),
                            )).await;

                            format!("Error executing tool: {}", e)
                        }
                    }
                }
                Err(e) => {
                    format!("Error parsing tool arguments: {}", e)
                }
            };

            results.push(ChatMessage::tool_result(&tc.id, result_text));
        }

        results
    }
}

#[async_trait::async_trait(?Send)]
impl Agent for NeoStackAgentImpl {
    async fn initialize(&self, _args: InitializeRequest) -> Result<InitializeResponse> {
        tracing::info!("NeoStack agent: initialize");

        Ok(InitializeResponse::new(ProtocolVersion::V1)
            .agent_capabilities(AgentCapabilities::default())
            .agent_info(
                Implementation::new("neostack", env!("CARGO_PKG_VERSION"))
                    .title("NeoStack Agent".to_string()),
            ))
    }

    async fn authenticate(&self, _args: AuthenticateRequest) -> Result<AuthenticateResponse> {
        // No authentication required for embedded agent
        Ok(AuthenticateResponse::default())
    }

    async fn new_session(&self, args: NewSessionRequest) -> Result<NewSessionResponse> {
        let session_id = Uuid::new_v4().to_string();
        tracing::info!("NeoStack agent: new_session -> {}", session_id);
        tracing::info!("NeoStack agent: MCP servers: {:?}", args.mcp_servers.len());

        let cwd = args.cwd.clone();
        let mcp_servers = args.mcp_servers.clone();
        let session = Session::new(&session_id, cwd, mcp_servers);
        self.agent.insert_session(session);

        Ok(NewSessionResponse::new(SessionId::new(session_id))
            .models(SessionModelState::new(
                ModelId::new("anthropic/claude-sonnet-4"),
                available_models(),
            ))
            .modes(SessionModeState::new(
                SessionModeId::new("default"),
                available_modes(),
            )))
    }

    async fn load_session(&self, args: LoadSessionRequest) -> Result<LoadSessionResponse> {
        let session_id = args.session_id.0.to_string();
        tracing::info!("NeoStack agent: load_session -> {}", session_id);

        // For now, just create a new session with the requested ID
        // TODO: Implement proper session persistence
        let cwd = args.cwd.clone();
        let mcp_servers = args.mcp_servers.clone();
        let session = Session::new(&session_id, cwd, mcp_servers);
        self.agent.insert_session(session);

        Ok(LoadSessionResponse::new()
            .models(SessionModelState::new(
                ModelId::new("anthropic/claude-sonnet-4"),
                available_models(),
            ))
            .modes(SessionModeState::new(
                SessionModeId::new("default"),
                available_modes(),
            )))
    }

    async fn resume_session(&self, args: ResumeSessionRequest) -> Result<ResumeSessionResponse> {
        let session_id = args.session_id.0.to_string();
        tracing::info!("NeoStack agent: resume_session -> {}", session_id);

        // For now, just create a new session with the requested ID
        // TODO: Implement proper session persistence (MCP servers should come from stored session)
        let cwd = args.cwd.clone();
        let session = Session::new(&session_id, cwd, vec![]);
        self.agent.insert_session(session);

        Ok(ResumeSessionResponse::new()
            .models(SessionModelState::new(
                ModelId::new("anthropic/claude-sonnet-4"),
                available_models(),
            ))
            .modes(SessionModeState::new(
                SessionModeId::new("default"),
                available_modes(),
            )))
    }

    async fn prompt(&self, args: PromptRequest) -> Result<PromptResponse> {
        let session_id = args.session_id.0.to_string();
        tracing::info!("NeoStack agent: prompt for session {}", session_id);

        // Check if session exists and get model
        let session = match self.agent.get_session(&session_id) {
            Some(s) => s,
            None => return Err(Error::invalid_params()),
        };

        // Extract text from prompt
        let user_text = self.extract_text(&args.prompt);
        if user_text.is_empty() {
            return Err(Error::invalid_params());
        }

        tracing::info!("NeoStack agent: user message: {}", user_text);

        // Add user message to history
        self.agent.add_message(
            &session_id,
            ChatMessage::user(user_text.clone()),
        );

        // Get connection for sending notifications
        let conn = match self.agent.connection() {
            Some(c) => c,
            None => {
                tracing::error!("NeoStack agent: no connection available");
                return Err(Error::internal_error());
            }
        };

        // Get MCP URL and tools if available
        let mcp_url = self.get_mcp_url(&session.mcp_servers);
        let tools = if let Some(ref url) = mcp_url {
            self.get_mcp_tools(url).await
        } else {
            vec![]
        };

        tracing::info!("NeoStack agent: {} tools available", tools.len());

        // ReAct loop - iterate until done or max iterations
        for iteration in 0..MAX_ITERATIONS {
            tracing::info!("NeoStack agent: ReAct iteration {}", iteration + 1);

            // Get all messages for context
            let messages = self.agent.get_messages(&session_id);

            // Check for cancellation
            if let Some(s) = self.agent.get_session(&session_id) {
                if s.is_cancelled() {
                    tracing::info!("NeoStack agent: cancelled");
                    return Ok(PromptResponse::new(StopReason::Cancelled));
                }
            }

            // Call the AI provider with tools
            let response = match self.agent.provider().chat_stream_with_tools(
                &session.model_id,
                messages,
                Some(system_prompt()),
                tools.clone(),
            ).await {
                Ok(r) => r,
                Err(e) => {
                    tracing::error!("NeoStack agent: provider error: {}", e);

                    // Send error as message chunk
                    let _ = conn.session_notification(SessionNotification::new(
                        SessionId::new(session_id.clone()),
                        SessionUpdate::AgentMessageChunk(ContentChunk::new(
                            ContentBlock::Text(TextContent::new(format!("Error: {}", e))),
                        )),
                    )).await;

                    return Ok(PromptResponse::new(StopReason::EndTurn));
                }
            };

            // Stream the response
            let mut stream = SseStream::new(response);
            let mut full_response = String::new();
            let mut received_tool_calls: Option<Vec<ToolCall>> = None;

            while let Some(event) = stream.next().await {
                match event {
                    StreamEvent::TextDelta(text) => {
                        full_response.push_str(&text);

                        // Send chunk to client
                        let _ = conn.session_notification(SessionNotification::new(
                            SessionId::new(session_id.clone()),
                            SessionUpdate::AgentMessageChunk(ContentChunk::new(
                                ContentBlock::Text(TextContent::new(text)),
                            )),
                        )).await;
                    }
                    StreamEvent::ToolCallDelta(_delta) => {
                        // Tool call deltas are accumulated by the stream parser
                    }
                    StreamEvent::Done => {
                        tracing::info!("NeoStack agent: stream done (no tool calls)");
                        break;
                    }
                    StreamEvent::DoneWithToolCalls(tool_calls) => {
                        tracing::info!("NeoStack agent: received {} tool calls", tool_calls.len());
                        received_tool_calls = Some(tool_calls);
                        break;
                    }
                    StreamEvent::Error(e) => {
                        tracing::error!("NeoStack agent: stream error: {}", e);
                        return Ok(PromptResponse::new(StopReason::EndTurn));
                    }
                }

                // Check for cancellation during streaming
                if let Some(s) = self.agent.get_session(&session_id) {
                    if s.is_cancelled() {
                        tracing::info!("NeoStack agent: cancelled during streaming");
                        return Ok(PromptResponse::new(StopReason::Cancelled));
                    }
                }
            }

            // Handle the result
            match received_tool_calls {
                Some(tool_calls) if !tool_calls.is_empty() => {
                    // Add assistant message with tool calls to history
                    self.agent.add_message(
                        &session_id,
                        ChatMessage::assistant_with_tool_calls(tool_calls.clone()),
                    );

                    // Execute tool calls via MCP
                    if let Some(ref url) = mcp_url {
                        let tool_results = self.execute_tool_calls(
                            url,
                            &tool_calls,
                            &session_id,
                            &conn,
                        ).await;

                        // Add tool results to history
                        for result in tool_results {
                            self.agent.add_message(&session_id, result);
                        }

                        // Continue the loop to get AI response to tool results
                        continue;
                    } else {
                        // No MCP URL, can't execute tools
                        tracing::warn!("NeoStack agent: received tool calls but no MCP URL available");
                        let _ = conn.session_notification(SessionNotification::new(
                            SessionId::new(session_id.clone()),
                            SessionUpdate::AgentMessageChunk(ContentChunk::new(
                                ContentBlock::Text(TextContent::new(
                                    "\n\n(Tool execution not available - no MCP server configured)".to_string()
                                )),
                            )),
                        )).await;
                        break;
                    }
                }
                _ => {
                    // No tool calls - add text response to history and finish
                    if !full_response.is_empty() {
                        self.agent.add_message(
                            &session_id,
                            ChatMessage::assistant(full_response),
                        );
                    }
                    break;
                }
            }
        }

        Ok(PromptResponse::new(StopReason::EndTurn))
    }

    async fn cancel(&self, args: CancelNotification) -> Result<()> {
        let session_id = args.session_id.0.to_string();
        tracing::info!("NeoStack agent: cancel session {}", session_id);

        self.agent.cancel_session(&session_id);
        Ok(())
    }

    async fn set_session_mode(
        &self,
        args: SetSessionModeRequest,
    ) -> Result<SetSessionModeResponse> {
        let session_id = args.session_id.0.to_string();
        let mode_id = args.mode_id.0.to_string();
        tracing::info!(
            "NeoStack agent: set_session_mode {} -> {}",
            session_id,
            mode_id
        );

        self.agent.set_session_mode(&session_id, &mode_id);

        Ok(SetSessionModeResponse::default())
    }

    async fn set_session_model(
        &self,
        args: SetSessionModelRequest,
    ) -> Result<SetSessionModelResponse> {
        let session_id = args.session_id.0.to_string();
        let model_id = args.model_id.0.to_string();
        tracing::info!(
            "NeoStack agent: set_session_model {} -> {}",
            session_id,
            model_id
        );

        self.agent.set_session_model(&session_id, &model_id);

        Ok(SetSessionModelResponse::new())
    }

    async fn ext_method(&self, _args: ExtRequest) -> Result<ExtResponse> {
        Ok(ExtResponse::new(RawValue::NULL.to_owned().into()))
    }

    async fn ext_notification(&self, _args: ExtNotification) -> Result<()> {
        Ok(())
    }
}

/// System prompt for the NeoStack agent
fn system_prompt() -> String {
    "You are NeoStack Agent, an AI coding assistant embedded in Lapce. \
    You help developers with coding tasks, answer questions about code, \
    and assist with software development. Be concise and helpful."
        .to_string()
}

/// Available AI models
fn available_models() -> Vec<ModelInfo> {
    vec![
        ModelInfo::new(ModelId::new("anthropic/claude-sonnet-4"), "Claude Sonnet 4")
            .description("Fast and intelligent".to_string()),
        ModelInfo::new(ModelId::new("anthropic/claude-opus-4"), "Claude Opus 4")
            .description("Most capable".to_string()),
        ModelInfo::new(
            ModelId::new("anthropic/claude-haiku-3.5"),
            "Claude Haiku 3.5",
        )
        .description("Fast and efficient".to_string()),
        ModelInfo::new(ModelId::new("openai/gpt-4o"), "GPT-4o")
            .description("OpenAI's flagship model".to_string()),
    ]
}

/// Available session modes
fn available_modes() -> Vec<SessionMode> {
    vec![
        SessionMode::new("default", "Default"),
        SessionMode::new("accept_edits", "Accept Edits"),
    ]
}
