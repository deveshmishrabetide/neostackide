//! Welcome screen view component

use std::sync::Arc;

use floem::{
    View,
    peniko::Color,
    reactive::{ReadSignal, SignalGet},
    style::{AlignItems, CursorStyle, Display, JustifyContent},
    views::{container, label, list, scroll, v_stack, Decorators},
};

use crate::command::{LapceWorkbenchCommand, WindowCommand};
use crate::config::{color::LapceColor, LapceConfig};
use crate::db::LapceDb;
use crate::listener::Listener;
use crate::workspace::{LapceWorkspace, LapceWorkspaceType};

/// Welcome screen shown when no workspace is open
pub fn welcome_view(
    db: LapceDb,
    workbench_command: Listener<LapceWorkbenchCommand>,
    window_command: Listener<WindowCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let recent_workspaces = db.recent_workspaces().unwrap_or_default();

    container(
        v_stack((
            // Logo placeholder
            label(|| "N")
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(48.0)
                        .font_weight(floem::text::Weight::BOLD)
                        .width(80.0)
                        .height(80.0)
                        .border_radius(40.0)
                        .background(cfg.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND))
                        .color(Color::WHITE)
                        .justify_center()
                        .align_items(AlignItems::Center)
                        .margin_bottom(20.0)
                }),
            // Title
            label(|| "Welcome to NeoStack")
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(28.0)
                        .font_weight(floem::text::Weight::BOLD)
                        .color(cfg.color(LapceColor::EDITOR_FOREGROUND))
                        .margin_bottom(10.0)
                }),
            // Subtitle
            label(|| "Open a folder to get started")
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(14.0)
                        .color(cfg.color(LapceColor::EDITOR_DIM))
                        .margin_bottom(40.0)
                }),
            // Open Folder button
            open_folder_button(workbench_command.clone(), config),
            // Keyboard shortcut hint
            label(|| {
                #[cfg(target_os = "macos")]
                { "Cmd+O to open a folder" }
                #[cfg(not(target_os = "macos"))]
                { "Ctrl+O to open a folder" }
            })
            .style(move |s| {
                let cfg = config.get();
                s.font_size(12.0)
                    .color(cfg.color(LapceColor::EDITOR_DIM))
                    .margin_top(15.0)
                    .margin_bottom(40.0)
            }),
            // Recent workspaces section
            recent_workspaces_section(recent_workspaces, window_command, config),
        ))
        .style(|s| s.align_items(AlignItems::Center).max_width(500.0)),
    )
    .style(move |s| {
        let cfg = config.get();
        s.size_full()
            .flex_col()
            .align_items(AlignItems::Center)
            .justify_content(JustifyContent::Center)
            .background(cfg.color(LapceColor::EDITOR_BACKGROUND))
    })
}

fn open_folder_button(
    workbench_command: Listener<LapceWorkbenchCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let command = workbench_command.clone();

    container(
        label(|| "Open Folder")
            .style(move |s| {
                s.font_size(14.0)
                    .font_weight(floem::text::Weight::SEMIBOLD)
                    .color(Color::WHITE)
            }),
    )
    .on_click_stop(move |_| {
        command.send(LapceWorkbenchCommand::OpenFolder);
    })
    .style(move |s| {
        let cfg = config.get();
        s.padding_horiz(40.0)
            .padding_vert(14.0)
            .border_radius(6.0)
            .background(cfg.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND))
            .cursor(CursorStyle::Pointer)
            .hover(|s| {
                s.background(
                    cfg.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND)
                        .with_alpha(0.8),
                )
            })
    })
}

fn recent_workspaces_section(
    workspaces: Vec<LapceWorkspace>,
    window_command: Listener<WindowCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let has_workspaces = !workspaces.is_empty();

    // Build the workspace items as a vector of views
    let workspace_views: Vec<_> = workspaces
        .into_iter()
        .take(10)
        .map(|ws| recent_workspace_item(ws, window_command.clone(), config))
        .collect();

    v_stack((
        // Section title
        label(|| "Recent")
            .style(move |s| {
                let cfg = config.get();
                s.font_size(13.0)
                    .font_weight(floem::text::Weight::SEMIBOLD)
                    .color(cfg.color(LapceColor::EDITOR_DIM))
                    .margin_bottom(15.0)
                    .apply_if(!has_workspaces, |s| s.display(Display::None))
            }),
        // Workspaces list using list() for static content
        scroll(
            list(workspace_views)
                .style(|s| s.flex_col().width_full()),
        )
        .style(move |s| {
            s.max_height(300.0)
                .width_full()
                .apply_if(!has_workspaces, |s| s.display(Display::None))
        }),
    ))
    .style(|s| s.width_full().align_items(AlignItems::Center))
}

fn recent_workspace_item(
    workspace: LapceWorkspace,
    window_command: Listener<WindowCommand>,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let display_name = workspace
        .path
        .as_ref()
        .and_then(|p| p.file_name())
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| "Unknown".to_string());

    let full_path = workspace
        .path
        .as_ref()
        .map(|p| p.to_string_lossy().to_string())
        .unwrap_or_default();

    let ws = workspace.clone();
    let command = window_command.clone();

    container(
        v_stack((
            label(move || display_name.clone())
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(13.0)
                        .font_weight(floem::text::Weight::MEDIUM)
                        .color(cfg.color(LapceColor::EDITOR_FOREGROUND))
                }),
            label(move || full_path.clone())
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(11.0)
                        .color(cfg.color(LapceColor::EDITOR_DIM))
                        .margin_top(2.0)
                }),
        ))
        .style(|s| s.width_full()),
    )
    .on_click_stop(move |_| {
        if ws.path.is_some() {
            // Create a new workspace with current timestamp
            let new_workspace = LapceWorkspace {
                kind: ws.kind.clone(),
                path: ws.path.clone(),
                last_open: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_secs(),
            };
            command.send(WindowCommand::SetWorkspace { workspace: new_workspace });
        }
    })
    .style(move |s| {
        let cfg = config.get();
        s.width_full()
            .padding_horiz(15.0)
            .padding_vert(10.0)
            .border_radius(6.0)
            .cursor(CursorStyle::Pointer)
            .margin_bottom(5.0)
            .hover(|s| s.background(cfg.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
    })
}
