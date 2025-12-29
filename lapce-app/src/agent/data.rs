//! Agent data structures and state management

use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};

use chrono::{DateTime, Utc};
use floem::ext_event::create_signal_from_channel;
use floem::reactive::{ReadSignal, RwSignal, Scope, SignalGet, SignalUpdate, SignalWith};
use im::{HashMap, Vector};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use std::collections::HashSet;

use crate::acp::{
    AgentConfig, AgentMessage, AgentNotification, AgentRpcHandler, AgentStatus,
    MessagePart, MessageRole, PermissionRequest, ToolCallState,
    start_agent_runtime,
};
use crate::editor::EditorData;
use crate::main_split::Editors;
use crate::window_tab::CommonData;

/// The AI provider for agent interactions
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub enum AgentProvider {
    #[default]
    Claude,
    Codex,
    Gemini,
    NeoStack,
}

impl AgentProvider {
    /// Get all available providers
    pub fn all() -> &'static [AgentProvider] {
        &[
            AgentProvider::Claude,
            AgentProvider::Codex,
            AgentProvider::Gemini,
            AgentProvider::NeoStack,
        ]
    }

    pub fn display_name(&self) -> &'static str {
        match self {
            AgentProvider::Claude => "Claude",
            AgentProvider::Codex => "Codex",
            AgentProvider::Gemini => "Gemini",
            AgentProvider::NeoStack => "NeoStack",
        }
    }
}

/// Status of a chat session
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub enum ChatStatus {
    #[default]
    Idle,
    Streaming,
    Completed,
    Error,
}

/// A single chat session
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Chat {
    pub id: String,
    pub title: String,
    pub provider: AgentProvider,
    pub status: ChatStatus,
    #[serde(with = "chrono::serde::ts_seconds")]
    pub created_at: DateTime<Utc>,
    pub unread: bool,
    /// Total cost in USD for this chat
    pub cost_usd: f64,
    /// Total token count
    pub token_count: u64,
    /// Agent session ID for session resumption (stored by the agent itself)
    /// This allows resuming chats with agents like Claude Code that maintain their own history
    #[serde(default)]
    pub agent_session_id: Option<String>,
}

impl Chat {
    pub fn new(title: impl Into<String>, provider: AgentProvider) -> Self {
        Self {
            id: Uuid::new_v4().to_string(),
            title: title.into(),
            provider,
            status: ChatStatus::Idle,
            created_at: Utc::now(),
            unread: false,
            cost_usd: 0.0,
            token_count: 0,
            agent_session_id: None,
        }
    }
}

/// Agent mode state
#[derive(Clone)]
pub struct AgentData {
    /// Scope for creating signals
    pub scope: Scope,

    /// Whether the left sidebar is open
    pub left_sidebar_open: RwSignal<bool>,

    /// Whether the right sidebar is open
    pub right_sidebar_open: RwSignal<bool>,

    /// All chat sessions
    pub chats: RwSignal<Vector<Chat>>,

    /// Currently selected chat ID
    pub current_chat_id: RwSignal<Option<String>>,

    /// Currently selected provider
    pub provider: RwSignal<AgentProvider>,

    /// Messages per chat (chat_id -> messages)
    pub messages: RwSignal<HashMap<String, Vector<AgentMessage>>>,

    /// Current streaming text buffer per chat
    pub streaming_text: RwSignal<HashMap<String, String>>,

    /// Current streaming thinking/reasoning buffer per chat
    pub streaming_thinking: RwSignal<HashMap<String, String>>,

    /// Whether currently streaming per chat
    pub is_streaming: RwSignal<HashMap<String, bool>>,

    /// Pending permission requests per chat
    pub pending_permissions: RwSignal<HashMap<String, Vector<PermissionRequest>>>,

    /// Agent connection status
    pub agent_status: RwSignal<AgentStatus>,

    /// Current session ID (from agent connection)
    pub session_id: RwSignal<Option<String>>,

    /// Available models from the connected agent
    pub available_models: RwSignal<Vec<crate::acp::ModelInfo>>,

    /// Currently selected model ID
    pub current_model_id: RwSignal<Option<String>>,

    /// Current plan/todo entries from the agent
    pub plan_entries: RwSignal<Vec<crate::acp::PlanEntry>>,

    /// Whether the todo panel is expanded
    pub todo_panel_expanded: RwSignal<bool>,

    /// RPC handler for communicating with agent runtime
    pub rpc: Arc<AgentRpcHandler>,

    /// Notification signal from agent runtime
    pub notification: ReadSignal<Option<AgentNotification>>,

    /// Editor for the input field
    pub input_editor: EditorData,

    /// Editors collection (needed for cleanup)
    pub editors: Editors,

    /// Common data
    pub common: Rc<CommonData>,

    /// Error message to display
    pub error_message: RwSignal<Option<String>>,

    /// Pending prompt to send when connection is established
    pub pending_prompt: RwSignal<Option<String>>,

    /// Counter for generating unique message IDs
    message_id_counter: Arc<AtomicU64>,

    /// Set of expanded tool call IDs (for collapsible tool UI)
    pub expanded_tools: RwSignal<HashSet<String>>,

    /// Workspace path for persistence
    workspace_path: PathBuf,
}

/// Persisted chat data (stored in .neostack/chats.json)
#[derive(Debug, Clone, Serialize, Deserialize)]
struct PersistedData {
    chats: Vec<Chat>,
    messages: std::collections::HashMap<String, Vec<AgentMessage>>,
}

impl AgentData {
    /// Get the persistence file path
    fn persistence_path(workspace_path: &PathBuf) -> PathBuf {
        workspace_path.join(".neostack").join("chats.json")
    }

    /// Load persisted chat data from disk
    fn load_persisted(workspace_path: &PathBuf) -> Option<PersistedData> {
        let path = Self::persistence_path(workspace_path);
        if path.exists() {
            match std::fs::read_to_string(&path) {
                Ok(content) => {
                    match serde_json::from_str(&content) {
                        Ok(data) => {
                            tracing::info!("Loaded chat history from {:?}", path);
                            return Some(data);
                        }
                        Err(e) => {
                            tracing::warn!("Failed to parse chat history: {}", e);
                        }
                    }
                }
                Err(e) => {
                    tracing::warn!("Failed to read chat history: {}", e);
                }
            }
        }
        None
    }

    /// Save chat data to disk
    fn save_persisted(&self) {
        let path = Self::persistence_path(&self.workspace_path);

        // Ensure .neostack directory exists
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }

        let data = PersistedData {
            chats: self.chats.with_untracked(|c| c.iter().cloned().collect()),
            messages: self.messages.with_untracked(|m| {
                m.iter()
                    .map(|(k, v)| (k.clone(), v.iter().cloned().collect()))
                    .collect()
            }),
        };

        match serde_json::to_string_pretty(&data) {
            Ok(json) => {
                if let Err(e) = std::fs::write(&path, json) {
                    tracing::warn!("Failed to save chat history: {}", e);
                } else {
                    tracing::debug!("Saved chat history to {:?}", path);
                }
            }
            Err(e) => {
                tracing::warn!("Failed to serialize chat history: {}", e);
            }
        }
    }

    pub fn new(cx: Scope, editors: Editors, common: Rc<CommonData>, workspace_path: PathBuf) -> Self {
        // Try to load persisted chat data
        let persisted = Self::load_persisted(&workspace_path);

        let (chats, messages, max_message_id) = if let Some(data) = persisted {
            // Find the max message ID to continue from
            let max_id = data.messages.values()
                .flat_map(|msgs| msgs.iter().map(|m| m.id))
                .max()
                .unwrap_or(0);

            // Convert to im::Vector and im::HashMap
            let chats = Vector::from(data.chats);
            let messages: HashMap<String, Vector<AgentMessage>> = data.messages
                .into_iter()
                .map(|(k, v)| (k, Vector::from(v)))
                .collect();

            (chats, messages, max_id)
        } else {
            (Vector::new(), HashMap::new(), 0)
        };

        // Create the RPC handler and notification channels
        // We use crossbeam for the runtime (thread-safe), and mpsc for Floem signal
        let rpc = Arc::new(AgentRpcHandler::new());
        let (runtime_tx, runtime_rx) = crossbeam_channel::unbounded();
        let (ui_tx, ui_rx) = std::sync::mpsc::channel();

        // Bridge the crossbeam channel to mpsc for Floem
        std::thread::spawn({
            move || {
                for notification in runtime_rx {
                    if ui_tx.send(notification).is_err() {
                        break;
                    }
                }
            }
        });

        // Start the agent runtime in a background thread
        start_agent_runtime(
            (*rpc).clone(),
            runtime_tx,
            workspace_path.clone(),
        );

        // Create a signal from the mpsc channel (Floem requires mpsc)
        let notification = create_signal_from_channel(ui_rx);

        // Create the input editor
        let input_editor = editors.make_local(cx, common.clone());

        Self {
            scope: cx,
            left_sidebar_open: cx.create_rw_signal(true),
            right_sidebar_open: cx.create_rw_signal(true),
            chats: cx.create_rw_signal(chats),
            current_chat_id: cx.create_rw_signal(None),
            provider: cx.create_rw_signal(AgentProvider::default()),
            messages: cx.create_rw_signal(messages),
            streaming_text: cx.create_rw_signal(HashMap::new()),
            streaming_thinking: cx.create_rw_signal(HashMap::new()),
            is_streaming: cx.create_rw_signal(HashMap::new()),
            pending_permissions: cx.create_rw_signal(HashMap::new()),
            agent_status: cx.create_rw_signal(AgentStatus::Disconnected),
            session_id: cx.create_rw_signal(None),
            available_models: cx.create_rw_signal(vec![]),
            current_model_id: cx.create_rw_signal(None),
            plan_entries: cx.create_rw_signal(vec![]),
            todo_panel_expanded: cx.create_rw_signal(false),
            rpc,
            notification,
            input_editor,
            editors,
            common,
            error_message: cx.create_rw_signal(None),
            pending_prompt: cx.create_rw_signal(None),
            message_id_counter: Arc::new(AtomicU64::new(max_message_id + 1)),
            expanded_tools: cx.create_rw_signal(HashSet::new()),
            workspace_path,
        }
    }

    /// Get the current input text from the editor
    pub fn get_input_text(&self) -> String {
        self.input_editor.doc().buffer.with_untracked(|b| b.to_string())
    }

    /// Clear the input editor
    pub fn clear_input(&self) {
        // Reset cursor to beginning before clearing to avoid invalid cursor state
        self.input_editor.cursor().set(lapce_core::cursor::Cursor::new(
            lapce_core::cursor::CursorMode::Insert(lapce_core::selection::Selection::caret(0)),
            None,
            None,
        ));
        self.input_editor.doc().reload(lapce_xi_rope::Rope::from(""), true);
    }

    /// Generate a unique message ID
    fn next_message_id(&self) -> u64 {
        self.message_id_counter.fetch_add(1, Ordering::Relaxed)
    }

    /// Check if a tool call is expanded
    pub fn is_tool_expanded(&self, tool_call_id: &str) -> bool {
        self.expanded_tools.with(|set| set.contains(tool_call_id))
    }

    /// Toggle the expanded state of a tool call
    pub fn toggle_tool_expanded(&self, tool_call_id: &str) {
        tracing::info!("toggle_tool_expanded: {}", tool_call_id);
        self.expanded_tools.update(|set| {
            if set.contains(tool_call_id) {
                tracing::info!("  -> collapsing");
                set.remove(tool_call_id);
            } else {
                tracing::info!("  -> expanding");
                set.insert(tool_call_id.to_string());
            }
        });
    }

    /// Connect to an agent with the given provider
    /// If the current chat has an agent_session_id, attempt to resume that session
    pub fn connect(&self, provider: AgentProvider) {
        let config = match provider {
            AgentProvider::Claude => AgentConfig::claude_code(),
            AgentProvider::Codex => AgentConfig::codex(),
            AgentProvider::Gemini => AgentConfig::gemini_cli(),
            AgentProvider::NeoStack => AgentConfig::neostack(),
        };

        // Check if current chat has an agent_session_id to resume
        let agent_session_id = self.current_chat_id.get_untracked().and_then(|chat_id| {
            self.chats.with_untracked(|chats| {
                chats.iter()
                    .find(|c| c.id == chat_id)
                    .and_then(|c| c.agent_session_id.clone())
            })
        });

        self.agent_status.set(AgentStatus::Connecting);

        if let Some(session_id) = agent_session_id {
            tracing::info!("Resuming agent session: {:?} (command: {}, session: {})", provider, config.command, session_id);
            self.rpc.resume_session(config, self.workspace_path.clone(), session_id);
        } else {
            tracing::info!("Connecting to agent: {:?} (command: {})", provider, config.command);
            self.rpc.connect(config, self.workspace_path.clone());
        }
    }

    /// Send a prompt to the agent
    pub fn send_prompt(&self, prompt: String) {
        // Auto-create a new chat if none exists
        if self.current_chat_id.get_untracked().is_none() {
            self.new_chat();
        }

        if let Some(session_id) = self.session_id.get_untracked() {
            // Connected - send immediately
            self.do_send_prompt(session_id, prompt);
        } else {
            // Check if we're currently connecting
            let status = self.agent_status.get_untracked();
            match status {
                AgentStatus::Connecting => {
                    // Queue the prompt to send when connection completes
                    self.pending_prompt.set(Some(prompt));
                }
                AgentStatus::Disconnected | AgentStatus::Error => {
                    // Not connected and not connecting - try to connect first
                    self.pending_prompt.set(Some(prompt));
                    self.ensure_connected();
                }
                _ => {
                    self.set_error(Some("Not connected to agent".to_string()));
                }
            }
        }
    }

    /// Internal: actually send the prompt (when we have a session_id)
    fn do_send_prompt(&self, session_id: String, prompt: String) {
        tracing::info!("do_send_prompt called with session_id: {}", session_id);

        // Mark current chat as streaming
        if let Some(chat_id) = self.current_chat_id.get_untracked() {
            tracing::info!("Current chat_id: {}", chat_id);
            self.set_streaming(&chat_id, true);

            // Check if this is the first message - update chat title
            let is_first_message = self.messages.with(|msgs| {
                msgs.get(&chat_id).map(|m| m.is_empty()).unwrap_or(true)
            });

            if is_first_message {
                // Use first ~50 chars of the prompt as the chat title
                let title = if prompt.len() > 50 {
                    format!("{}...", &prompt[..47])
                } else {
                    prompt.clone()
                };
                self.update_chat_title(&chat_id, &title);
            }

            // Add user message
            tracing::info!("Adding user message to chat: {}", chat_id);
            self.add_message(&chat_id, AgentMessage {
                id: self.next_message_id(),
                role: MessageRole::User,
                parts: vec![MessagePart::Text(prompt.clone())],
                timestamp: std::time::Instant::now(),
            });

            // Log the message count after adding
            let count = self.messages.with(|msgs| {
                msgs.get(&chat_id).map(|m| m.len()).unwrap_or(0)
            });
            tracing::info!("Messages in chat after adding: {}", count);
        } else {
            tracing::warn!("No current chat_id when trying to send prompt");
        }

        self.agent_status.set(AgentStatus::Processing);
        self.rpc.prompt_async(session_id, prompt, |_result| {
            // Response will come through notification channel
        });
    }

    /// Cancel the current operation
    pub fn cancel(&self) {
        if let Some(session_id) = self.session_id.get_untracked() {
            self.rpc.cancel(session_id);
        }
    }

    /// Set the current model for the agent
    pub fn set_model(&self, model_id: &str) {
        if let Some(session_id) = self.session_id.get_untracked() {
            self.current_model_id.set(Some(model_id.to_string()));
            self.rpc.set_model(session_id, model_id.to_string());
        }
    }

    /// Create a new chat and make it the current one
    pub fn new_chat(&self) {
        let provider = self.provider.get_untracked();
        let chat = Chat::new("New chat", provider);
        let chat_id = chat.id.clone();

        self.chats.update(|chats| {
            chats.push_front(chat);
        });
        self.current_chat_id.set(Some(chat_id));

        // Persist changes
        self.save_persisted();

        // Auto-connect if not already connected
        self.ensure_connected();
    }

    /// Select a chat by ID
    pub fn select_chat(&self, chat_id: &str) {
        self.current_chat_id.set(Some(chat_id.to_string()));

        // Auto-connect if not already connected
        self.ensure_connected();
    }

    /// Ensure we are connected to an agent, connecting if necessary
    fn ensure_connected(&self) {
        let status = self.agent_status.get_untracked();
        match status {
            AgentStatus::Disconnected | AgentStatus::Error => {
                // Need to connect
                let provider = self.provider.get_untracked();
                self.connect(provider);
            }
            AgentStatus::Connecting | AgentStatus::Connected | AgentStatus::Processing => {
                // Already connected or connecting, nothing to do
            }
        }
    }

    /// Get the current chat
    pub fn current_chat(&self) -> Option<Chat> {
        let current_id = self.current_chat_id.get();
        current_id.and_then(|id| {
            self.chats.with(|chats: &Vector<Chat>| {
                chats.iter().find(|c| c.id == id).cloned()
            })
        })
    }

    /// Get messages for the current chat
    pub fn current_messages(&self) -> Vector<AgentMessage> {
        let current_id = self.current_chat_id.get();
        let messages = current_id.clone()
            .and_then(|id| {
                self.messages.with(|msgs| msgs.get(&id).cloned())
            })
            .unwrap_or_default();
        // Log message details
        for msg in messages.iter() {
            let part_types: Vec<&str> = msg.parts.iter().map(|p| match p {
                MessagePart::Text(_) => "Text",
                MessagePart::ToolCall { .. } => "ToolCall",
                MessagePart::Reasoning(_) => "Reasoning",
                MessagePart::Error(_) => "Error",
            }).collect();
            tracing::debug!("  msg[{}] role={:?} parts={:?}", msg.id, msg.role, part_types);
        }
        tracing::debug!("current_messages() returning {} messages for chat {:?}", messages.len(), current_id);
        messages
    }

    /// Check if current chat is streaming
    pub fn is_current_streaming(&self) -> bool {
        let current_id = self.current_chat_id.get();
        current_id
            .map(|id| {
                self.is_streaming.with(|s| s.get(&id).copied().unwrap_or(false))
            })
            .unwrap_or(false)
    }

    /// Get streaming text for current chat
    pub fn current_streaming_text(&self) -> String {
        let current_id = self.current_chat_id.get();
        current_id
            .and_then(|id| {
                self.streaming_text.with(|s| s.get(&id).cloned())
            })
            .unwrap_or_default()
    }

    /// Get pending permissions for current chat
    pub fn current_pending_permissions(&self) -> Vector<PermissionRequest> {
        let current_id = self.current_chat_id.get();
        current_id
            .and_then(|id| {
                self.pending_permissions.with(|p| p.get(&id).cloned())
            })
            .unwrap_or_default()
    }

    /// Add a message to a chat
    pub fn add_message(&self, chat_id: &str, message: AgentMessage) {
        self.messages.update(|msgs| {
            let chat_messages = msgs.entry(chat_id.to_string()).or_insert_with(Vector::new);
            chat_messages.push_back(message);
        });
        // Persist changes
        self.save_persisted();
    }

    /// Update streaming state for a chat
    pub fn set_streaming(&self, chat_id: &str, streaming: bool) {
        self.is_streaming.update(|s| {
            s.insert(chat_id.to_string(), streaming);
        });

        // Update chat status
        self.chats.update(|chats| {
            if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                chat.status = if streaming { ChatStatus::Streaming } else { ChatStatus::Idle };
            }
        });
    }

    /// Append text to streaming buffer
    pub fn append_streaming_text(&self, chat_id: &str, text: &str) {
        self.streaming_text.update(|s| {
            let entry = s.entry(chat_id.to_string()).or_insert_with(String::new);
            entry.push_str(text);
        });
    }

    /// Flush streaming text to the current agent message as a text part
    /// If there's an ongoing agent message, appends to it; otherwise creates a new one
    pub fn flush_streaming_text(&self, chat_id: &str) {
        let text = self.streaming_text.with_untracked(|s| s.get(chat_id).cloned());
        tracing::info!("flush_streaming_text for chat {}: text len = {:?}", chat_id, text.as_ref().map(|t| t.len()));
        if let Some(text) = text {
            if !text.is_empty() {
                tracing::info!("flush_streaming_text: appending {} chars", text.len());
                self.append_text_part_to_current_message(chat_id, &text);
                // Persist changes after flushing
                self.save_persisted();
            }
        }
        self.streaming_text.update(|s| {
            s.remove(chat_id);
        });
    }

    /// Get streaming thinking text for current chat
    pub fn current_streaming_thinking(&self) -> String {
        let current_id = self.current_chat_id.get();
        current_id
            .and_then(|id| {
                self.streaming_thinking.with(|s| s.get(&id).cloned())
            })
            .unwrap_or_default()
    }

    /// Append text to streaming thinking buffer
    pub fn append_streaming_thinking(&self, chat_id: &str, text: &str) {
        self.streaming_thinking.update(|s| {
            let entry = s.entry(chat_id.to_string()).or_insert_with(String::new);
            entry.push_str(text);
        });
    }

    /// Flush streaming thinking to the current agent message as a Reasoning part
    pub fn flush_streaming_thinking(&self, chat_id: &str) {
        let text = self.streaming_thinking.with_untracked(|s| s.get(chat_id).cloned());
        if let Some(text) = text {
            if !text.is_empty() {
                tracing::info!("flush_streaming_thinking: appending {} chars as Reasoning", text.len());
                self.append_reasoning_part_to_current_message(chat_id, &text);
            }
        }
        self.streaming_thinking.update(|s| {
            s.remove(chat_id);
        });
    }

    /// Append a Reasoning part to the current agent message, or create a new message
    fn append_reasoning_part_to_current_message(&self, chat_id: &str, text: &str) {
        self.messages.update(|msgs| {
            let chat_messages = msgs.entry(chat_id.to_string()).or_insert_with(Vector::new);

            // Check if the last message is from the agent and we can append to it
            let should_append = chat_messages.last()
                .map(|m| m.role == MessageRole::Agent)
                .unwrap_or(false);

            if should_append {
                // Append Reasoning part to existing agent message
                if let Some(last_msg) = chat_messages.back_mut() {
                    last_msg.parts.push(MessagePart::Reasoning(text.to_string()));
                }
            } else {
                // Create a new agent message with the Reasoning part
                let new_message = AgentMessage {
                    id: self.next_message_id(),
                    role: MessageRole::Agent,
                    parts: vec![MessagePart::Reasoning(text.to_string())],
                    timestamp: std::time::Instant::now(),
                };
                chat_messages.push_back(new_message);
            }
        });
    }

    /// Append a text part to the current agent message, or create a new message
    fn append_text_part_to_current_message(&self, chat_id: &str, text: &str) {
        self.messages.update(|msgs| {
            let chat_messages = msgs.entry(chat_id.to_string()).or_insert_with(Vector::new);

            // Check if we have an existing agent message to append to
            // im::Vector requires pop + modify + push for mutable access to last element
            if let Some(last_msg) = chat_messages.last() {
                if last_msg.role == MessageRole::Agent {
                    // Pop, modify, push back
                    let mut last_msg = chat_messages.pop_back().unwrap();
                    last_msg.parts.push(MessagePart::Text(text.to_string()));
                    chat_messages.push_back(last_msg);
                    return;
                }
            }

            // Create a new agent message
            chat_messages.push_back(AgentMessage {
                id: self.next_message_id(),
                role: MessageRole::Agent,
                parts: vec![MessagePart::Text(text.to_string())],
                timestamp: std::time::Instant::now(),
            });
        });
    }

    /// Add a tool call part to the current agent message
    /// If a tool with the same ID already exists, update it instead of adding a duplicate
    fn add_tool_call_part(&self, chat_id: &str, tool_id: String, name: String, args: Option<String>) {
        tracing::info!("add_tool_call_part: {} - {}", name, tool_id);
        self.messages.update(|msgs| {
            let chat_messages = msgs.entry(chat_id.to_string()).or_insert_with(Vector::new);

            // First, check if this tool_id already exists in any agent message
            // If so, update it instead of adding a duplicate
            let len = chat_messages.len();
            for i in (0..len).rev() {
                if let Some(msg) = chat_messages.get(i) {
                    if msg.role == MessageRole::Agent {
                        let has_tool = msg.parts.iter().any(|part| {
                            matches!(part, MessagePart::ToolCall { tool_call_id, .. } if tool_call_id == &tool_id)
                        });

                        if has_tool {
                            tracing::info!("add_tool_call_part: updating existing tool call");
                            // Update the existing tool call with new name/args
                            let mut msg = chat_messages.remove(i);
                            for part in msg.parts.iter_mut() {
                                if let &mut MessagePart::ToolCall {
                                    tool_call_id: ref existing_id,
                                    name: ref mut n,
                                    args: ref mut a,
                                    ..
                                } = part {
                                    if existing_id == &tool_id {
                                        *n = name.clone();
                                        if args.is_some() {
                                            *a = args.clone();
                                        }
                                    }
                                }
                            }
                            chat_messages.insert(i, msg);
                            return;
                        }
                    }
                }
            }

            // No existing tool call found, add a new one
            if let Some(last_msg) = chat_messages.last() {
                if last_msg.role == MessageRole::Agent {
                    tracing::info!("add_tool_call_part: appending new tool to existing agent message with {} parts", last_msg.parts.len());
                    // Pop, modify, push back
                    let mut last_msg = chat_messages.pop_back().unwrap();
                    last_msg.parts.push(MessagePart::ToolCall {
                        tool_call_id: tool_id,
                        name,
                        args,
                        state: ToolCallState::Running,
                        output: None,
                        is_error: false,
                    });
                    chat_messages.push_back(last_msg);
                    return;
                }
            }

            tracing::info!("add_tool_call_part: creating new agent message");
            // Create a new agent message with the tool call
            chat_messages.push_back(AgentMessage {
                id: self.next_message_id(),
                role: MessageRole::Agent,
                parts: vec![MessagePart::ToolCall {
                    tool_call_id: tool_id,
                    name,
                    args,
                    state: ToolCallState::Running,
                    output: None,
                    is_error: false,
                }],
                timestamp: std::time::Instant::now(),
            });
        });
    }

    /// Update a tool call's state and output when it completes
    fn update_tool_result(&self, chat_id: &str, tool_id: &str, success: bool, output: Option<String>) {
        self.messages.update(|msgs| {
            if let Some(chat_messages) = msgs.get_mut(chat_id) {
                // Search backwards for the tool call
                // We need to find and update the message containing this tool_id
                let len = chat_messages.len();
                for i in (0..len).rev() {
                    if let Some(msg) = chat_messages.get(i) {
                        if msg.role == MessageRole::Agent {
                            // Check if this message contains our tool call
                            let has_tool = msg.parts.iter().any(|part| {
                                matches!(part, MessagePart::ToolCall { tool_call_id, .. } if tool_call_id == tool_id)
                            });

                            if has_tool {
                                tracing::info!("update_tool_result: found tool {} in message {}", tool_id, i);
                                // Remove, modify, insert back at same position
                                let mut msg = chat_messages.remove(i);
                                for part in msg.parts.iter_mut() {
                                    if let &mut MessagePart::ToolCall {
                                        ref tool_call_id,
                                        ref mut state,
                                        output: ref mut out,
                                        ref mut is_error,
                                        ..
                                    } = part {
                                        if tool_call_id == tool_id {
                                            tracing::info!("update_tool_result: updating tool state to {:?}", if success { "Completed" } else { "Failed" });
                                            *state = if success { ToolCallState::Completed } else { ToolCallState::Failed };
                                            *out = output.clone();
                                            *is_error = !success;
                                        }
                                    }
                                }
                                chat_messages.insert(i, msg);
                                return;
                            }
                        }
                    }
                }
            }
        });
        // Persist changes after tool result update
        self.save_persisted();
    }

    /// Add a permission request
    pub fn add_permission_request(&self, chat_id: &str, request: PermissionRequest) {
        self.pending_permissions.update(|p| {
            let perms = p.entry(chat_id.to_string()).or_insert_with(Vector::new);
            perms.push_back(request);
        });
    }

    /// Remove a permission request
    pub fn remove_permission_request(&self, chat_id: &str, request_id: &str) {
        self.pending_permissions.update(|p| {
            if let Some(perms) = p.get_mut(chat_id) {
                perms.retain(|r| r.id != request_id);
            }
        });
    }

    /// Set error message
    pub fn set_error(&self, error: Option<String>) {
        self.error_message.set(error);
    }

    /// Clear error message
    pub fn clear_error(&self) {
        self.error_message.set(None);
    }

    /// Handle a notification from the agent runtime
    pub fn handle_notification(&self, notification: AgentNotification) {
        let chat_id = self.current_chat_id.get_untracked();

        match notification {
            AgentNotification::Connected { session_id } => {
                tracing::info!("Agent connected with session_id: {}", session_id);
                self.session_id.set(Some(session_id.clone()));
                self.agent_status.set(AgentStatus::Connected);

                // Store the session ID in the current chat for future resumption
                if let Some(chat_id) = self.current_chat_id.get_untracked() {
                    self.chats.update(|chats| {
                        if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                            chat.agent_session_id = Some(session_id.clone());
                        }
                    });
                    // Persist the updated session ID
                    self.save_persisted();
                }

                // Send any pending prompt that was queued while connecting
                if let Some(prompt) = self.pending_prompt.get_untracked() {
                    tracing::info!("Sending queued prompt after connection");
                    self.pending_prompt.set(None);
                    self.do_send_prompt(session_id, prompt);
                }
            }
            AgentNotification::Disconnected => {
                tracing::info!("Agent disconnected");
                self.session_id.set(None);
                self.agent_status.set(AgentStatus::Disconnected);
            }
            AgentNotification::StatusChanged(status) => {
                self.agent_status.set(status);
            }
            AgentNotification::ModelsAvailable { models, current_model_id } => {
                tracing::info!("Models available: {} models, current: {:?}", models.len(), current_model_id);
                self.available_models.set(models);
                self.current_model_id.set(current_model_id);
            }
            AgentNotification::PlanUpdated { entries } => {
                tracing::info!("Plan updated: {} entries", entries.len());
                self.plan_entries.set(entries);
                // Auto-expand panel when plan is received
                if !self.plan_entries.with_untracked(|p| p.is_empty()) {
                    self.todo_panel_expanded.set(true);
                }
            }
            AgentNotification::SessionInfoUpdated { title } => {
                tracing::info!("Session info updated: title={:?}", title);
                // Update the current chat's title if provided
                if let Some(new_title) = title {
                    if let Some(ref chat_id) = chat_id {
                        if !new_title.is_empty() {
                            self.chats.update(|chats| {
                                if let Some(chat) = chats.iter_mut().find(|c| &c.id == chat_id) {
                                    chat.title = new_title.clone();
                                }
                            });
                            // Persist the updated title
                            self.save_persisted();
                        }
                    }
                }
            }
            AgentNotification::ThinkingChunk { text } => {
                tracing::debug!("ThinkingChunk: {} chars", text.len());
                if let Some(ref chat_id) = chat_id {
                    // Append to thinking buffer
                    self.append_streaming_thinking(chat_id, &text);
                }
            }
            AgentNotification::TextChunk { text } => {
                tracing::debug!("TextChunk: {} chars", text.len());
                if let Some(ref chat_id) = chat_id {
                    // Append to streaming buffer (will be flushed when tool starts or turn completes)
                    self.append_streaming_text(chat_id, &text);
                }
            }
            AgentNotification::Message(mut message) => {
                tracing::info!("Message notification: role={:?}, parts={}", message.role, message.parts.len());
                if let Some(ref chat_id) = chat_id {
                    // Assign an ID if the message doesn't have one
                    if message.id == 0 {
                        message.id = self.next_message_id();
                    }
                    self.add_message(chat_id, message);
                }
            }
            AgentNotification::ToolStarted { tool_id, name, input } => {
                tracing::info!("ToolStarted: {} ({})", name, tool_id);
                if let Some(ref chat_id) = chat_id {
                    // Flush any streaming thinking before the tool call
                    self.flush_streaming_thinking(chat_id);
                    // Flush any streaming text before the tool call (interleaved content)
                    self.flush_streaming_text(chat_id);
                    // Add the tool call as a part of the current agent message
                    self.add_tool_call_part(chat_id, tool_id, name, input);
                }
            }
            AgentNotification::ToolCompleted { tool_id, name: _, success, output } => {
                tracing::info!("ToolCompleted: {} success={}", tool_id, success);
                if let Some(ref chat_id) = chat_id {
                    // Update the existing tool call with its result (merged display)
                    self.update_tool_result(chat_id, &tool_id, success, output);
                }
            }
            AgentNotification::PermissionRequest(request) => {
                tracing::info!("PermissionRequest: {}", request.id);
                if let Some(ref chat_id) = chat_id {
                    self.add_permission_request(chat_id, request);
                }
            }
            AgentNotification::TurnCompleted { stop_reason } => {
                tracing::info!("TurnCompleted: stop_reason={:?}", stop_reason);
                if let Some(ref chat_id) = chat_id {
                    // Flush any remaining thinking
                    self.flush_streaming_thinking(chat_id);
                    // Flush the streaming buffer to create a complete message
                    self.flush_streaming_text(chat_id);
                    // End streaming state
                    self.set_streaming(chat_id, false);
                    // Log message count after flush
                    let count = self.messages.with(|msgs| {
                        msgs.get(chat_id).map(|m| m.len()).unwrap_or(0)
                    });
                    tracing::info!("TurnCompleted: messages count after flush = {}", count);
                    // Update chat status to completed
                    self.chats.update(|chats| {
                        if let Some(chat) = chats.iter_mut().find(|c| &c.id == chat_id) {
                            chat.status = ChatStatus::Completed;
                        }
                    });
                }
                // Update agent status back to connected/ready
                self.agent_status.set(AgentStatus::Connected);
            }
            AgentNotification::Error { message } => {
                tracing::error!("Agent error: {}", message);
                self.set_error(Some(message.clone()));
                if let Some(ref chat_id) = chat_id {
                    self.chats.update(|chats| {
                        if let Some(chat) = chats.iter_mut().find(|c| &c.id == chat_id) {
                            chat.status = ChatStatus::Error;
                        }
                    });
                    // End streaming on error
                    self.set_streaming(chat_id, false);
                }
            }
        }
    }

    /// Set up notification processing effect (call once after creation)
    pub fn setup_notification_effect(&self) {
        let agent = self.clone();
        let notification = self.notification;

        self.scope.create_effect(move |_| {
            if let Some(notification) = notification.get() {
                agent.handle_notification(notification);
            }
        });
    }

    /// Update chat title based on first message
    pub fn update_chat_title(&self, chat_id: &str, title: &str) {
        self.chats.update(|chats| {
            if let Some(chat) = chats.iter_mut().find(|c| c.id == chat_id) {
                // Truncate to ~50 chars
                let truncated = if title.len() > 50 {
                    format!("{}...", &title[..47])
                } else {
                    title.to_string()
                };
                chat.title = truncated;
            }
        });
        // Persist changes
        self.save_persisted();
    }
}
