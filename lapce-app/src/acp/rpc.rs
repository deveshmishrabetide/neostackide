//! Agent RPC handler following Lapce's proxy pattern
//!
//! This provides a clean interface for communicating with ACP agents,
//! similar to how ProxyRpcHandler works for the LSP proxy.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use crossbeam_channel::{Receiver, Sender, unbounded};
use parking_lot::Mutex;

use super::types::*;

/// Request ID for tracking pending requests
pub type RequestId = u64;

/// RPC messages sent to the agent runtime
pub enum AgentRpc {
    /// Connect to an agent
    Connect {
        config: AgentConfig,
        workspace_path: PathBuf,
    },
    /// Send a prompt
    Prompt {
        id: RequestId,
        session_id: String,
        prompt: String,
    },
    /// Cancel the current operation
    Cancel {
        session_id: String,
    },
    /// Respond to a permission request
    PermissionResponse {
        request_id: String,
        response: PermissionResponse,
    },
    /// Disconnect from the agent
    Disconnect,
    /// Shutdown the handler
    Shutdown,
}

/// Response from agent operations
#[derive(Debug, Clone)]
pub enum AgentResponse {
    /// Connection established
    Connected {
        session_id: String,
    },
    /// Prompt completed
    PromptCompleted {
        stop_reason: Option<String>,
    },
    /// Operation cancelled
    Cancelled,
    /// Error occurred
    Error {
        message: String,
    },
}

/// Callback for async responses
pub trait AgentCallback: Send + FnOnce(Result<AgentResponse, String>) {}
impl<F: Send + FnOnce(Result<AgentResponse, String>)> AgentCallback for F {}

enum ResponseHandler {
    Callback(Box<dyn AgentCallback>),
    Chan(Sender<Result<AgentResponse, String>>),
}

impl ResponseHandler {
    fn invoke(self, result: Result<AgentResponse, String>) {
        match self {
            ResponseHandler::Callback(f) => f(result),
            ResponseHandler::Chan(tx) => {
                let _ = tx.send(result);
            }
        }
    }
}

/// Handler for agent RPC, similar to ProxyRpcHandler
#[derive(Clone)]
pub struct AgentRpcHandler {
    /// Channel to send commands to the agent runtime
    tx: Sender<AgentRpc>,
    /// Channel to receive commands (used by the runtime)
    rx: Receiver<AgentRpc>,
    /// Request ID counter
    id: Arc<AtomicU64>,
    /// Pending request handlers
    pending: Arc<Mutex<HashMap<RequestId, ResponseHandler>>>,
}

impl AgentRpcHandler {
    /// Create a new agent RPC handler
    pub fn new() -> Self {
        let (tx, rx) = unbounded();
        Self {
            tx,
            rx,
            id: Arc::new(AtomicU64::new(0)),
            pending: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Get the receiver for the runtime to process
    pub fn rx(&self) -> &Receiver<AgentRpc> {
        &self.rx
    }

    /// Connect to an agent with the given configuration
    pub fn connect(&self, config: AgentConfig, workspace_path: PathBuf) {
        tracing::info!("AgentRpcHandler: Sending Connect message for {}", config.command);
        match self.tx.send(AgentRpc::Connect {
            config,
            workspace_path,
        }) {
            Ok(_) => tracing::info!("AgentRpcHandler: Connect message sent successfully"),
            Err(e) => tracing::error!("AgentRpcHandler: Failed to send Connect message: {}", e),
        }
    }

    /// Send a prompt to the agent (async with callback)
    pub fn prompt_async(
        &self,
        session_id: String,
        prompt: String,
        callback: impl AgentCallback + 'static,
    ) {
        let id = self.id.fetch_add(1, Ordering::Relaxed);
        self.pending.lock().insert(id, ResponseHandler::Callback(Box::new(callback)));

        let _ = self.tx.send(AgentRpc::Prompt {
            id,
            session_id,
            prompt,
        });
    }

    /// Send a prompt to the agent (blocking)
    pub fn prompt(&self, session_id: String, prompt: String) -> Result<AgentResponse, String> {
        let id = self.id.fetch_add(1, Ordering::Relaxed);
        let (tx, rx) = crossbeam_channel::bounded(1);
        self.pending.lock().insert(id, ResponseHandler::Chan(tx));

        let _ = self.tx.send(AgentRpc::Prompt {
            id,
            session_id,
            prompt,
        });

        rx.recv().unwrap_or_else(|_| Err("Channel closed".to_string()))
    }

    /// Cancel the current operation
    pub fn cancel(&self, session_id: String) {
        let _ = self.tx.send(AgentRpc::Cancel { session_id });
    }

    /// Respond to a permission request
    pub fn respond_permission(&self, request_id: String, response: PermissionResponse) {
        let _ = self.tx.send(AgentRpc::PermissionResponse {
            request_id,
            response,
        });
    }

    /// Disconnect from the agent
    pub fn disconnect(&self) {
        let _ = self.tx.send(AgentRpc::Disconnect);
    }

    /// Shutdown the handler
    pub fn shutdown(&self) {
        let _ = self.tx.send(AgentRpc::Shutdown);
    }

    /// Handle a response from the agent runtime
    pub fn handle_response(&self, id: RequestId, result: Result<AgentResponse, String>) {
        if let Some(handler) = self.pending.lock().remove(&id) {
            handler.invoke(result);
        }
    }
}

impl Default for AgentRpcHandler {
    fn default() -> Self {
        Self::new()
    }
}
