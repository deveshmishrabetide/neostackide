//! Bridge runtime - runs WebSocket server in a background thread
//!
//! This module handles all async bridge operations in a dedicated thread,
//! communicating with the UI via channels. Follows the AgentRuntime pattern.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

use crossbeam_channel::{Receiver, Sender, unbounded};
use futures_util::{SinkExt, StreamExt};
use parking_lot::{Mutex, RwLock};
use tokio::net::{TcpListener, TcpStream};
use tokio::runtime::Runtime;
use tokio::sync::mpsc;
use tokio_tungstenite::{accept_async, tungstenite::Message};
use uuid::Uuid;

use super::types::*;

/// Request ID for tracking pending commands
pub type RequestId = u64;

/// RPC messages sent to the bridge runtime
#[derive(Debug)]
pub enum BridgeRpc {
    /// Start the WebSocket server
    Start,
    /// Stop the WebSocket server
    Stop,
    /// Send a command to a specific client
    SendCommand {
        id: RequestId,
        session_id: String,
        cmd: String,
        args: Option<serde_json::Value>,
    },
    /// Send a command to any connected client
    SendCommandToAny {
        id: RequestId,
        cmd: String,
        args: Option<serde_json::Value>,
    },
    /// Shutdown the runtime
    Shutdown,
}

/// Response from bridge operations
#[derive(Debug, Clone)]
pub enum BridgeResponse {
    /// Server started
    Started { port: u16 },
    /// Command completed
    CommandCompleted(BridgeEvent),
    /// Error occurred
    Error { message: String },
}

/// Callback for async responses
pub trait BridgeCallback: Send + FnOnce(Result<BridgeResponse, String>) {}
impl<F: Send + FnOnce(Result<BridgeResponse, String>)> BridgeCallback for F {}

enum ResponseHandler {
    Callback(Box<dyn BridgeCallback>),
    #[allow(dead_code)]
    Chan(Sender<Result<BridgeResponse, String>>),
}

impl ResponseHandler {
    fn invoke(self, result: Result<BridgeResponse, String>) {
        match self {
            ResponseHandler::Callback(f) => f(result),
            ResponseHandler::Chan(tx) => {
                let _ = tx.send(result);
            }
        }
    }
}

/// Handler for bridge RPC, similar to AgentRpcHandler
#[derive(Clone)]
pub struct BridgeRpcHandler {
    /// Channel to send commands to the bridge runtime
    tx: Sender<BridgeRpc>,
    /// Channel to receive commands (used by the runtime)
    rx: Receiver<BridgeRpc>,
    /// Request ID counter
    id: Arc<AtomicU64>,
    /// Pending request handlers
    pending: Arc<Mutex<HashMap<RequestId, ResponseHandler>>>,
    /// Connected client count (updated via notifications)
    connected_count: Arc<AtomicU64>,
}

impl BridgeRpcHandler {
    /// Create a new bridge RPC handler
    pub fn new() -> Self {
        let (tx, rx) = unbounded();
        Self {
            tx,
            rx,
            id: Arc::new(AtomicU64::new(0)),
            pending: Arc::new(Mutex::new(HashMap::new())),
            connected_count: Arc::new(AtomicU64::new(0)),
        }
    }

    /// Check if any UE clients are connected
    pub fn is_connected(&self) -> bool {
        self.connected_count.load(Ordering::Relaxed) > 0
    }

    /// Update connected client count (called from notification handler)
    pub fn set_connected_count(&self, count: u64) {
        self.connected_count.store(count, Ordering::Relaxed);
    }

    /// Increment connected count (called when client connects)
    pub fn client_connected(&self) {
        self.connected_count.fetch_add(1, Ordering::Relaxed);
    }

    /// Decrement connected count (called when client disconnects)
    pub fn client_disconnected(&self) {
        let prev = self.connected_count.fetch_sub(1, Ordering::Relaxed);
        if prev == 0 {
            // Underflow protection
            self.connected_count.store(0, Ordering::Relaxed);
        }
    }

    /// Send a command and wait for response (async)
    pub async fn send_command(
        &self,
        session_id: Option<&str>,
        cmd: &str,
        args: Option<serde_json::Value>,
    ) -> Result<BridgeEvent, String> {
        let (tx, rx) = crossbeam_channel::bounded(1);
        let id = self.id.fetch_add(1, Ordering::Relaxed);
        self.pending.lock().insert(id, ResponseHandler::Chan(tx));

        // Send the command
        if let Some(sid) = session_id {
            let _ = self.tx.send(BridgeRpc::SendCommand {
                id,
                session_id: sid.to_string(),
                cmd: cmd.to_string(),
                args,
            });
        } else {
            let _ = self.tx.send(BridgeRpc::SendCommandToAny {
                id,
                cmd: cmd.to_string(),
                args,
            });
        }

        // Wait for response with timeout
        match rx.recv_timeout(std::time::Duration::from_secs(30)) {
            Ok(result) => match result {
                Ok(BridgeResponse::CommandCompleted(event)) => Ok(event),
                Ok(BridgeResponse::Error { message }) => Err(message),
                Ok(BridgeResponse::Started { .. }) => Err("Unexpected response type".to_string()),
                Err(e) => Err(e),
            },
            Err(_) => Err("Command timed out".to_string()),
        }
    }

    /// Get the receiver for the runtime to process
    pub fn rx(&self) -> &Receiver<BridgeRpc> {
        &self.rx
    }

    /// Start the WebSocket server
    pub fn start(&self) {
        tracing::info!("BridgeRpcHandler: Sending Start message");
        let _ = self.tx.send(BridgeRpc::Start);
    }

    /// Stop the WebSocket server
    pub fn stop(&self) {
        let _ = self.tx.send(BridgeRpc::Stop);
    }

    /// Send a command to a specific client (async with callback)
    pub fn send_command_async(
        &self,
        session_id: String,
        cmd: String,
        args: Option<serde_json::Value>,
        callback: impl BridgeCallback + 'static,
    ) {
        let id = self.id.fetch_add(1, Ordering::Relaxed);
        self.pending.lock().insert(id, ResponseHandler::Callback(Box::new(callback)));

        let _ = self.tx.send(BridgeRpc::SendCommand {
            id,
            session_id,
            cmd,
            args,
        });
    }

    /// Send a command to any connected client (async with callback)
    pub fn send_command_to_any_async(
        &self,
        cmd: String,
        args: Option<serde_json::Value>,
        callback: impl BridgeCallback + 'static,
    ) {
        let id = self.id.fetch_add(1, Ordering::Relaxed);
        self.pending.lock().insert(id, ResponseHandler::Callback(Box::new(callback)));

        let _ = self.tx.send(BridgeRpc::SendCommandToAny {
            id,
            cmd,
            args,
        });
    }

    /// Convenience method: Start Play In Editor
    pub fn pie_start(&self, session_id: Option<String>, callback: impl BridgeCallback + 'static) {
        if let Some(sid) = session_id {
            self.send_command_async(sid, commands::PIE_START.to_string(), None, callback);
        } else {
            self.send_command_to_any_async(commands::PIE_START.to_string(), None, callback);
        }
    }

    /// Convenience method: Stop Play In Editor
    pub fn pie_stop(&self, session_id: Option<String>, callback: impl BridgeCallback + 'static) {
        if let Some(sid) = session_id {
            self.send_command_async(sid, commands::PIE_STOP.to_string(), None, callback);
        } else {
            self.send_command_to_any_async(commands::PIE_STOP.to_string(), None, callback);
        }
    }

    /// Convenience method: Trigger hot reload
    pub fn hot_reload(&self, session_id: Option<String>, callback: impl BridgeCallback + 'static) {
        if let Some(sid) = session_id {
            self.send_command_async(sid, commands::HOT_RELOAD.to_string(), None, callback);
        } else {
            self.send_command_to_any_async(commands::HOT_RELOAD.to_string(), None, callback);
        }
    }

    /// Shutdown the handler
    pub fn shutdown(&self) {
        let _ = self.tx.send(BridgeRpc::Shutdown);
    }

    /// Handle a response from the bridge runtime
    pub fn handle_response(&self, id: RequestId, result: Result<BridgeResponse, String>) {
        if let Some(handler) = self.pending.lock().remove(&id) {
            handler.invoke(result);
        }
    }
}

impl Default for BridgeRpcHandler {
    fn default() -> Self {
        Self::new()
    }
}

// Re-export BridgeEvent for MCP
pub use super::types::BridgeEvent;

/// State of a connected client
struct ClientState {
    info: UEClient,
    tx: mpsc::Sender<String>,
}

/// Pending command waiting for response
struct PendingCommand {
    request_id: String,
    rpc_id: RequestId,
}

/// The bridge runtime that manages the WebSocket server
pub struct BridgeRuntime {
    /// RPC handler for receiving commands
    rpc: BridgeRpcHandler,
    /// Channel to send notifications to UI
    notification_tx: Sender<BridgeNotification>,
    /// Currently bound port
    port: Arc<RwLock<Option<u16>>>,
    /// Connected clients
    clients: Arc<RwLock<HashMap<String, ClientState>>>,
    /// Pending commands waiting for responses
    pending_commands: Arc<Mutex<HashMap<String, PendingCommand>>>,
    /// Shutdown flag
    shutdown: Arc<AtomicBool>,
}

impl BridgeRuntime {
    /// Create a new bridge runtime
    pub fn new(
        rpc: BridgeRpcHandler,
        notification_tx: Sender<BridgeNotification>,
    ) -> Self {
        Self {
            rpc,
            notification_tx,
            port: Arc::new(RwLock::new(None)),
            clients: Arc::new(RwLock::new(HashMap::new())),
            pending_commands: Arc::new(Mutex::new(HashMap::new())),
            shutdown: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Run the main loop (call this from a dedicated thread)
    pub fn run(self) {
        // Create a tokio runtime for this thread
        let rt = Runtime::new().expect("Failed to create tokio runtime");

        // Process RPC commands
        for msg in self.rpc.rx().iter() {
            match msg {
                BridgeRpc::Start => {
                    let port = self.port.clone();
                    let clients = self.clients.clone();
                    let pending_commands = self.pending_commands.clone();
                    let notification_tx = self.notification_tx.clone();
                    let shutdown = self.shutdown.clone();
                    let rpc = self.rpc.clone();

                    rt.block_on(async {
                        match Self::do_start(
                            port,
                            clients,
                            pending_commands,
                            notification_tx,
                            shutdown,
                            rpc,
                        ).await {
                            Ok(bound_port) => {
                                tracing::info!("Bridge server started on port {}", bound_port);
                            }
                            Err(e) => {
                                tracing::error!("Failed to start bridge server: {}", e);
                            }
                        }
                    });
                }

                BridgeRpc::Stop => {
                    self.shutdown.store(true, Ordering::SeqCst);
                    *self.port.write() = None;
                    self.clients.write().clear();
                    let _ = self.notification_tx.send(BridgeNotification::ServerStopped);
                }

                BridgeRpc::SendCommand { id, session_id, cmd, args } => {
                    let clients = self.clients.clone();
                    let pending = self.pending_commands.clone();
                    let rpc = self.rpc.clone();

                    rt.block_on(async {
                        Self::do_send_command(
                            clients,
                            pending,
                            rpc,
                            id,
                            Some(session_id),
                            cmd,
                            args,
                        ).await;
                    });
                }

                BridgeRpc::SendCommandToAny { id, cmd, args } => {
                    let clients = self.clients.clone();
                    let pending = self.pending_commands.clone();
                    let rpc = self.rpc.clone();

                    rt.block_on(async {
                        Self::do_send_command(
                            clients,
                            pending,
                            rpc,
                            id,
                            None,
                            cmd,
                            args,
                        ).await;
                    });
                }

                BridgeRpc::Shutdown => {
                    self.shutdown.store(true, Ordering::SeqCst);
                    *self.port.write() = None;
                    self.clients.write().clear();
                    break;
                }
            }
        }
    }

    async fn do_start(
        port: Arc<RwLock<Option<u16>>>,
        clients: Arc<RwLock<HashMap<String, ClientState>>>,
        pending_commands: Arc<Mutex<HashMap<String, PendingCommand>>>,
        notification_tx: Sender<BridgeNotification>,
        shutdown: Arc<AtomicBool>,
        rpc: BridgeRpcHandler,
    ) -> Result<u16, String> {
        // Reset shutdown flag
        shutdown.store(false, Ordering::SeqCst);

        // Try ports 27020-27029
        for offset in 0..WS_PORT_ATTEMPTS {
            let try_port = WS_BASE_PORT + offset;
            match TcpListener::bind(format!("127.0.0.1:{}", try_port)).await {
                Ok(listener) => {
                    *port.write() = Some(try_port);

                    // Send notification
                    let _ = notification_tx.send(BridgeNotification::ServerStarted {
                        port: try_port,
                    });

                    // Spawn accept loop
                    let clients_clone = clients.clone();
                    let pending_clone = pending_commands.clone();
                    let notification_tx_clone = notification_tx.clone();
                    let shutdown_clone = shutdown.clone();
                    let rpc_clone = rpc.clone();

                    tokio::spawn(async move {
                        Self::accept_loop(
                            listener,
                            clients_clone,
                            pending_clone,
                            notification_tx_clone,
                            shutdown_clone,
                            rpc_clone,
                        ).await;
                    });

                    return Ok(try_port);
                }
                Err(e) => {
                    tracing::debug!("Port {} unavailable: {}", try_port, e);
                }
            }
        }

        Err(format!("Failed to bind to any port in range {}-{}",
            WS_BASE_PORT, WS_BASE_PORT + WS_PORT_ATTEMPTS - 1))
    }

    async fn accept_loop(
        listener: TcpListener,
        clients: Arc<RwLock<HashMap<String, ClientState>>>,
        pending_commands: Arc<Mutex<HashMap<String, PendingCommand>>>,
        notification_tx: Sender<BridgeNotification>,
        shutdown: Arc<AtomicBool>,
        rpc: BridgeRpcHandler,
    ) {
        loop {
            if shutdown.load(Ordering::SeqCst) {
                break;
            }

            tokio::select! {
                result = listener.accept() => {
                    match result {
                        Ok((stream, addr)) => {
                            tracing::info!("New connection from: {}", addr);
                            let clients_clone = clients.clone();
                            let pending_clone = pending_commands.clone();
                            let notification_tx_clone = notification_tx.clone();
                            let shutdown_clone = shutdown.clone();
                            let rpc_clone = rpc.clone();

                            tokio::spawn(async move {
                                if let Err(e) = Self::handle_connection(
                                    stream,
                                    clients_clone,
                                    pending_clone,
                                    notification_tx_clone,
                                    shutdown_clone,
                                    rpc_clone,
                                ).await {
                                    tracing::error!("Connection error: {}", e);
                                }
                            });
                        }
                        Err(e) => {
                            if !shutdown.load(Ordering::SeqCst) {
                                tracing::error!("Accept error: {}", e);
                            }
                        }
                    }
                }
                _ = tokio::time::sleep(tokio::time::Duration::from_millis(100)) => {
                    // Check shutdown flag periodically
                }
            }
        }
    }

    async fn handle_connection(
        stream: TcpStream,
        clients: Arc<RwLock<HashMap<String, ClientState>>>,
        pending_commands: Arc<Mutex<HashMap<String, PendingCommand>>>,
        notification_tx: Sender<BridgeNotification>,
        shutdown: Arc<AtomicBool>,
        rpc: BridgeRpcHandler,
    ) -> Result<(), String> {
        // Upgrade to WebSocket
        let ws_stream = accept_async(stream)
            .await
            .map_err(|e| format!("WebSocket handshake failed: {}", e))?;

        let (mut write, mut read) = ws_stream.split();

        // Wait for handshake message
        let handshake = tokio::time::timeout(
            tokio::time::Duration::from_secs(5),
            read.next()
        )
        .await
        .map_err(|_| "Handshake timeout")?
        .ok_or("Connection closed before handshake")?
        .map_err(|e| format!("Failed to receive handshake: {}", e))?;

        let handshake_text = match handshake {
            Message::Text(text) => text.to_string(),
            _ => return Err("Expected text message for handshake".to_string()),
        };

        let handshake_msg: HandshakeMessage = serde_json::from_str(&handshake_text)
            .map_err(|e| format!("Invalid handshake JSON: {}", e))?;

        // Validate handshake
        if !handshake_msg.is_valid() {
            let ack = HandshakeAck::failure(format!(
                "Protocol version mismatch: expected {}, got {}",
                PROTOCOL_VERSION, handshake_msg.version
            ));
            let ack_json = serde_json::to_string(&ack).unwrap();
            let _ = write.send(Message::Text(ack_json.into())).await;
            return Err("Protocol version mismatch".to_string());
        }

        // Generate session ID
        let session_id = Uuid::new_v4().to_string();

        // Send acknowledgment
        let ack = HandshakeAck::success(session_id.clone());
        let ack_json = serde_json::to_string(&ack).unwrap();
        write.send(Message::Text(ack_json.into()))
            .await
            .map_err(|e| format!("Failed to send handshake ack: {}", e))?;

        // Create client info
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();

        let client = UEClient {
            session_id: session_id.clone(),
            project_id: handshake_msg.project_id,
            project_name: handshake_msg.project_name,
            project_path: handshake_msg.project_path,
            engine_version: handshake_msg.engine_version,
            pid: handshake_msg.pid,
            connected_at: now,
        };

        // Create channel for sending messages to this client
        let (tx, mut rx) = mpsc::channel::<String>(32);

        // Store client state
        clients.write().insert(session_id.clone(), ClientState {
            info: client.clone(),
            tx,
        });

        // Notify UI
        let project_name_log = client.project_name.clone();
        let _ = notification_tx.send(BridgeNotification::ClientConnected(client));

        tracing::info!("Client connected: {} ({})", session_id, project_name_log);

        // Spawn sender task
        let session_id_sender = session_id.clone();
        let shutdown_sender = shutdown.clone();
        tokio::spawn(async move {
            while let Some(msg) = rx.recv().await {
                if shutdown_sender.load(Ordering::SeqCst) {
                    break;
                }
                if write.send(Message::Text(msg.into())).await.is_err() {
                    tracing::warn!("Failed to send message to client {}", session_id_sender);
                    break;
                }
            }
        });

        // Message receive loop
        while let Some(msg_result) = read.next().await {
            if shutdown.load(Ordering::SeqCst) {
                break;
            }

            match msg_result {
                Ok(Message::Text(text)) => {
                    let text = text.to_string();
                    // Try to parse as BridgeEvent (response)
                    if let Ok(event) = serde_json::from_str::<BridgeEvent>(&text) {
                        // Check if this is a response to a pending command
                        if let Some(request_id) = &event.request_id {
                            if let Some(pending) = pending_commands.lock().remove(request_id) {
                                rpc.handle_response(
                                    pending.rpc_id,
                                    Ok(BridgeResponse::CommandCompleted(event.clone())),
                                );
                            }
                        }

                        // Also notify UI
                        let _ = notification_tx.send(BridgeNotification::CommandResponse(event));
                    }
                }
                Ok(Message::Close(_)) => {
                    break;
                }
                Ok(Message::Ping(data)) => {
                    // Pong is handled automatically by tungstenite
                    tracing::trace!("Received ping from {}", session_id);
                    let _ = data; // Silence unused warning
                }
                Err(e) => {
                    tracing::error!("WebSocket error for {}: {}", session_id, e);
                    break;
                }
                _ => {}
            }
        }

        // Client disconnected
        clients.write().remove(&session_id);
        let _ = notification_tx.send(BridgeNotification::ClientDisconnected {
            session_id: session_id.clone(),
        });

        tracing::info!("Client disconnected: {}", session_id);

        Ok(())
    }

    async fn do_send_command(
        clients: Arc<RwLock<HashMap<String, ClientState>>>,
        pending_commands: Arc<Mutex<HashMap<String, PendingCommand>>>,
        rpc: BridgeRpcHandler,
        rpc_id: RequestId,
        session_id: Option<String>,
        cmd: String,
        args: Option<serde_json::Value>,
    ) {
        let request_id = Uuid::new_v4().to_string();

        let command = BridgeCommand {
            cmd,
            request_id: request_id.clone(),
            args,
        };

        let command_json = match serde_json::to_string(&command) {
            Ok(json) => json,
            Err(e) => {
                rpc.handle_response(rpc_id, Err(format!("Failed to serialize command: {}", e)));
                return;
            }
        };

        // Find the client to send to
        let tx = {
            let clients_guard = clients.read();

            if let Some(sid) = session_id {
                clients_guard.get(&sid).map(|c| c.tx.clone())
            } else {
                // Send to first available client
                clients_guard.values().next().map(|c| c.tx.clone())
            }
        };

        match tx {
            Some(tx) => {
                // Store pending command
                pending_commands.lock().insert(request_id.clone(), PendingCommand {
                    request_id,
                    rpc_id,
                });

                // Send command
                if let Err(e) = tx.send(command_json).await {
                    rpc.handle_response(rpc_id, Err(format!("Failed to send command: {}", e)));
                }
                // Response will be handled when we receive the event
            }
            None => {
                rpc.handle_response(rpc_id, Err("No clients connected".to_string()));
            }
        }
    }
}

/// Start the bridge runtime in a background thread
pub fn start_bridge_runtime(
    rpc: BridgeRpcHandler,
    notification_tx: Sender<BridgeNotification>,
) {
    std::thread::Builder::new()
        .name("BridgeRuntime".to_string())
        .spawn(move || {
            let runtime = BridgeRuntime::new(rpc, notification_tx);
            runtime.run();
        })
        .expect("Failed to spawn bridge runtime thread");
}
