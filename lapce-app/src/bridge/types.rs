//! Types for Unreal Engine bridge integration
//!
//! This module defines all data structures for WebSocket communication
//! between Lapce and the Unreal Engine NeoStack plugin.

use serde::{Deserialize, Serialize};

/// Protocol version for WebSocket bridge communication
/// Must match the NeoStack UE plugin's PROTOCOL_VERSION
pub const PROTOCOL_VERSION: i32 = 2;

/// Base port for WebSocket server
pub const WS_BASE_PORT: u16 = 27020;

/// Number of port fallback attempts (27020-27029)
pub const WS_PORT_ATTEMPTS: u16 = 10;

/// Bridge connection status
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum BridgeStatus {
    /// Bridge server is not running
    #[default]
    Stopped,
    /// Bridge server is listening for connections
    Listening,
    /// One or more UE clients are connected
    Connected,
}

/// Plugin installation status
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum PluginStatus {
    /// Plugin check not yet performed
    #[default]
    Unknown,
    /// Plugin is not installed in the project
    NotInstalled,
    /// Plugin is installed and up to date
    Installed {
        version: String,
    },
    /// Plugin is installed but an update is available
    UpdateAvailable {
        installed_version: String,
        bundled_version: String,
    },
}

/// Information about a connected Unreal Engine client
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UEClient {
    /// Unique session ID assigned by the bridge
    pub session_id: String,
    /// Unique project identifier
    pub project_id: String,
    /// Project display name
    pub project_name: String,
    /// Full path to the project directory
    pub project_path: String,
    /// Unreal Engine version (e.g., "5.4.0")
    pub engine_version: String,
    /// Process ID of the Unreal Editor
    pub pid: i32,
    /// Unix timestamp when the client connected
    pub connected_at: u64,
}

/// Handshake message sent by UE Plugin on connect
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HandshakeMessage {
    /// Message type - should be "handshake"
    #[serde(rename = "type")]
    pub msg_type: String,
    /// Protocol version - must match PROTOCOL_VERSION
    pub version: i32,
    /// Unique project identifier
    pub project_id: String,
    /// Full path to the project directory
    pub project_path: String,
    /// Project display name
    pub project_name: String,
    /// Unreal Engine version
    pub engine_version: String,
    /// Process ID of the Unreal Editor
    pub pid: i32,
}

impl HandshakeMessage {
    /// Check if this handshake is valid
    pub fn is_valid(&self) -> bool {
        self.msg_type == "handshake" && self.version == PROTOCOL_VERSION
    }
}

/// Handshake acknowledgment sent by IDE to UE Plugin
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HandshakeAck {
    /// Message type - always "handshake_ack"
    #[serde(rename = "type")]
    pub msg_type: String,
    /// Assigned session ID (UUID)
    pub session_id: String,
    /// Whether the handshake was successful
    pub success: bool,
    /// Error message if handshake failed
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

impl HandshakeAck {
    /// Create a successful handshake acknowledgment
    pub fn success(session_id: String) -> Self {
        Self {
            msg_type: "handshake_ack".to_string(),
            session_id,
            success: true,
            error: None,
        }
    }

    /// Create a failed handshake acknowledgment
    pub fn failure(error: impl Into<String>) -> Self {
        Self {
            msg_type: "handshake_ack".to_string(),
            session_id: String::new(),
            success: false,
            error: Some(error.into()),
        }
    }
}

/// Command sent from IDE to UE Plugin
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BridgeCommand {
    /// Command name (e.g., "pie_start", "hot_reload", "execute_tool")
    pub cmd: String,
    /// Unique request ID for correlating responses
    pub request_id: String,
    /// Optional command arguments
    #[serde(skip_serializing_if = "Option::is_none")]
    pub args: Option<serde_json::Value>,
}

/// Response/Event from UE Plugin
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BridgeEvent {
    /// Event type or response name
    pub event: String,
    /// Whether the operation was successful
    pub success: bool,
    /// Correlating request ID (for command responses)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub request_id: Option<String>,
    /// Error message if operation failed
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    /// Event data payload
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<serde_json::Value>,
}

/// Plugin version information from .uplugin file
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginVersionInfo {
    /// Whether the plugin is installed in the project
    pub installed: bool,
    /// Numeric version of installed plugin (from .uplugin Version field)
    pub installed_version: Option<i32>,
    /// Version name of installed plugin (from .uplugin VersionName field)
    pub installed_version_name: Option<String>,
    /// Numeric version of bundled plugin
    pub bundled_version: i32,
    /// Version name of bundled plugin
    pub bundled_version_name: String,
    /// Whether an update is available
    pub update_available: bool,
}

/// Notifications from bridge runtime to UI
///
/// These are sent via crossbeam channel from the background
/// Tokio runtime to the main UI thread.
#[derive(Debug, Clone)]
pub enum BridgeNotification {
    /// WebSocket server started listening
    ServerStarted {
        /// The port the server is listening on
        port: u16,
    },
    /// WebSocket server stopped
    ServerStopped,
    /// A new UE client connected
    ClientConnected(UEClient),
    /// A UE client disconnected
    ClientDisconnected {
        /// Session ID of the disconnected client
        session_id: String,
    },
    /// Response to a command
    CommandResponse(BridgeEvent),
    /// An error occurred in the bridge
    Error {
        /// Error message
        message: String,
    },
}

/// Standard bridge commands
pub mod commands {
    /// Start Play In Editor
    pub const PIE_START: &str = "pie_start";
    /// Stop Play In Editor
    pub const PIE_STOP: &str = "pie_stop";
    /// Trigger hot reload / live coding
    pub const HOT_RELOAD: &str = "hot_reload";
    /// Start viewport streaming (Pixel Streaming 2)
    pub const START_STREAMING: &str = "start_streaming";
    /// Stop viewport streaming
    pub const STOP_STREAMING: &str = "stop_streaming";
    /// Execute a tool in UE
    pub const EXECUTE_TOOL: &str = "execute_tool";
    /// Open an asset in the editor
    pub const OPEN_ASSET: &str = "OpenAsset";
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_handshake_validation() {
        let valid = HandshakeMessage {
            msg_type: "handshake".to_string(),
            version: PROTOCOL_VERSION,
            project_id: "test".to_string(),
            project_path: "/path/to/project".to_string(),
            project_name: "TestProject".to_string(),
            engine_version: "5.4.0".to_string(),
            pid: 1234,
        };
        assert!(valid.is_valid());

        let invalid_type = HandshakeMessage {
            msg_type: "wrong".to_string(),
            ..valid.clone()
        };
        assert!(!invalid_type.is_valid());

        let invalid_version = HandshakeMessage {
            version: 999,
            ..valid
        };
        assert!(!invalid_version.is_valid());
    }

    #[test]
    fn test_handshake_ack() {
        let success = HandshakeAck::success("session-123".to_string());
        assert!(success.success);
        assert!(success.error.is_none());

        let failure = HandshakeAck::failure("Version mismatch");
        assert!(!failure.success);
        assert_eq!(failure.error, Some("Version mismatch".to_string()));
    }
}
