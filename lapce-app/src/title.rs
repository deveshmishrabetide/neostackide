use std::{rc::Rc, sync::Arc};

use floem::{
    View,
    event::EventListener,
    menu::{Menu, MenuItem},
    peniko::Color,
    reactive::{Memo, ReadSignal, RwSignal, SignalGet, SignalUpdate, SignalWith, create_memo},
    style::{AlignItems, CursorStyle, JustifyContent},
    views::{Decorators, container, drag_window_area, empty, label, stack, svg},
};
use lapce_core::meta;
use lapce_rpc::proxy::ProxyStatus;

// Re-export build types for convenience
pub use crate::build::{BuildConfig, BuildState, BuildTarget};

/// View modes for the IDE
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum ViewMode {
    Agent,
    #[default]
    Ide,
    DevOps,
}

use crate::{
    app::{clickable_icon, not_clickable_icon, tooltip_label, window_menu},
    command::{LapceCommand, LapceWorkbenchCommand, WindowCommand},
    config::{LapceConfig, color::LapceColor, icon::LapceIcons},
    listener::Listener,
    main_split::MainSplitData,
    update::ReleaseInfo,
    window_tab::WindowTabData,
    workspace::LapceWorkspace,
};

/// View mode selector button (Agent | IDE | DevOps)
fn view_mode_button(
    mode: ViewMode,
    label_text: &'static str,
    icon_name: &'static str,
    view_mode: RwSignal<ViewMode>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    stack((
        svg(move || config.get().ui_svg(icon_name)).style(move |s| {
            let config = config.get();
            let icon_size = config.ui.icon_size() as f32;
            s.size(icon_size, icon_size)
                .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
        }),
        label(move || label_text.to_string())
            .style(|s| s.margin_left(4.0).selectable(false).font_size(12.0)),
    ))
    .on_click_stop(move |_| {
        view_mode.set(mode);
    })
    .style(move |s| {
        let config = config.get();
        let is_active = view_mode.get() == mode;
        s.padding_horiz(10.0)
            .padding_vert(4.0)
            .align_items(Some(AlignItems::Center))
            .border_radius(4.0)
            .cursor(CursorStyle::Pointer)
            .apply_if(is_active, |s| {
                s.background(config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND))
            })
            .hover(|s| {
                s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
            })
    })
}

/// View mode selector (Agent | IDE | DevOps)
fn view_mode_selector(
    view_mode: RwSignal<ViewMode>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    stack((
        view_mode_button(ViewMode::Agent, "Agent", LapceIcons::DEBUG_ALT, view_mode, config),
        view_mode_button(ViewMode::Ide, "IDE", LapceIcons::FILE, view_mode, config),
        view_mode_button(ViewMode::DevOps, "DevOps", LapceIcons::SCM, view_mode, config),
    ))
    .style(move |s| {
        let config = config.get();
        s.align_items(Some(AlignItems::Center))
            .padding_horiz(4.0)
            .padding_vert(2.0)
            .border(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .border_radius(6.0)
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
}

/// Dropdown selector button for target/config
fn dropdown_button(
    label_text: impl Fn() -> String + 'static,
    width: f64,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    stack((
        label(label_text)
            .style(|s| s.selectable(false).font_size(12.0).flex_grow(1.0)),
        svg(move || config.get().ui_svg(LapceIcons::ITEM_OPENED)).style(move |s| {
            let config = config.get();
            s.size(10.0, 10.0)
                .margin_left(4.0)
                .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
        }),
    ))
    .style(move |s| {
        let config = config.get();
        s.width(width)
            .padding_horiz(8.0)
            .padding_vert(4.0)
            .align_items(Some(AlignItems::Center))
            .border(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .border_radius(4.0)
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
            .cursor(CursorStyle::Pointer)
            .hover(|s| {
                s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
            })
    })
}

/// Toolbar button (build/run/debug) - reactive version
fn toolbar_button_reactive<F, G, H, T>(
    icon: F,
    tooltip: G,
    color: Option<fn(&LapceConfig) -> Color>,
    disabled: H,
    on_click: impl Fn() + 'static,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View
where
    F: Fn() -> &'static str + 'static,
    G: Fn() -> T + 'static + Clone,
    H: Fn() -> bool + 'static + Clone,
    T: std::fmt::Display + 'static,
{
    let disabled_for_style = disabled.clone();
    let disabled_for_click = disabled.clone();
    tooltip_label(
        config,
        container(svg(move || config.get().ui_svg(icon())).style(move |s| {
            let config = config.get();
            let is_disabled = disabled();
            let icon_size = config.ui.icon_size() as f32;
            let icon_color = if is_disabled {
                config.color(LapceColor::LAPCE_ICON_ACTIVE).with_alpha(0.5)
            } else if let Some(color_fn) = color {
                color_fn(&config)
            } else {
                config.color(LapceColor::LAPCE_ICON_ACTIVE)
            };
            s.size(icon_size, icon_size).color(icon_color)
        })),
        tooltip,
    )
    .on_click_stop(move |_| {
        if !disabled_for_click() {
            on_click();
        }
    })
    .style(move |s| {
        let config = config.get();
        let is_disabled = disabled_for_style();
        s.padding(6.0)
            .border_radius(4.0)
            .cursor(if is_disabled { CursorStyle::Default } else { CursorStyle::Pointer })
            .apply_if(!is_disabled, |s| {
                s.hover(|s| {
                    s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
                })
                .active(|s| {
                    s.background(config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND))
                })
            })
    })
}

/// Toolbar button (build/run/debug)
fn toolbar_button(
    icon: fn() -> &'static str,
    tooltip: &'static str,
    color: Option<fn(&LapceConfig) -> Color>,
    disabled: bool,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    tooltip_label(
        config,
        container(svg(move || config.get().ui_svg(icon())).style(move |s| {
            let config = config.get();
            let icon_size = config.ui.icon_size() as f32;
            let icon_color = if disabled {
                config.color(LapceColor::LAPCE_ICON_ACTIVE).with_alpha(0.5)
            } else if let Some(color_fn) = color {
                color_fn(&config)
            } else {
                config.color(LapceColor::LAPCE_ICON_ACTIVE)
            };
            s.size(icon_size, icon_size).color(icon_color)
        })),
        move || tooltip,
    )
    .style(move |s| {
        let config = config.get();
        s.padding(6.0)
            .border_radius(4.0)
            .cursor(if disabled { CursorStyle::Default } else { CursorStyle::Pointer })
            .apply_if(!disabled, |s| {
                s.hover(|s| {
                    s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
                })
                .active(|s| {
                    s.background(config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND))
                })
            })
    })
}

/// Unreal Engine toolbar (target, config, build/run/debug)
fn unreal_toolbar(window_tab_data: Rc<WindowTabData>) -> impl View {
    let build_target = window_tab_data.build_target;
    let build_config = window_tab_data.build_config;
    let build_targets = window_tab_data.build_targets;
    let build_state = window_tab_data.build_state;
    let config = window_tab_data.common.config;

    // Clone WindowTabData for closures
    let wtd_build = window_tab_data.clone();
    let wtd_run = window_tab_data.clone();

    stack((
        // Target selector - populated from discovered targets
        dropdown_button(
            move || {
                let target = build_target.get();
                if target.is_empty() {
                    "Select Target".to_string()
                } else {
                    target
                }
            },
            120.0,
            config,
        )
        .popout_menu(move || {
            let targets = build_targets.get();
            let mut menu = Menu::new("");

            if targets.is_empty() {
                menu = menu.entry(MenuItem::new("No targets found").enabled(false));
            } else {
                for target in targets {
                    let name = target.name.clone();
                    let target_type = target.target_type.clone();
                    let display = if target_type.is_empty() {
                        name.clone()
                    } else {
                        format!("{} ({})", name, target_type)
                    };
                    let name_for_action = name.clone();
                    menu = menu.entry(MenuItem::new(display).action(move || {
                        build_target.set(name_for_action.clone());
                    }));
                }
            }
            menu
        }),
        // Config selector
        dropdown_button(
            move || build_config.get().as_str().to_string(),
            100.0,
            config,
        )
        .popout_menu(move || {
            Menu::new("")
                .entry(MenuItem::new("Development").action(move || {
                    build_config.set(BuildConfig::Development);
                }))
                .entry(MenuItem::new("DebugGame").action(move || {
                    build_config.set(BuildConfig::DebugGame);
                }))
                .entry(MenuItem::new("Test").action(move || {
                    build_config.set(BuildConfig::Test);
                }))
                .entry(MenuItem::new("Shipping").action(move || {
                    build_config.set(BuildConfig::Shipping);
                }))
        })
        .style(|s| s.margin_left(6.0)),
        // Separator
        container(empty()).style(move |s| {
            let config = config.get();
            s.width(1.0)
                .height(20.0)
                .margin_horiz(8.0)
                .background(config.color(LapceColor::LAPCE_BORDER))
        }),
        // Build button - toggles between build and cancel based on state
        toolbar_button_reactive(
            move || {
                match build_state.get() {
                    BuildState::Building | BuildState::Cancelling => LapceIcons::DEBUG_STOP,
                    _ => LapceIcons::DEBUG_RESTART,
                }
            },
            move || {
                match build_state.get() {
                    BuildState::Building => "Cancel Build",
                    BuildState::Cancelling => "Cancelling...",
                    _ => "Build (Ctrl+B)",
                }
            },
            Some(|c: &LapceConfig| c.color(LapceColor::LAPCE_ICON_ACTIVE)),
            move || build_state.get() == BuildState::Cancelling,
            move || {
                let state = wtd_build.build_state.get_untracked();
                match state {
                    BuildState::Building => wtd_build.cancel_build(),
                    BuildState::Idle => wtd_build.start_build(),
                    _ => {}
                }
            },
            config,
        ),
        // Run button - toggles between run and stop based on state
        toolbar_button_reactive(
            move || {
                match build_state.get() {
                    BuildState::Running => LapceIcons::DEBUG_STOP,
                    _ => LapceIcons::START,
                }
            },
            move || {
                match build_state.get() {
                    BuildState::Running => "Stop",
                    BuildState::Building => "Building...",
                    _ => "Run",
                }
            },
            Some(|_c: &LapceConfig| Color::from_rgb8(34, 197, 94)), // green
            move || build_state.get() == BuildState::Building || build_state.get() == BuildState::Cancelling,
            move || {
                let state = wtd_run.build_state.get_untracked();
                match state {
                    BuildState::Running => wtd_run.stop_running(),
                    BuildState::Idle => wtd_run.run_project(),
                    _ => {}
                }
            },
            config,
        )
        .style(|s| s.margin_left(2.0)),
        // Debug button (disabled for now)
        toolbar_button(
            || LapceIcons::DEBUG,
            "Debug (F5)",
            None,
            true, // disabled for now
            config,
        )
        .style(|s| s.margin_left(2.0)),
    ))
    .style(|s| s.align_items(Some(AlignItems::Center)))
}

fn left(
    workspace: Arc<LapceWorkspace>,
    lapce_command: Listener<LapceCommand>,
    workbench_command: Listener<LapceWorkbenchCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
    proxy_status: RwSignal<Option<ProxyStatus>>,
    num_window_tabs: Memo<usize>,
) -> impl View {
    let is_local = workspace.kind.is_local();
    let is_macos = cfg!(target_os = "macos");
    stack((
        empty().style(move |s| {
            let should_hide = if is_macos {
                num_window_tabs.get() > 1
            } else {
                true
            };
            s.width(75.0).apply_if(should_hide, |s| s.hide())
        }),
        container(svg(move || config.get().ui_svg(LapceIcons::LOGO)).style(
            move |s| {
                let config = config.get();
                s.size(16.0, 16.0)
                    .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
            },
        ))
        .style(move |s| s.margin_horiz(10.0).apply_if(is_macos, |s| s.hide())),
        not_clickable_icon(
            || LapceIcons::MENU,
            || false,
            || false,
            || "Menu",
            config,
        )
        .popout_menu(move || window_menu(lapce_command, workbench_command))
        .style(move |s| {
            s.margin_left(4.0)
                .margin_right(6.0)
                .apply_if(is_macos, |s| s.hide())
        }),
        tooltip_label(
            config,
            container(svg(move || config.get().ui_svg(LapceIcons::REMOTE)).style(
                move |s| {
                    let config = config.get();
                    let size = (config.ui.icon_size() as f32 + 2.0).min(30.0);
                    s.size(size, size).color(if is_local {
                        config.color(LapceColor::LAPCE_ICON_ACTIVE)
                    } else {
                        match proxy_status.get() {
                            Some(_) => Color::WHITE,
                            None => config.color(LapceColor::LAPCE_ICON_ACTIVE),
                        }
                    })
                },
            )),
            || "Connect to Remote",
        )
        .popout_menu(move || {
            #[allow(unused_mut)]
            let mut menu = Menu::new("").entry(
                MenuItem::new("Connect to SSH Host").action(move || {
                    workbench_command.send(LapceWorkbenchCommand::ConnectSshHost);
                }),
            );
            if !is_local
                && proxy_status.get().is_some_and(|p| {
                    matches!(p, ProxyStatus::Connecting | ProxyStatus::Connected)
                })
            {
                menu = menu.entry(MenuItem::new("Disconnect remote").action(
                    move || {
                        workbench_command
                            .send(LapceWorkbenchCommand::DisconnectRemote);
                    },
                ));
            }
            #[cfg(windows)]
            {
                menu = menu.entry(MenuItem::new("Connect to WSL Host").action(
                    move || {
                        workbench_command
                            .send(LapceWorkbenchCommand::ConnectWslHost);
                    },
                ));
            }
            menu
        })
        .style(move |s| {
            let config = config.get();
            let color = if is_local {
                Color::TRANSPARENT
            } else {
                match proxy_status.get() {
                    Some(ProxyStatus::Connected) => {
                        config.color(LapceColor::LAPCE_REMOTE_CONNECTED)
                    }
                    Some(ProxyStatus::Connecting) => {
                        config.color(LapceColor::LAPCE_REMOTE_CONNECTING)
                    }
                    Some(ProxyStatus::Disconnected) => {
                        config.color(LapceColor::LAPCE_REMOTE_DISCONNECTED)
                    }
                    None => Color::TRANSPARENT,
                }
            };
            s.height_pct(100.0)
                .padding_horiz(10.0)
                .items_center()
                .background(color)
                .hover(|s| {
                    s.cursor(CursorStyle::Pointer).background(
                        config.color(LapceColor::PANEL_HOVERED_BACKGROUND),
                    )
                })
                .active(|s| {
                    s.cursor(CursorStyle::Pointer).background(
                        config.color(LapceColor::PANEL_HOVERED_ACTIVE_BACKGROUND),
                    )
                })
        }),
        drag_window_area(empty())
            .style(|s| s.height_pct(100.0).flex_basis(0.0).flex_grow(1.0)),
    ))
    .style(move |s| {
        s.height_pct(100.0)
            .flex_basis(0.0)
            .flex_grow(1.0)
            .items_center()
    })
    .debug_name("Left Side of Top Bar")
}

fn middle(
    workspace: Arc<LapceWorkspace>,
    main_split: MainSplitData,
    workbench_command: Listener<LapceWorkbenchCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let local_workspace = workspace.clone();
    let can_jump_backward = {
        let main_split = main_split.clone();
        create_memo(move |_| main_split.can_jump_location_backward(true))
    };
    let can_jump_forward =
        create_memo(move |_| main_split.can_jump_location_forward(true));

    let jump_backward = move || {
        clickable_icon(
            || LapceIcons::LOCATION_BACKWARD,
            move || {
                workbench_command.send(LapceWorkbenchCommand::JumpLocationBackward);
            },
            || false,
            move || !can_jump_backward.get(),
            || "Jump Backward",
            config,
        )
        .style(move |s| s.margin_horiz(6.0))
    };
    let jump_forward = move || {
        clickable_icon(
            || LapceIcons::LOCATION_FORWARD,
            move || {
                workbench_command.send(LapceWorkbenchCommand::JumpLocationForward);
            },
            || false,
            move || !can_jump_forward.get(),
            || "Jump Forward",
            config,
        )
        .style(move |s| s.margin_right(6.0))
    };

    let open_folder = move || {
        not_clickable_icon(
            || LapceIcons::PALETTE_MENU,
            || false,
            || false,
            || "Open Folder / Recent Workspace",
            config,
        )
        .popout_menu(move || {
            Menu::new("")
                .entry(MenuItem::new("Open Folder").action(move || {
                    workbench_command.send(LapceWorkbenchCommand::OpenFolder);
                }))
                .entry(MenuItem::new("Open Recent Workspace").action(move || {
                    workbench_command.send(LapceWorkbenchCommand::PaletteWorkspace);
                }))
        })
    };

    stack((
        stack((
            drag_window_area(empty())
                .style(|s| s.height_pct(100.0).flex_basis(0.0).flex_grow(1.0)),
            jump_backward(),
            jump_forward(),
        ))
        .style(|s| {
            s.flex_basis(0)
                .flex_grow(1.0)
                .justify_content(Some(JustifyContent::FlexEnd))
        }),
        container(
            stack((
                svg(move || config.get().ui_svg(LapceIcons::SEARCH)).style(
                    move |s| {
                        let config = config.get();
                        let icon_size = config.ui.icon_size() as f32;
                        s.size(icon_size, icon_size)
                            .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                    },
                ),
                label(move || {
                    if let Some(s) = local_workspace.display() {
                        s
                    } else {
                        "Open Folder".to_string()
                    }
                })
                .style(|s| s.padding_left(10).padding_right(5).selectable(false)),
                open_folder(),
            ))
            .style(|s| s.align_items(Some(AlignItems::Center))),
        )
        .on_event_stop(EventListener::PointerDown, |_| {})
        .on_click_stop(move |_| {
            if workspace.clone().path.is_some() {
                workbench_command.send(LapceWorkbenchCommand::PaletteHelpAndFile);
            } else {
                workbench_command.send(LapceWorkbenchCommand::PaletteWorkspace);
            }
        })
        .style(move |s| {
            let config = config.get();
            s.flex_basis(0)
                .flex_grow(10.0)
                .min_width(200.0)
                .max_width(500.0)
                .height(26.0)
                .justify_content(Some(JustifyContent::Center))
                .align_items(Some(AlignItems::Center))
                .border(1.0)
                .border_color(config.color(LapceColor::LAPCE_BORDER))
                .border_radius(6.0)
                .background(config.color(LapceColor::EDITOR_BACKGROUND))
        }),
        stack((
            clickable_icon(
                || LapceIcons::START,
                move || {
                    workbench_command.send(LapceWorkbenchCommand::PaletteRunAndDebug)
                },
                || false,
                || false,
                || "Run and Debug",
                config,
            )
            .style(move |s| s.margin_horiz(6.0)),
            drag_window_area(empty())
                .style(|s| s.height_pct(100.0).flex_basis(0.0).flex_grow(1.0)),
        ))
        .style(move |s| {
            s.flex_basis(0)
                .flex_grow(1.0)
                .justify_content(Some(JustifyContent::FlexStart))
        }),
    ))
    .style(|s| {
        s.flex_basis(0)
            .flex_grow(2.0)
            .align_items(Some(AlignItems::Center))
            .justify_content(Some(JustifyContent::Center))
    })
    .debug_name("Middle of Top Bar")
}

fn right(
    window_command: Listener<WindowCommand>,
    workbench_command: Listener<LapceWorkbenchCommand>,
    latest_release: ReadSignal<Arc<Option<ReleaseInfo>>>,
    update_in_progress: RwSignal<bool>,
    num_window_tabs: Memo<usize>,
    window_maximized: RwSignal<bool>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let latest_version = create_memo(move |_| {
        let latest_release = latest_release.get();
        let latest_version =
            latest_release.as_ref().as_ref().map(|r| r.version.clone());
        if latest_version.is_some()
            && latest_version.as_deref() != Some(meta::VERSION)
        {
            latest_version
        } else {
            None
        }
    });

    let has_update = move || latest_version.with(|v| v.is_some());

    stack((
        drag_window_area(empty())
            .style(|s| s.height_pct(100.0).flex_basis(0.0).flex_grow(1.0)),
        stack((
            not_clickable_icon(
                || LapceIcons::SETTINGS,
                || false,
                || false,
                || "Settings",
                config,
            )
            .popout_menu(move || {
                Menu::new("")
                    .entry(MenuItem::new("Command Palette").action(move || {
                        workbench_command.send(LapceWorkbenchCommand::PaletteCommand)
                    }))
                    .separator()
                    .entry(MenuItem::new("Open Settings").action(move || {
                        workbench_command.send(LapceWorkbenchCommand::OpenSettings)
                    }))
                    .entry(MenuItem::new("Open Keyboard Shortcuts").action(
                        move || {
                            workbench_command
                                .send(LapceWorkbenchCommand::OpenKeyboardShortcuts)
                        },
                    ))
                    .entry(MenuItem::new("Open Theme Color Settings").action(
                        move || {
                            workbench_command
                                .send(LapceWorkbenchCommand::OpenThemeColorSettings)
                        },
                    ))
                    .separator()
                    .entry(if let Some(v) = latest_version.get_untracked() {
                        if update_in_progress.get_untracked() {
                            MenuItem::new(format!("Update in progress ({v})"))
                                .enabled(false)
                        } else {
                            MenuItem::new(format!("Restart to update ({v})")).action(
                                move || {
                                    workbench_command
                                        .send(LapceWorkbenchCommand::RestartToUpdate)
                                },
                            )
                        }
                    } else {
                        MenuItem::new("No update available").enabled(false)
                    })
                    .separator()
                    .entry(MenuItem::new("About Lapce").action(move || {
                        workbench_command.send(LapceWorkbenchCommand::ShowAbout)
                    }))
            }),
            container(label(|| "1".to_string()).style(move |s| {
                let config = config.get();
                s.font_size(10.0)
                    .color(config.color(LapceColor::EDITOR_BACKGROUND))
                    .border_radius(100.0)
                    .margin_left(5.0)
                    .margin_top(10.0)
                    .background(config.color(LapceColor::EDITOR_CARET))
            }))
            .style(move |s| {
                let has_update = has_update();
                s.absolute()
                    .size_pct(100.0, 100.0)
                    .justify_end()
                    .items_end()
                    .pointer_events_none()
                    .apply_if(!has_update, |s| s.hide())
            }),
        ))
        .style(move |s| s.margin_horiz(6.0)),
        window_controls_view(
            window_command,
            true,
            num_window_tabs,
            window_maximized,
            config,
        ),
    ))
    .style(|s| {
        s.flex_basis(0)
            .flex_grow(1.0)
            .justify_content(Some(JustifyContent::FlexEnd))
    })
    .debug_name("Right of top bar")
}

pub fn title(window_tab_data: Rc<WindowTabData>) -> impl View {
    let workspace = window_tab_data.workspace.clone();
    let lapce_command = window_tab_data.common.lapce_command;
    let workbench_command = window_tab_data.common.workbench_command;
    let window_command = window_tab_data.common.window_common.window_command;
    let latest_release = window_tab_data.common.window_common.latest_release;
    let proxy_status = window_tab_data.common.proxy_status;
    let num_window_tabs = window_tab_data.common.window_common.num_window_tabs;
    let window_maximized = window_tab_data.common.window_common.window_maximized;
    let title_height = window_tab_data.title_height;
    let update_in_progress = window_tab_data.update_in_progress;
    let config = window_tab_data.common.config;

    let view_mode = window_tab_data.view_mode;

    stack((
        left(
            workspace.clone(),
            lapce_command,
            workbench_command,
            config,
            proxy_status,
            num_window_tabs,
        ),
        view_mode_selector(view_mode, config),
        middle(
            workspace,
            window_tab_data.main_split.clone(),
            workbench_command,
            config,
        ),
        unreal_toolbar(window_tab_data.clone()),
        right(
            window_command,
            workbench_command,
            latest_release,
            update_in_progress,
            num_window_tabs,
            window_maximized,
            config,
        ),
    ))
    .on_resize(move |rect| {
        let height = rect.height();
        if height != title_height.get_untracked() {
            title_height.set(height);
        }
    })
    .style(move |s| {
        let config = config.get();
        s.width_pct(100.0)
            .height(42.0)
            .items_center()
            .background(config.color(LapceColor::PANEL_BACKGROUND))
            .border_bottom(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
    })
    .debug_name("Title / Top Bar")
}

pub fn window_controls_view(
    window_command: Listener<WindowCommand>,
    is_title: bool,
    num_window_tabs: Memo<usize>,
    window_maximized: RwSignal<bool>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    stack((
        clickable_icon(
            || LapceIcons::WINDOW_MINIMIZE,
            || {
                floem::action::minimize_window();
            },
            || false,
            || false,
            || "Minimize",
            config,
        )
        .style(|s| s.margin_right(16.0).margin_left(10.0)),
        clickable_icon(
            move || {
                if window_maximized.get() {
                    LapceIcons::WINDOW_RESTORE
                } else {
                    LapceIcons::WINDOW_MAXIMIZE
                }
            },
            move || {
                floem::action::set_window_maximized(
                    !window_maximized.get_untracked(),
                );
            },
            || false,
            || false,
            || "Maximize",
            config,
        )
        .style(|s| s.margin_right(16.0)),
        clickable_icon(
            || LapceIcons::WINDOW_CLOSE,
            move || {
                window_command.send(WindowCommand::CloseWindow);
            },
            || false,
            || false,
            || "Close Window",
            config,
        )
        .style(|s| s.margin_right(6.0)),
    ))
    .style(move |s| {
        s.apply_if(
            cfg!(target_os = "macos")
                || !config.get_untracked().core.custom_titlebar
                || (is_title && num_window_tabs.get() > 1),
            |s| s.hide(),
        )
    })
}
