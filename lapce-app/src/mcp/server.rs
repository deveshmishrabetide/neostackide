//! MCP HTTP Server
//!
//! HTTP server for MCP protocol on port 27030+.

use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::Arc;

use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpListener;
use crossbeam_channel::Sender;

use super::executor::execute_tool;
use super::tools::get_all_tools;
use super::types::*;
use crate::bridge::BridgeRpcHandler;

/// Global MCP HTTP server port
static MCP_HTTP_PORT: AtomicU16 = AtomicU16::new(0);

const MCP_HTTP_BASE_PORT: u16 = 27030;
const MCP_HTTP_PORT_ATTEMPTS: u16 = 10;

/// Get current MCP HTTP port (0 if not running)
pub fn get_mcp_port() -> u16 {
    MCP_HTTP_PORT.load(Ordering::Relaxed)
}

/// Start MCP HTTP server
pub async fn start_mcp_server(
    bridge: BridgeRpcHandler,
    notification_tx: Sender<McpNotification>,
) -> Result<u16, String> {
    // Find an available port
    let mut listener: Option<TcpListener> = None;
    let mut bound_port: u16 = 0;

    for offset in 0..MCP_HTTP_PORT_ATTEMPTS {
        let port = MCP_HTTP_BASE_PORT + offset;
        match TcpListener::bind(format!("127.0.0.1:{}", port)).await {
            Ok(l) => {
                listener = Some(l);
                bound_port = port;
                break;
            }
            Err(e) => {
                tracing::debug!("[MCP] Port {} unavailable: {}", port, e);
            }
        }
    }

    let listener = listener.ok_or_else(|| {
        format!(
            "Failed to bind MCP server to any port in range {}-{}",
            MCP_HTTP_BASE_PORT,
            MCP_HTTP_BASE_PORT + MCP_HTTP_PORT_ATTEMPTS - 1
        )
    })?;

    MCP_HTTP_PORT.store(bound_port, Ordering::Relaxed);
    tracing::info!("[MCP] Server starting on port {}", bound_port);

    // Notify UI
    let _ = notification_tx.send(McpNotification::ServerStarted { port: bound_port });

    // Wrap bridge in Arc for sharing across connections
    let bridge = Arc::new(bridge);
    let notification_tx = Arc::new(notification_tx);

    // Spawn the server task
    tokio::spawn(async move {
        loop {
            match listener.accept().await {
                Ok((stream, addr)) => {
                    tracing::debug!("[MCP] Connection from {}", addr);
                    let bridge_clone = Arc::clone(&bridge);
                    let notif_clone = Arc::clone(&notification_tx);
                    tokio::spawn(async move {
                        if let Err(e) = handle_connection(stream, bridge_clone, notif_clone).await {
                            tracing::debug!("[MCP] Connection error: {}", e);
                        }
                    });
                }
                Err(e) => {
                    tracing::error!("[MCP] Accept error: {}", e);
                }
            }
        }
    });

    Ok(bound_port)
}

/// Handle an HTTP connection
async fn handle_connection(
    mut stream: tokio::net::TcpStream,
    bridge: Arc<BridgeRpcHandler>,
    notification_tx: Arc<Sender<McpNotification>>,
) -> Result<(), String> {
    let (reader, mut writer) = stream.split();
    let mut buf_reader = BufReader::new(reader);

    // Read HTTP request line
    let mut request_line = String::new();
    buf_reader.read_line(&mut request_line).await.map_err(|e| e.to_string())?;

    // Parse method and path
    let parts: Vec<&str> = request_line.split_whitespace().collect();
    if parts.len() < 2 {
        return send_text_response(&mut writer, 400, "Bad Request").await;
    }

    let method = parts[0];
    let path = parts[1];

    // Read headers
    let mut content_length: usize = 0;
    loop {
        let mut header_line = String::new();
        buf_reader.read_line(&mut header_line).await.map_err(|e| e.to_string())?;
        if header_line.trim().is_empty() {
            break;
        }
        if header_line.to_lowercase().starts_with("content-length:") {
            if let Some(len_str) = header_line.split(':').nth(1) {
                content_length = len_str.trim().parse().unwrap_or(0);
            }
        }
    }

    // Read body if present
    let body = if content_length > 0 {
        let mut body_buf = vec![0u8; content_length];
        tokio::io::AsyncReadExt::read_exact(&mut buf_reader, &mut body_buf)
            .await
            .map_err(|e| e.to_string())?;
        String::from_utf8(body_buf).unwrap_or_default()
    } else {
        String::new()
    };

    // Route request
    match (method, path) {
        ("POST", "/mcp") | ("POST", "/mcp/") => {
            handle_mcp_request(&mut writer, &body, &bridge, &notification_tx).await
        }
        ("GET", "/health") | ("GET", "/health/") => {
            send_json_response(&mut writer, 200, &json!({"status": "ok"})).await
        }
        _ => {
            send_text_response(&mut writer, 404, "Not Found").await
        }
    }
}

/// Handle MCP JSON-RPC request
async fn handle_mcp_request(
    writer: &mut tokio::net::tcp::WriteHalf<'_>,
    body: &str,
    bridge: &BridgeRpcHandler,
    notification_tx: &Sender<McpNotification>,
) -> Result<(), String> {
    // Parse JSON-RPC request
    let request: JsonRpcRequest = match serde_json::from_str(body) {
        Ok(req) => req,
        Err(e) => {
            let response = JsonRpcResponse::error(
                JsonRpcId::Null,
                PARSE_ERROR,
                format!("Parse error: {}", e),
            );
            return send_json_response(writer, 200, &serde_json::to_value(response).unwrap()).await;
        }
    };

    tracing::debug!("[MCP] Method: {}", request.method);

    // Notifications have no id - acknowledge silently
    if request.id.is_none() {
        return send_json_response(writer, 202, &json!({})).await;
    }

    let id = request.id.clone().unwrap_or(JsonRpcId::Null);

    // Handle the request
    let response = match request.method.as_str() {
        "initialize" => handle_initialize(id),
        "tools/list" => handle_tools_list(id),
        "tools/call" => handle_tools_call(id, request.params, bridge, notification_tx).await,
        "ping" => JsonRpcResponse::success(id, json!({ "pong": true })),
        _ => JsonRpcResponse::error(
            id,
            METHOD_NOT_FOUND,
            format!("Method not found: {}", request.method),
        ),
    };

    send_json_response(writer, 200, &serde_json::to_value(response).unwrap()).await
}

fn handle_initialize(id: JsonRpcId) -> JsonRpcResponse {
    let result = InitializeResult {
        protocol_version: MCP_PROTOCOL_VERSION.to_string(),
        capabilities: ServerCapabilities {
            tools: Some(ToolsCapability {
                list_changed: Some(false),
            }),
        },
        server_info: ServerInfo {
            name: SERVER_NAME.to_string(),
            version: SERVER_VERSION.to_string(),
        },
    };

    JsonRpcResponse::success(id, serde_json::to_value(result).unwrap())
}

fn handle_tools_list(id: JsonRpcId) -> JsonRpcResponse {
    let tools = get_all_tools();
    JsonRpcResponse::success(id, json!({ "tools": tools }))
}

async fn handle_tools_call(
    id: JsonRpcId,
    params: Option<Value>,
    bridge: &BridgeRpcHandler,
    notification_tx: &Sender<McpNotification>,
) -> JsonRpcResponse {
    let params: ToolCallParams = match params {
        Some(p) => match serde_json::from_value(p) {
            Ok(params) => params,
            Err(e) => {
                return JsonRpcResponse::error(
                    id,
                    INVALID_PARAMS,
                    format!("Invalid params: {}", e),
                );
            }
        },
        None => {
            return JsonRpcResponse::error(id, INVALID_PARAMS, "Missing params");
        }
    };

    // Notify UI about tool call
    let _ = notification_tx.send(McpNotification::ToolCalled {
        tool: params.name.clone(),
    });

    let args = params.arguments.unwrap_or(json!({}));

    // Execute the tool with timeout
    tracing::info!("[MCP] Calling execute_tool for: {}", params.name);
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(60),
        execute_tool(bridge, &params.name, args)
    ).await;

    let result = match result {
        Ok(r) => r,
        Err(_) => {
            tracing::error!("[MCP] Tool execution timed out after 60s");
            Err("Tool execution timed out".to_string())
        }
    };
    tracing::info!("[MCP] execute_tool returned: {:?}", result.is_ok());

    match result {
        Ok(tool_result) => {
            let content = if tool_result.success {
                serde_json::to_string_pretty(&tool_result.result).unwrap_or_default()
            } else {
                tool_result.error.unwrap_or_else(|| "Unknown error".to_string())
            };
            tracing::info!("[MCP] Tool result - success: {}, content: {}", tool_result.success, &content[..content.len().min(100)]);

            let call_result = ToolCallResult {
                content: vec![ToolContent {
                    content_type: "text".to_string(),
                    text: content,
                }],
                is_error: if tool_result.success { None } else { Some(true) },
            };

            JsonRpcResponse::success(id, serde_json::to_value(call_result).unwrap())
        }
        Err(e) => {
            tracing::error!("[MCP] Tool execution error: {}", e);
            let call_result = ToolCallResult {
                content: vec![ToolContent {
                    content_type: "text".to_string(),
                    text: e,
                }],
                is_error: Some(true),
            };

            JsonRpcResponse::success(id, serde_json::to_value(call_result).unwrap())
        }
    }
}

/// Send a plain text HTTP response
async fn send_text_response(
    writer: &mut tokio::net::tcp::WriteHalf<'_>,
    status: u16,
    body: &str,
) -> Result<(), String> {
    let status_text = match status {
        200 => "OK",
        400 => "Bad Request",
        404 => "Not Found",
        500 => "Internal Server Error",
        _ => "Unknown",
    };

    let response = format!(
        "HTTP/1.1 {} {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        status, status_text, body.len(), body
    );

    writer.write_all(response.as_bytes()).await.map_err(|e| e.to_string())?;
    writer.flush().await.map_err(|e| e.to_string())?;
    Ok(())
}

/// Send a JSON HTTP response
async fn send_json_response(
    writer: &mut tokio::net::tcp::WriteHalf<'_>,
    status: u16,
    body: &Value,
) -> Result<(), String> {
    let body_str = serde_json::to_string(body).unwrap_or_default();

    let status_text = match status {
        200 => "OK",
        202 => "Accepted",
        400 => "Bad Request",
        404 => "Not Found",
        500 => "Internal Server Error",
        _ => "Unknown",
    };

    let response = format!(
        "HTTP/1.1 {} {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        status, status_text, body_str.len(), body_str
    );

    writer.write_all(response.as_bytes()).await.map_err(|e| e.to_string())?;
    writer.flush().await.map_err(|e| e.to_string())?;
    Ok(())
}
