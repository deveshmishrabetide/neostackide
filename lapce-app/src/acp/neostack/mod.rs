//! NeoStack embedded ACP agent
//!
//! This module provides an embedded AI coding agent that runs in-process,
//! communicating via ACP protocol over in-memory duplex channels instead
//! of subprocess stdio.

mod agent;
pub mod provider;
pub mod streaming;

use std::cell::RefCell;
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::Rc;

use agent_client_protocol::AgentSideConnection;

pub use agent::NeoStackAgentImpl;
use provider::{ChatMessage, NeostackProvider};

/// Session state for a NeoStack agent conversation
#[derive(Debug, Clone)]
pub struct Session {
    pub id: String,
    pub cwd: PathBuf,
    pub model_id: String,
    pub mode_id: String,
    pub cancelled: bool,
    pub messages: Vec<ChatMessage>,
}

impl Session {
    pub fn new(id: &str, cwd: PathBuf) -> Self {
        Self {
            id: id.to_string(),
            cwd,
            model_id: "anthropic/claude-sonnet-4".to_string(),
            mode_id: "default".to_string(),
            cancelled: false,
            messages: Vec::new(),
        }
    }

    pub fn cancel(&mut self) {
        self.cancelled = true;
    }

    pub fn is_cancelled(&self) -> bool {
        self.cancelled
    }
}

/// The embedded NeoStack agent
///
/// This agent implements the ACP `Agent` trait and communicates with
/// the Lapce client via in-memory duplex channels.
pub struct NeoStackAgent {
    workspace_path: PathBuf,
    sessions: RefCell<HashMap<String, Session>>,
    connection: RefCell<Option<Rc<AgentSideConnection>>>,
    provider: NeostackProvider,
}

impl NeoStackAgent {
    /// Create a new NeoStack agent for the given workspace
    pub fn new(workspace_path: PathBuf) -> Self {
        Self {
            workspace_path,
            sessions: RefCell::new(HashMap::new()),
            connection: RefCell::new(None),
            provider: NeostackProvider::new(),
        }
    }

    /// Get the provider for AI inference
    pub fn provider(&self) -> &NeostackProvider {
        &self.provider
    }

    /// Get the ACP connection for sending notifications
    pub fn connection(&self) -> Option<Rc<AgentSideConnection>> {
        self.connection.borrow().clone()
    }

    /// Set the ACP connection for sending notifications back to client
    pub fn set_connection(&self, conn: AgentSideConnection) {
        *self.connection.borrow_mut() = Some(Rc::new(conn));
    }

    /// Get the workspace path
    pub fn workspace_path(&self) -> &PathBuf {
        &self.workspace_path
    }

    /// Get a session by ID
    pub fn get_session(&self, session_id: &str) -> Option<Session> {
        self.sessions.borrow().get(session_id).cloned()
    }

    /// Add a message to a session's history
    pub fn add_message(&self, session_id: &str, message: ChatMessage) {
        if let Some(session) = self.sessions.borrow_mut().get_mut(session_id) {
            session.messages.push(message);
        }
    }

    /// Get messages for a session
    pub fn get_messages(&self, session_id: &str) -> Vec<ChatMessage> {
        self.sessions
            .borrow()
            .get(session_id)
            .map(|s| s.messages.clone())
            .unwrap_or_default()
    }

    /// Insert a new session
    pub fn insert_session(&self, session: Session) {
        self.sessions.borrow_mut().insert(session.id.clone(), session);
    }

    /// Cancel a session
    pub fn cancel_session(&self, session_id: &str) {
        if let Some(session) = self.sessions.borrow_mut().get_mut(session_id) {
            session.cancel();
        }
    }

    /// Update session model
    pub fn set_session_model(&self, session_id: &str, model_id: &str) {
        if let Some(session) = self.sessions.borrow_mut().get_mut(session_id) {
            session.model_id = model_id.to_string();
        }
    }

    /// Update session mode
    pub fn set_session_mode(&self, session_id: &str, mode_id: &str) {
        if let Some(session) = self.sessions.borrow_mut().get_mut(session_id) {
            session.mode_id = mode_id.to_string();
        }
    }
}
