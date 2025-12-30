//! Unreal Engine Bridge Module
//!
//! This module provides real-time communication between Lapce and
//! Unreal Engine via WebSocket. It follows Lapce's existing architecture
//! patterns with a background Tokio runtime and reactive UI signals.
//!
//! ## Architecture
//!
//! ```text
//! UE Plugin (C++) --WebSocket 27020--> BridgeRuntime (Tokio thread)
//!                                            |
//!                                     crossbeam channels
//!                                            |
//!                                     BridgeData (RwSignals)
//!                                            |
//!                                     Status Bar UI / Plugin Banner
//! ```
//!
//! ## Modules
//!
//! - [`types`]: Data structures for bridge communication
//! - [`runtime`]: Background Tokio runtime with WebSocket server
//! - [`plugin`]: NeoStack plugin installation and version management
//! - [`view`]: UI components (status indicator, plugin banner)

mod plugin;
mod runtime;
mod types;
mod view;

// Re-export public API
pub use plugin::{
    bundled_plugin_path,
    check_plugin_version,
    find_uproject_file,
    install_plugin,
    is_unreal_project,
    uninstall_plugin,
};

pub use runtime::{
    BridgeRpcHandler,
    BridgeResponse,
    start_bridge_runtime,
};

pub use types::{
    BridgeCommand,
    BridgeEvent,
    BridgeNotification,
    BridgeStatus,
    HandshakeAck,
    HandshakeMessage,
    PluginStatus,
    PluginVersionInfo,
    UEClient,
    PROTOCOL_VERSION,
    WS_BASE_PORT,
    WS_PORT_ATTEMPTS,
    commands,
};

pub use view::{
    BridgePopoverData,
    bridge_popover_box,
    bridge_status_indicator,
    plugin_banner,
    simple_bridge_indicator,
};
