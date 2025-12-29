//! Bridge UI components: status bar indicator and plugin banner
//!
//! This module provides the visual components for Unreal Engine bridge integration:
//! - Status bar indicator showing connection status
//! - Plugin installation banner for prompting users to install/update

use std::sync::Arc;

use floem::{
    View,
    event::EventListener,
    reactive::{ReadSignal, RwSignal, SignalGet, SignalUpdate, SignalWith, create_memo},
    style::{AlignItems, CursorStyle, Display, Position},
    views::{Decorators, container, empty, label, stack, svg},
};

use crate::config::{LapceConfig, color::LapceColor, icon::LapceIcons};

use super::types::{BridgeStatus, PluginStatus, UEClient};

/// Status bar indicator for UE bridge connection
///
/// Shows "UE Connected" (green) or "UE Disconnected" (gray).
/// Clicking opens a popover with connection details and plugin actions.
pub fn bridge_status_indicator(
    config: ReadSignal<Arc<LapceConfig>>,
    bridge_status: RwSignal<BridgeStatus>,
    bridge_clients: RwSignal<Vec<UEClient>>,
    plugin_status: RwSignal<PluginStatus>,
    on_install_plugin: impl Fn() + 'static + Clone,
) -> impl View {
    let popover_visible = floem::reactive::create_rw_signal(false);

    let is_connected = create_memo(move |_| {
        bridge_status.get() == BridgeStatus::Connected
    });

    let client_count = create_memo(move |_| {
        bridge_clients.with(|c| c.len())
    });

    let status_text = create_memo(move |_| {
        match bridge_status.get() {
            BridgeStatus::Connected => {
                let count = client_count.get();
                if count > 1 {
                    format!("UE Connected ({})", count)
                } else {
                    "UE Connected".to_string()
                }
            }
            BridgeStatus::Listening => "UE Listening".to_string(),
            BridgeStatus::Stopped => "UE Disconnected".to_string(),
        }
    });

    let on_install = on_install_plugin.clone();

    stack((
        // Main indicator button
        stack((
            // Status dot
            empty().style(move |s| {
                let config = config.get();
                let color = if is_connected.get() {
                    config.color(LapceColor::TERMINAL_GREEN)
                } else {
                    config.color(LapceColor::EDITOR_DIM)
                };
                s.size(8.0, 8.0)
                    .border_radius(4.0)
                    .background(color)
                    .margin_right(6.0)
            }),
            // Status text
            label(move || status_text.get()).style(move |s| {
                s.color(config.get().color(LapceColor::STATUS_FOREGROUND))
                    .selectable(false)
            }),
        ))
        .on_click_stop(move |_| {
            popover_visible.update(|v| *v = !*v);
        })
        .style(move |s| {
            s.height_pct(100.0)
                .padding_horiz(10.0)
                .align_items(Some(AlignItems::Center))
                .cursor(CursorStyle::Pointer)
                .hover(|s| {
                    s.background(config.get().color(LapceColor::PANEL_HOVERED_BACKGROUND))
                })
        }),
        // Popover
        connection_popover(
            config,
            bridge_status,
            bridge_clients,
            plugin_status,
            popover_visible,
            on_install,
        ),
    ))
    .style(|s| s.position(Position::Relative))
}

/// Connection details popover
fn connection_popover(
    config: ReadSignal<Arc<LapceConfig>>,
    bridge_status: RwSignal<BridgeStatus>,
    bridge_clients: RwSignal<Vec<UEClient>>,
    plugin_status: RwSignal<PluginStatus>,
    visible: RwSignal<bool>,
    on_install_plugin: impl Fn() + 'static + Clone,
) -> impl View {
    let is_connected = create_memo(move |_| {
        bridge_status.get() == BridgeStatus::Connected
    });

    let on_install = on_install_plugin.clone();

    container(
        stack((
            // Header with status icon and message
            stack((
                // Status icon
                svg(move || {
                    if is_connected.get() {
                        config.get().ui_svg(LapceIcons::DEBUG_CONTINUE)
                    } else {
                        config.get().ui_svg(LapceIcons::DEBUG_DISCONNECT)
                    }
                }).style(move |s| {
                    let config = config.get();
                    let color = if is_connected.get() {
                        config.color(LapceColor::TERMINAL_GREEN)
                    } else {
                        config.color(LapceColor::EDITOR_DIM)
                    };
                    let size = config.ui.icon_size() as f32 + 4.0;
                    s.size(size, size)
                        .color(color)
                        .margin_right(10.0)
                }),
                // Status title
                label(move || {
                    match bridge_status.get() {
                        BridgeStatus::Connected => {
                            let count = bridge_clients.with(|c| c.len());
                            if count > 1 {
                                format!("Connected to {} Unreal Editors", count)
                            } else {
                                "Connected to Unreal Editor".to_string()
                            }
                        }
                        BridgeStatus::Listening => "Waiting for Unreal Editor".to_string(),
                        BridgeStatus::Stopped => "Bridge Not Running".to_string(),
                    }
                }).style(move |s| {
                    s.font_bold()
                        .color(config.get().color(LapceColor::EDITOR_FOREGROUND))
                }),
            ))
            .style(|s| s.align_items(Some(AlignItems::Center))),

            // Description/action area
            container(
                label(move || {
                    match (bridge_status.get(), plugin_status.get()) {
                        (BridgeStatus::Connected, _) => {
                            "Real-time sync and AI features are active.".to_string()
                        }
                        (_, PluginStatus::NotInstalled) => {
                            "Install the NeoStack plugin to enable UE integration.".to_string()
                        }
                        (_, PluginStatus::UpdateAvailable { installed_version, bundled_version }) => {
                            format!("Update available: v{} -> v{}", installed_version, bundled_version)
                        }
                        _ => {
                            "Open your project in Unreal Editor to connect.".to_string()
                        }
                    }
                }).style(move |s| {
                    s.color(config.get().color(LapceColor::EDITOR_DIM))
                })
            )
            .style(|s| s.margin_top(8.0)),

            // Install/Update button (if needed)
            {
                let on_install = on_install.clone();
                let visible_memo = visible;
                container(
                    label(move || {
                        match plugin_status.get() {
                            PluginStatus::NotInstalled => "Install Plugin".to_string(),
                            PluginStatus::UpdateAvailable { .. } => "Update Plugin".to_string(),
                            _ => String::new(),
                        }
                    })
                )
                .on_click_stop(move |_| {
                    on_install();
                    visible_memo.set(false);
                })
                .style(move |s| {
                    let show_button = matches!(
                        plugin_status.get(),
                        PluginStatus::NotInstalled | PluginStatus::UpdateAvailable { .. }
                    );
                    let config = config.get();

                    s.display(if show_button { Display::Flex } else { Display::None })
                        .margin_top(12.0)
                        .padding_horiz(16.0)
                        .padding_vert(6.0)
                        .border_radius(4.0)
                        .background(config.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND))
                        .color(config.color(LapceColor::LAPCE_BUTTON_PRIMARY_FOREGROUND))
                        .cursor(CursorStyle::Pointer)
                        .hover(|s| {
                            s.background(config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND))
                        })
                })
            },
        ))
        .style(|s| s.flex_col())
    )
    .on_event_stop(EventListener::PointerDown, |_| {
        // Prevent click from propagating
    })
    .style(move |s| {
        let config = config.get();
        s.display(if visible.get() { Display::Flex } else { Display::None })
            .position(Position::Absolute)
            .inset_bottom(30.0)
            .inset_left(-10.0)
            .padding(16.0)
            .min_width(280.0)
            .border(1.0)
            .border_radius(8.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .background(config.color(LapceColor::PANEL_BACKGROUND))
            .box_shadow_blur(8.0)
            .box_shadow_color(config.color(LapceColor::LAPCE_DROPDOWN_SHADOW))
            .z_index(100)
    })
}

/// Plugin installation/update banner
///
/// Fixed bottom-right card prompting users to install or update the plugin.
pub fn plugin_banner(
    config: ReadSignal<Arc<LapceConfig>>,
    plugin_status: RwSignal<PluginStatus>,
    show_banner: RwSignal<bool>,
    on_install_plugin: impl Fn() + 'static + Clone,
) -> impl View {
    let on_install = on_install_plugin.clone();

    let should_show = create_memo(move |_| {
        show_banner.get() && matches!(
            plugin_status.get(),
            PluginStatus::NotInstalled | PluginStatus::UpdateAvailable { .. }
        )
    });

    container(
        stack((
            // Close button (top-right)
            container(
                svg(move || config.get().ui_svg(LapceIcons::CLOSE))
                    .style(move |s| {
                        let config = config.get();
                        let size = config.ui.icon_size() as f32 - 2.0;
                        s.size(size, size)
                            .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                    })
            )
            .on_click_stop(move |_| {
                show_banner.set(false);
            })
            .style(move |s| {
                s.position(Position::Absolute)
                    .inset_top(8.0)
                    .inset_right(8.0)
                    .padding(4.0)
                    .border_radius(4.0)
                    .cursor(CursorStyle::Pointer)
                    .hover(|s| {
                        s.background(config.get().color(LapceColor::PANEL_HOVERED_BACKGROUND))
                    })
            }),

            // Icon
            svg(move || {
                match plugin_status.get() {
                    PluginStatus::NotInstalled => config.get().ui_svg(LapceIcons::EXTENSIONS),
                    PluginStatus::UpdateAvailable { .. } => config.get().ui_svg(LapceIcons::DEBUG_STEP_INTO),
                    _ => config.get().ui_svg(LapceIcons::EXTENSIONS),
                }
            }).style(move |s| {
                let config = config.get();
                let color = match plugin_status.get() {
                    PluginStatus::NotInstalled => config.color(LapceColor::TERMINAL_YELLOW),
                    PluginStatus::UpdateAvailable { .. } => config.color(LapceColor::TERMINAL_BLUE),
                    _ => config.color(LapceColor::LAPCE_ICON_ACTIVE),
                };
                s.size(32.0, 32.0)
                    .color(color)
                    .margin_bottom(12.0)
            }),

            // Title
            label(move || {
                match plugin_status.get() {
                    PluginStatus::NotInstalled => "NeoStack Plugin Required".to_string(),
                    PluginStatus::UpdateAvailable { installed_version, bundled_version } => {
                        format!("Plugin Update v{} -> v{}", installed_version, bundled_version)
                    }
                    _ => String::new(),
                }
            }).style(move |s| {
                s.font_bold()
                    .color(config.get().color(LapceColor::EDITOR_FOREGROUND))
            }),

            // Description
            label(move || {
                match plugin_status.get() {
                    PluginStatus::NotInstalled => {
                        "Install the NeoStack plugin to enable real-time \
                         communication with Unreal Engine."
                            .to_string()
                    }
                    PluginStatus::UpdateAvailable { .. } => {
                        "Update to get the latest features and bug fixes.".to_string()
                    }
                    _ => String::new(),
                }
            }).style(move |s| {
                s.margin_top(4.0)
                    .max_width(260.0)
                    .color(config.get().color(LapceColor::EDITOR_DIM))
            }),

            // Install/Update button
            {
                let on_install = on_install.clone();
                container(
                    stack((
                        svg(move || config.get().ui_svg(LapceIcons::DEBUG_STEP_INTO))
                            .style(move |s| {
                                let config = config.get();
                                let size = config.ui.icon_size() as f32;
                                s.size(size, size)
                                    .margin_right(6.0)
                                    .color(config.color(LapceColor::LAPCE_BUTTON_PRIMARY_FOREGROUND))
                            }),
                        label(move || {
                            match plugin_status.get() {
                                PluginStatus::NotInstalled => "Install Plugin".to_string(),
                                PluginStatus::UpdateAvailable { .. } => "Update Plugin".to_string(),
                                _ => String::new(),
                            }
                        }),
                    ))
                    .style(|s| s.align_items(Some(AlignItems::Center)))
                )
                .on_click_stop(move |_| {
                    on_install();
                })
                .style(move |s| {
                    let config = config.get();
                    s.margin_top(16.0)
                        .padding_horiz(16.0)
                        .padding_vert(8.0)
                        .border_radius(4.0)
                        .background(config.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND))
                        .color(config.color(LapceColor::LAPCE_BUTTON_PRIMARY_FOREGROUND))
                        .cursor(CursorStyle::Pointer)
                        .hover(|s| {
                            s.background(config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND))
                        })
                })
            },
        ))
        .style(|s| s.flex_col().items_center().padding_top(8.0))
    )
    .style(move |s| {
        let config = config.get();
        s.display(if should_show.get() { Display::Flex } else { Display::None })
            .position(Position::Absolute)
            .inset_right(16.0)
            .inset_bottom(40.0) // Above status bar
            .padding(20.0)
            .width(320.0)
            .border(1.0)
            .border_radius(8.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .background(config.color(LapceColor::PANEL_BACKGROUND))
            .box_shadow_blur(12.0)
            .box_shadow_color(config.color(LapceColor::LAPCE_DROPDOWN_SHADOW))
            .z_index(50)
    })
    .debug_name("Plugin Banner")
}

/// Helper: Create a simple bridge status indicator without popover
/// For simpler use cases where full functionality isn't needed
pub fn simple_bridge_indicator(
    config: ReadSignal<Arc<LapceConfig>>,
    is_connected: impl Fn() -> bool + 'static + Copy,
) -> impl View {
    stack((
        empty().style(move |s| {
            let config = config.get();
            let color = if is_connected() {
                config.color(LapceColor::TERMINAL_GREEN)
            } else {
                config.color(LapceColor::EDITOR_DIM)
            };
            s.size(8.0, 8.0)
                .border_radius(4.0)
                .background(color)
                .margin_right(6.0)
        }),
        label(move || {
            if is_connected() {
                "UE Connected"
            } else {
                "UE Disconnected"
            }
        }).style(move |s| {
            s.color(config.get().color(LapceColor::STATUS_FOREGROUND))
                .selectable(false)
        }),
    ))
    .style(move |s| {
        s.height_pct(100.0)
            .padding_horiz(10.0)
            .align_items(Some(AlignItems::Center))
    })
}
