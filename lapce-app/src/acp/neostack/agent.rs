//! Agent trait implementation for NeoStack
//!
//! Implements the ACP `Agent` trait for the embedded NeoStack agent.

use std::rc::Rc;

use agent_client_protocol::{
    Agent, AgentCapabilities, AuthenticateRequest, AuthenticateResponse, CancelNotification,
    Client, ContentBlock, ContentChunk, Error, ExtNotification, ExtRequest, ExtResponse,
    Implementation, InitializeRequest, InitializeResponse, LoadSessionRequest,
    LoadSessionResponse, ModelId, ModelInfo, NewSessionRequest, NewSessionResponse, PromptRequest,
    PromptResponse, ProtocolVersion, ResumeSessionRequest, ResumeSessionResponse, Result,
    SessionId, SessionMode, SessionModeId, SessionModeState, SessionModelState,
    SessionNotification, SessionUpdate, SetSessionModeRequest, SetSessionModeResponse,
    SetSessionModelRequest, SetSessionModelResponse, StopReason, TextContent,
};
use serde_json::value::RawValue;
use uuid::Uuid;

use super::provider::{ChatMessage, StreamEvent};
use super::streaming::SseStream;
use super::{NeoStackAgent, Session};

/// Wrapper to implement Agent trait
///
/// We need this because Agent trait requires `?Send` and we use Rc internally
pub struct NeoStackAgentImpl {
    agent: Rc<NeoStackAgent>,
}

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

        let cwd = args.cwd.clone();
        let session = Session::new(&session_id, cwd);
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
        let session = Session::new(&session_id, cwd);
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
        // TODO: Implement proper session persistence
        let cwd = args.cwd.clone();
        let session = Session::new(&session_id, cwd);
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
            ChatMessage {
                role: "user".to_string(),
                content: user_text.clone(),
            },
        );

        // Get all messages for context
        let messages = self.agent.get_messages(&session_id);

        // Get connection for sending notifications
        let conn = match self.agent.connection() {
            Some(c) => c,
            None => {
                tracing::error!("NeoStack agent: no connection available");
                return Err(Error::internal_error());
            }
        };

        // Call the AI provider
        let response = match self.agent.provider().chat_stream(
            &session.model_id,
            messages,
            Some(system_prompt()),
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
                StreamEvent::Done => {
                    tracing::info!("NeoStack agent: stream done");
                    break;
                }
                StreamEvent::Error(e) => {
                    tracing::error!("NeoStack agent: stream error: {}", e);
                    break;
                }
            }

            // Check for cancellation
            if let Some(s) = self.agent.get_session(&session_id) {
                if s.is_cancelled() {
                    tracing::info!("NeoStack agent: cancelled");
                    return Ok(PromptResponse::new(StopReason::Cancelled));
                }
            }
        }

        // Add assistant response to history
        if !full_response.is_empty() {
            self.agent.add_message(
                &session_id,
                ChatMessage {
                    role: "assistant".to_string(),
                    content: full_response,
                },
            );
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
