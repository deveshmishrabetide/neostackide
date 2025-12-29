//! Todo panel component for displaying agent's execution plan
//!
//! Modern styled panel that sits above the input box.
//! Shows task list with progress tracking and connects visually with input.

use std::sync::Arc;

use floem::{
    View,
    reactive::{ReadSignal, RwSignal, SignalGet, SignalUpdate, SignalWith, create_memo},
    style::{AlignItems, CursorStyle, Display, FlexDirection},
    views::{Decorators, container, empty, h_stack, label, scroll, svg, v_stack, dyn_stack},
};

use crate::acp::{PlanEntry, PlanEntryStatus, PlanStats};
use crate::config::{LapceConfig, color::LapceColor, icon::LapceIcons};
use super::data::AgentData;

/// Check if there are any todo entries (for conditional styling in input)
pub fn has_todos(agent: &AgentData) -> impl Fn() -> bool + 'static + Copy {
    let plan_entries = agent.plan_entries;
    move || plan_entries.with(|e| !e.is_empty())
}

/// Todo panel that displays above the input
pub fn todo_panel(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let plan_entries = agent.plan_entries;
    let expanded = agent.todo_panel_expanded;

    // Compute stats from entries
    let stats = create_memo(move |_| {
        plan_entries.with(|entries| PlanStats::from_entries(entries))
    });

    // Get current in-progress task for collapsed view
    let current_task = create_memo(move |_| {
        plan_entries.with(|entries| {
            entries.iter()
                .find(|e| e.status == PlanEntryStatus::InProgress)
                .or_else(|| entries.iter().find(|e| e.status == PlanEntryStatus::Pending))
                .map(|e| e.content.clone())
        })
    });

    // Check if all tasks are completed
    let all_completed = create_memo(move |_| {
        let s = stats.get();
        s.total > 0 && s.completed == s.total
    });

    container(
        v_stack((
            // Collapsed view - compact single line
            collapsed_view(
                expanded,
                move || current_task.get(),
                move || all_completed.get(),
                move || stats.get(),
                config,
            ),
            // Expanded view - full task list
            expanded_view(expanded, plan_entries, move || stats.get(), config),
        ))
        .style(|s| s.width_full().flex_direction(FlexDirection::Column))
    )
    .style(move |s| {
        let config = config.get();
        let has_entries = plan_entries.with(|e| !e.is_empty());

        if has_entries {
            s.width_full()
                .border(2.0)
                .border_color(config.color(LapceColor::LAPCE_BORDER))
                // Round top corners, flat bottom to connect with input
                .border_radius(0.0)
                .border_top_left_radius(12.0)
                .border_top_right_radius(12.0)
                .border_bottom(0.0)
                .background(config.color(LapceColor::PANEL_BACKGROUND).with_alpha(0.3))
        } else {
            s.display(Display::None)
        }
    })
    .debug_name("Todo Panel")
}

/// Collapsed view - shows current task and progress
fn collapsed_view(
    expanded: RwSignal<bool>,
    current_task: impl Fn() -> Option<String> + 'static + Copy,
    all_completed: impl Fn() -> bool + 'static + Copy,
    stats: impl Fn() -> PlanStats + 'static + Copy,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    h_stack((
        // Status icon and current task
        h_stack((
            // Icon based on status
            svg(move || {
                if all_completed() {
                    config.get().ui_svg(LapceIcons::DEBUG_STOP)  // Checkmark-like
                } else {
                    config.get().ui_svg(LapceIcons::DEBUG_RESTART)  // Spinner
                }
            })
            .style(move |s| {
                let config = config.get();
                let color = if all_completed() {
                    floem::peniko::Color::from_rgb8(34, 197, 94) // Green
                } else {
                    config.color(LapceColor::LAPCE_ICON_ACTIVE)
                };
                s.width(14.0)
                    .height(14.0)
                    .color(color)
            }),
            // Task text
            label(move || {
                if all_completed() {
                    "All tasks completed".to_string()
                } else {
                    current_task().unwrap_or_else(|| "No active task".to_string())
                }
            })
            .style(move |s| {
                let config = config.get();
                s.font_size(12.0)
                    .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.8))
                    .text_ellipsis()
                    .flex_grow(1.0)
                    .min_width(0.0)
            }),
        ))
        .style(|s| s.align_items(AlignItems::Center).gap(8.0).flex_grow(1.0).min_width(0.0)),

        // Progress counter badge
        container(
            label(move || {
                let s = stats();
                format!("{}/{}", s.completed, s.total)
            })
            .style(move |s| {
                let config = config.get();
                s.font_size(10.0)
                    .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.6))
            })
        )
        .style(move |s| {
            let config = config.get();
            s.padding_horiz(8.0)
                .padding_vert(2.0)
                .border_radius(4.0)
                .background(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.08))
        }),

        // Expand icon
        svg(move || config.get().ui_svg(LapceIcons::ITEM_OPENED))
            .style(move |s| {
                let config = config.get();
                s.width(14.0)
                    .height(14.0)
                    .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.4))
            }),
    ))
    .style(move |s| {
        let config = config.get();
        let is_expanded = expanded.get();
        if !is_expanded {
            s.width_full()
                .padding(12.0)
                .align_items(AlignItems::Center)
                .gap(8.0)
                .cursor(CursorStyle::Pointer)
                .hover(|s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
        } else {
            s.display(Display::None)
        }
    })
    .on_click_stop(move |_| {
        expanded.set(true);
    })
}

/// Expanded view - shows full task list with header
fn expanded_view(
    expanded: RwSignal<bool>,
    plan_entries: RwSignal<Vec<PlanEntry>>,
    stats: impl Fn() -> PlanStats + 'static + Copy,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    v_stack((
        // Header row
        h_stack((
            // Title
            label(|| "Tasks")
                .style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_bold()
                        .color(config.color(LapceColor::EDITOR_FOREGROUND))
                }),

            // Stats
            label(move || {
                let s = stats();
                format!("{}/{}", s.completed, s.total)
            })
            .style(move |s| {
                let config = config.get();
                s.font_size(11.0)
                    .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.5))
            }),

            // Spacer
            empty().style(|s| s.flex_grow(1.0)),

            // Progress bar
            container(
                container(empty())
                    .style(move |s| {
                        let config = config.get();
                        let progress = stats().progress;
                        s.height_full()
                            .width_pct(progress as f64)
                            .border_radius(2.0)
                            .background(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                    })
            )
            .style(move |s| {
                let config = config.get();
                s.width(80.0)
                    .height(4.0)
                    .border_radius(2.0)
                    .background(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.15))
            }),

            // Collapse icon
            svg(move || config.get().ui_svg(LapceIcons::ITEM_CLOSED))
                .style(move |s| {
                    let config = config.get();
                    s.width(14.0)
                        .height(14.0)
                        .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.4))
                }),
        ))
        .style(move |s| {
            let config = config.get();
            s.width_full()
                .padding(12.0)
                .align_items(AlignItems::Center)
                .gap(8.0)
                .cursor(CursorStyle::Pointer)
                .border_bottom(1.0)
                .border_color(config.color(LapceColor::LAPCE_BORDER).with_alpha(0.5))
                .hover(|s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
        })
        .on_click_stop(move |_| {
            expanded.set(false);
        }),

        // Task list
        scroll(
            dyn_stack(
                move || plan_entries.get(),
                |entry| entry.content.clone(),
                move |entry| todo_item(entry, config),
            )
            .style(|s| s.flex_direction(FlexDirection::Column).width_full())
        )
        .style(|s| s.max_height(160.0).width_full().padding(8.0)),
    ))
    .style(move |s| {
        let is_expanded = expanded.get();
        if is_expanded {
            s.width_full()
        } else {
            s.display(Display::None)
        }
    })
}

/// Single todo item row
fn todo_item(entry: PlanEntry, config: ReadSignal<Arc<LapceConfig>>) -> impl View {
    let status = entry.status;
    let content = entry.content.clone();

    h_stack((
        // Status icon
        svg(move || {
            let icon = match status {
                PlanEntryStatus::Completed => LapceIcons::DEBUG_STOP,
                PlanEntryStatus::InProgress => LapceIcons::DEBUG_RESTART,
                PlanEntryStatus::Pending => LapceIcons::ITEM_CLOSED,
            };
            config.get().ui_svg(icon)
        })
        .style(move |s| {
            let config = config.get();
            let color = match status {
                PlanEntryStatus::Completed => floem::peniko::Color::from_rgb8(34, 197, 94),
                PlanEntryStatus::InProgress => config.color(LapceColor::LAPCE_ICON_ACTIVE),
                PlanEntryStatus::Pending => config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.3),
            };
            s.width(14.0)
                .height(14.0)
                .color(color)
        }),

        // Task content
        label(move || content.clone())
            .style(move |s| {
                let config = config.get();
                let base = s
                    .font_size(12.0)
                    .flex_grow(1.0)
                    .min_width(0.0);

                match status {
                    PlanEntryStatus::Completed => base
                        .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.4)),
                    PlanEntryStatus::InProgress => base
                        .color(config.color(LapceColor::EDITOR_FOREGROUND)),
                    PlanEntryStatus::Pending => base
                        .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.7)),
                }
            }),
    ))
    .style(move |s| {
        let config = config.get();
        s.align_items(AlignItems::Center)
            .gap(10.0)
            .width_full()
            .padding_vert(6.0)
            .padding_horiz(8.0)
            .border_radius(6.0)
            .hover(|s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
    })
}
