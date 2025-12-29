//! Message list component for agent chat
//!
//! Renders messages with interleaved content (text, tool calls, reasoning)
//! and supports collapsible tool call display.

use std::sync::Arc;

use floem::{
    IntoView, View,
    reactive::{ReadSignal, SignalGet, SignalWith, create_memo},
    style::{AlignItems, CursorStyle, Display},
    views::{Decorators, container, dyn_stack, empty, h_stack, scroll, text, v_stack},
};
use im::Vector;

use crate::acp::{AgentMessage, MessagePart, MessageRole, ToolCallState};
use crate::config::{LapceConfig, color::LapceColor};
use crate::agent::data::AgentData;

// ============================================================================
// Tool Call Helpers
// ============================================================================

/// Extract the target path/command from tool arguments for display
fn extract_tool_target(tool_name: &str, args: &Option<String>) -> String {
    let Some(args) = args else { return String::new() };

    // Try to parse as JSON and extract relevant field
    if let Ok(json) = serde_json::from_str::<serde_json::Value>(args) {
        let name_lower = tool_name.to_lowercase();

        // File tools
        if name_lower.contains("read") || name_lower.contains("write") || name_lower.contains("edit") {
            if let Some(path) = json.get("file_path").or(json.get("path")) {
                if let Some(s) = path.as_str() {
                    // Get just the filename for display
                    return s.split('/').last().unwrap_or(s).to_string();
                }
            }
        }

        // Bash command
        if name_lower == "bash" {
            if let Some(cmd) = json.get("command") {
                if let Some(s) = cmd.as_str() {
                    // Truncate long commands
                    if s.len() > 50 {
                        return format!("{}...", &s[..47]);
                    }
                    return s.to_string();
                }
            }
        }

        // Glob/Grep pattern
        if name_lower == "glob" || name_lower == "grep" {
            if let Some(pattern) = json.get("pattern") {
                if let Some(s) = pattern.as_str() {
                    return s.to_string();
                }
            }
        }
    }

    String::new()
}

/// Get a summary of the tool result
fn get_tool_summary(tool_name: &str, output: &Option<String>, is_error: bool) -> String {
    if is_error {
        return "Error".to_string();
    }

    let Some(output) = output else {
        return String::new();
    };

    let name_lower = tool_name.to_lowercase();

    if name_lower.contains("read") {
        let lines = output.lines().count();
        return format!("Read {} line{}", lines, if lines != 1 { "s" } else { "" });
    }

    if name_lower.contains("write") {
        return "File written".to_string();
    }

    if name_lower.contains("edit") {
        return "File edited".to_string();
    }

    if name_lower == "bash" {
        if output.trim().is_empty() {
            return "No output".to_string();
        }
        let lines = output.lines().count();
        return format!("{} line{}", lines, if lines != 1 { "s" } else { "" });
    }

    if name_lower == "glob" {
        let files = output.lines().filter(|l| !l.is_empty()).count();
        return format!("Found {} file{}", files, if files != 1 { "s" } else { "" });
    }

    if name_lower == "grep" {
        let matches = output.lines().filter(|l| !l.is_empty()).count();
        return format!("Found {} match{}", matches, if matches != 1 { "es" } else { "" });
    }

    // Default
    if output.trim().is_empty() {
        "Complete".to_string()
    } else {
        let lines = output.lines().count();
        format!("{} line{}", lines, if lines != 1 { "s" } else { "" })
    }
}

// ============================================================================
// Inline Tool Call Component
// ============================================================================

/// Render an inline tool call with collapsible output
fn inline_tool_call(
    tool_call_id: String,
    name: String,
    args: Option<String>,
    state: ToolCallState,
    output: Option<String>,
    is_error: bool,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    tracing::debug!("inline_tool_call CREATED: {} state={:?} output={}",
        tool_call_id, state, output.is_some());

    let tool_id_for_click = tool_call_id.clone();
    let expanded_tools = agent.expanded_tools;

    // Create memo for expanded state - this creates stronger reactive dependency
    let is_expanded = {
        let tool_id = tool_call_id.clone();
        create_memo(move |_| expanded_tools.with(|set| set.contains(&tool_id)))
    };

    let target = extract_tool_target(&name, &args);
    let summary = get_tool_summary(&name, &output, is_error);
    let has_output = output.is_some();
    let output_for_expanded = output.clone();

    // Status dot color - None means use explicit green for success
    let status_color: Option<&str> = match state {
        ToolCallState::Running => Some(LapceColor::LAPCE_WARN),  // Yellow
        ToolCallState::Completed => None,  // Will use explicit green
        ToolCallState::Failed => Some(LapceColor::LAPCE_ERROR),  // Red
    };

    v_stack((
        // Header: ● ToolName(target) - clickable
        h_stack((
            // Status dot
            empty().style(move |s| {
                let config = config.get();
                // Use explicit green for success, theme colors for others
                let color = match status_color {
                    Some(c) => config.color(c),
                    None => floem::peniko::Color::from_rgb8(34, 197, 94), // Green for success
                };
                s.width(8.0)
                    .height(8.0)
                    .border_radius(4.0)
                    .background(color)
                    .flex_shrink(0.0)
            }),
            // Tool name
            text(name.clone()).style(move |s| {
                let config = config.get();
                s.font_size(13.0)
                    .font_family("monospace".to_string())
                    .color(config.color(LapceColor::EDITOR_FOREGROUND))
            }),
            // Target in parentheses
            {
                let target_display = if target.is_empty() {
                    String::new()
                } else if target.len() > 60 {
                    format!("(...{})", &target[target.len()-57..])
                } else {
                    format!("({})", target)
                };
                text(target_display).style(move |s| {
                    let config = config.get();
                    s.font_size(13.0)
                        .font_family("monospace".to_string())
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7))
                })
            },
        ))
        .style(move |s| {
            s.align_items(AlignItems::Center)
                .gap(6.0)
                .cursor(CursorStyle::Pointer)
                .padding(4.0)
                .margin_left(-4.0)
                .border_radius(4.0)
                .hover(|s| {
                    let config = config.get();
                    s.background(config.color(LapceColor::PANEL_BACKGROUND))
                })
        })
        .on_click_stop(move |_| {
            tracing::info!("Tool header clicked: {}", tool_id_for_click);
            agent.toggle_tool_expanded(&tool_id_for_click);
        }),

        // Summary line (when collapsed): └ Result summary
        container(
            h_stack((
                text("└").style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_family("monospace".to_string())
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.4))
                }),
                text(summary.clone()).style(move |s| {
                    let config = config.get();
                    let color = if is_error {
                        config.color(LapceColor::LAPCE_ERROR)
                    } else {
                        config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7)
                    };
                    s.font_size(12.0)
                        .font_family("monospace".to_string())
                        .color(color)
                }),
            ))
            .style(|s| s.gap(6.0).padding_left(14.0))
        ).style(move |s| {
            // Hide when expanded or no summary
            if is_expanded.get() || summary.is_empty() {
                s.display(Display::None)
            } else {
                s
            }
        }),

        // Expanded content
        container(
            v_stack((
                // Summary in expanded view
                h_stack((
                    text("└").style(move |s| {
                        let config = config.get();
                        s.font_size(12.0)
                            .font_family("monospace".to_string())
                            .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.4))
                    }),
                    text(get_tool_summary(&name, &output_for_expanded, is_error)).style(move |s| {
                        let config = config.get();
                        let color = if is_error {
                            config.color(LapceColor::LAPCE_ERROR)
                        } else {
                            config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7)
                        };
                        s.font_size(12.0)
                            .font_family("monospace".to_string())
                            .color(color)
                    }),
                ))
                .style(|s| s.gap(6.0)),

                // Full output with scroll
                scroll(
                    text(output.unwrap_or_default()).style(move |s| {
                        let config = config.get();
                        s.font_size(12.0)
                            .font_family("monospace".to_string())
                            .color(config.color(LapceColor::EDITOR_FOREGROUND))
                            .line_height(1.4)
                            .min_width_full()
                    })
                ).style(move |s| {
                    let config = config.get();
                    s.padding(8.0)
                        .margin_top(4.0)
                        .background(config.color(LapceColor::PANEL_BACKGROUND))
                        .border_radius(4.0)
                        .max_height(200.0)
                        .width_full()
                }),
            ))
            .style(|s| s.flex_col().width_full().margin_left(14.0).border_left(2.0).padding_left(12.0))
        ).style(move |s| {
            if is_expanded.get() && has_output {
                s.margin_top(4.0).width_full()
            } else {
                s.display(Display::None)
            }
        }),

        // Loading state when running
        container(
            h_stack((
                text("└").style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_family("monospace".to_string())
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.4))
                }),
                text("Running...").style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_family("monospace".to_string())
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7))
                }),
            ))
            .style(|s| s.gap(6.0).padding_left(14.0))
        ).style(move |s| {
            if state == ToolCallState::Running {
                s
            } else {
                s.display(Display::None)
            }
        }),
    ))
    .style(move |s| {
        s.flex_col()
            .width_full()
            .padding_vert(2.0)
    })
}

// ============================================================================
// Message Parts Rendering
// ============================================================================

/// Render a text part
fn text_part_view(
    text_content: String,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    container(
        text(text_content).style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .color(config.color(LapceColor::EDITOR_FOREGROUND))
                .line_height(1.5)
        })
    )
}

/// Render a reasoning/thinking part (collapsible)
fn reasoning_part_view(
    reasoning: String,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    container(
        text(reasoning).style(move |s| {
            let config = config.get();
            s.font_size(12.0)
                .font_style(floem::text::Style::Italic)
                .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7))
                .line_height(1.4)
        })
    ).style(move |s| {
        let config = config.get();
        s.padding(8.0)
            .margin_vert(4.0)
            .border_left(2.0)
            .border_color(config.color(LapceColor::LAPCE_WARN).with_alpha(0.5))
            .background(config.color(LapceColor::LAPCE_WARN).with_alpha(0.05))
    })
}

/// Render an error part
fn error_part_view(
    error: String,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    container(
        text(error).style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .color(config.color(LapceColor::LAPCE_ERROR))
        })
    ).style(move |s| {
        let config = config.get();
        s.padding(8.0)
            .margin_vert(4.0)
            .background(config.color(LapceColor::LAPCE_ERROR).with_alpha(0.1))
            .border_radius(4.0)
    })
}

/// Render all parts of a message
fn message_parts_view(
    parts: Vec<MessagePart>,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    // Log what parts we're rendering
    tracing::debug!("message_parts_view: rendering {} parts", parts.len());
    for (i, p) in parts.iter().enumerate() {
        match p {
            MessagePart::Text(t) => tracing::debug!("  part[{}]: Text({} chars)", i, t.len()),
            MessagePart::ToolCall { tool_call_id, name, state, output, .. } => {
                tracing::debug!("  part[{}]: ToolCall({}, {}, {:?}, output={})",
                    i, tool_call_id, name, state, output.is_some());
            },
            MessagePart::Reasoning(_) => tracing::debug!("  part[{}]: Reasoning", i),
            MessagePart::Error(_) => tracing::debug!("  part[{}]: Error", i),
        }
    }

    // Create indexed parts for stable keys
    let indexed_parts: Vec<(usize, MessagePart)> = parts.into_iter().enumerate().collect();

    dyn_stack(
        move || indexed_parts.clone(),
        |(idx, part)| {
            // Generate stable key using index + type + state/output info
            let key = match part {
                MessagePart::Text(_) => format!("text-{}", idx),
                MessagePart::ToolCall { tool_call_id, state, output, .. } => {
                    let state_char = match state {
                        ToolCallState::Running => "r",
                        ToolCallState::Completed => "c",
                        ToolCallState::Failed => "f",
                    };
                    let has_output = if output.is_some() { "O" } else { "" };
                    format!("{}-{}{}", tool_call_id, state_char, has_output)
                },
                MessagePart::Reasoning(_) => format!("reason-{}", idx),
                MessagePart::Error(_) => format!("error-{}", idx),
            };
            tracing::debug!("  parts_view key: {}", key);
            key
        },
        move |(_idx, part)| {
            let agent = agent.clone();
            match part {
                MessagePart::Text(text_content) => {
                    text_part_view(text_content, config).into_any()
                }
                MessagePart::ToolCall {
                    tool_call_id,
                    name,
                    args,
                    state,
                    output,
                    is_error,
                } => {
                    inline_tool_call(
                        tool_call_id,
                        name,
                        args,
                        state,
                        output,
                        is_error,
                        agent,
                        config,
                    ).into_any()
                }
                MessagePart::Reasoning(reasoning) => {
                    reasoning_part_view(reasoning, config).into_any()
                }
                MessagePart::Error(error) => {
                    error_part_view(error, config).into_any()
                }
            }
        },
    )
    .style(|s| s.flex_col().width_full().gap(4.0))
}

// ============================================================================
// Message View
// ============================================================================

/// Render a single message with all its parts
fn message_view(
    message: AgentMessage,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
    is_last: bool,
    is_streaming: impl Fn() -> bool + 'static,
) -> impl View {
    let role = message.role;
    let parts = message.parts;

    let (role_label, role_color) = match role {
        MessageRole::User => ("You", LapceColor::LAPCE_ICON_ACTIVE),
        MessageRole::Agent => ("Agent", LapceColor::EDITOR_FOCUS),
        MessageRole::System => ("System", LapceColor::LAPCE_WARN),
    };

    // Hide border on last agent message when streaming (streaming text continues it)
    let hide_border = is_last && role == MessageRole::Agent;

    v_stack((
        // Role header
        text(role_label).style(move |s| {
            let config = config.get();
            s.font_size(11.0)
                .font_weight(floem::text::Weight::SEMIBOLD)
                .color(config.color(role_color))
                .padding_bottom(4.0)
        }),
        // Message parts
        message_parts_view(parts, agent, config),
    ))
    .style(move |s| {
        let config = config.get();
        let bg = match role {
            MessageRole::User => config.color(LapceColor::PANEL_BACKGROUND),
            MessageRole::Agent => config.color(LapceColor::EDITOR_BACKGROUND),
            MessageRole::System => config.color(LapceColor::LAPCE_WARN).with_alpha(0.1),
        };
        let s = s.width_full()
            .padding(12.0)
            .background(bg);
        // Don't show border on last agent message when streaming
        if hide_border && is_streaming() {
            s
        } else {
            s.border_bottom(1.0)
                .border_color(config.color(LapceColor::LAPCE_BORDER))
        }
    })
}

// ============================================================================
// Streaming View
// ============================================================================

/// Streaming text indicator - shown as continuation of agent message (no header)
fn streaming_view(
    streaming_text: impl Fn() -> String + 'static,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    // Streaming content with cursor - no "Agent" header since it continues the agent message
    h_stack((
        floem::views::label(streaming_text).style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .color(config.color(LapceColor::EDITOR_FOREGROUND))
                .line_height(1.5)
        }),
        // Blinking cursor
        empty().style(move |s| {
            let config = config.get();
            s.width(2.0)
                .height(16.0)
                .background(config.color(LapceColor::EDITOR_CARET))
                .margin_left(2.0)
        }),
    ))
    .style(move |s| {
        let config = config.get();
        s.width_full()
            .padding_horiz(12.0)
            .padding_vert(4.0)
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
}

// ============================================================================
// Empty State
// ============================================================================

/// Empty state when no messages
fn empty_state(config: ReadSignal<Arc<LapceConfig>>) -> impl View {
    v_stack((
        text("Start a conversation").style(move |s| {
            let config = config.get();
            s.font_size(16.0)
                .font_weight(floem::text::Weight::MEDIUM)
                .color(config.color(LapceColor::EDITOR_FOREGROUND))
        }),
        text("Type a message below to begin").style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.6))
                .padding_top(8.0)
        }),
    ))
    .style(move |s| {
        s.flex_grow(1.0)
            .width_full()
            .flex_col()
            .items_center()
            .justify_center()
    })
}

// ============================================================================
// Message List
// ============================================================================

/// The message list component
pub fn message_list(
    messages: impl Fn() -> Vector<AgentMessage> + 'static,
    streaming_text: impl Fn() -> String + 'static + Clone,
    is_streaming: impl Fn() -> bool + 'static + Clone,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let messages_fn = messages;
    let streaming_text_fn = streaming_text;
    let is_streaming_fn = is_streaming;
    let is_streaming_for_msgs = is_streaming_fn.clone();
    let is_streaming_for_view = is_streaming_fn.clone();

    container(
        scroll(
            v_stack((
                // Messages
                dyn_stack(
                    move || {
                        let msgs = messages_fn();
                        let len = msgs.len();
                        // Include index for detecting last message
                        msgs.iter().cloned().enumerate().map(|(i, m)| (i, len, m)).collect::<Vec<_>>()
                    },
                    |(_, _, msg)| {
                        // Key includes parts count, types, states, AND output presence
                        // This ensures view recreation when tool state or output changes
                        let parts_summary: String = msg.parts.iter().map(|p| {
                            match p {
                                MessagePart::Text(t) => format!("T{}", t.len() % 100), // Include length hint
                                MessagePart::ToolCall { state, output, .. } => {
                                    let state_char = match state {
                                        ToolCallState::Running => "r",
                                        ToolCallState::Completed => "c",
                                        ToolCallState::Failed => "f",
                                    };
                                    let has_output = if output.is_some() { "O" } else { "" };
                                    format!("C{}{}", state_char, has_output)
                                },
                                MessagePart::Reasoning(_) => "R".to_string(),
                                MessagePart::Error(_) => "E".to_string(),
                            }
                        }).collect::<Vec<_>>().join("-");
                        let key = format!("msg{}:{}", msg.id, parts_summary);
                        tracing::debug!("dyn_stack key: {}", key);
                        key
                    },
                    {
                        let is_streaming_fn = is_streaming_for_msgs.clone();
                        move |(idx, len, msg)| {
                            let is_last = idx == len - 1;
                            let is_streaming_fn = is_streaming_fn.clone();
                            message_view(msg, agent.clone(), config, is_last, move || is_streaming_fn())
                        }
                    },
                )
                .style(|s| s.flex_col().width_full()),
                // Streaming text - continues the last agent message visually
                {
                    let streaming_text_fn = streaming_text_fn.clone();
                    let streaming_text_fn2 = streaming_text_fn.clone();
                    let is_streaming_fn = is_streaming_for_view;
                    container(
                        streaming_view(move || streaming_text_fn(), config)
                    ).style(move |s| {
                        if is_streaming_fn() && !streaming_text_fn2().is_empty() {
                            s
                        } else {
                            s.display(Display::None)
                        }
                    })
                },
            ))
            .style(|s| s.flex_col().width_full()),
        )
        .style(|s| s.flex_grow(1.0).width_full()),
    )
    .style(move |s| {
        let config = config.get();
        s.flex_grow(1.0)
            .width_full()
            .flex_col()
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
    .debug_name("Message List")
}
