//! Message list component for agent chat

use std::sync::Arc;

use floem::{
    IntoView, View,
    reactive::{ReadSignal, SignalGet},
    style::{AlignItems, Display},
    views::{Decorators, container, dyn_stack, empty, h_stack, scroll, text, v_stack},
};
use im::Vector;

use crate::acp::{AgentMessage, MessageContent, MessageRole, ToolUseStatus};
use crate::config::{LapceConfig, color::LapceColor};

/// Render a single message
fn message_view(
    message: AgentMessage,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let role = message.role;
    let content = message.content;

    let (role_label, role_color) = match role {
        MessageRole::User => ("You", LapceColor::LAPCE_ICON_ACTIVE),
        MessageRole::Agent => ("Agent", LapceColor::EDITOR_FOCUS),
        MessageRole::System => ("System", LapceColor::LAPCE_WARN),
    };

    v_stack((
        // Role header
        text(role_label).style(move |s| {
            let config = config.get();
            s.font_size(11.0)
                .font_weight(floem::text::Weight::SEMIBOLD)
                .color(config.color(role_color))
                .padding_bottom(4.0)
        }),
        // Content
        message_content_view(content, config),
    ))
    .style(move |s| {
        let config = config.get();
        let bg = match role {
            MessageRole::User => config.color(LapceColor::PANEL_BACKGROUND),
            MessageRole::Agent => config.color(LapceColor::EDITOR_BACKGROUND),
            MessageRole::System => config.color(LapceColor::LAPCE_WARN).with_alpha(0.1),
        };
        s.width_full()
            .padding(12.0)
            .background(bg)
            .border_bottom(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
    })
}

/// Render message content based on type
fn message_content_view(
    content: MessageContent,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    match content {
        MessageContent::Text(text_content) => {
            container(
                text(text_content).style(move |s| {
                    let config = config.get();
                    s.font_size(13.0)
                        .color(config.color(LapceColor::EDITOR_FOREGROUND))
                        .line_height(1.5)
                })
            ).into_any()
        }
        MessageContent::Code { language, code } => {
            v_stack((
                // Language label
                text(language.unwrap_or_else(|| "code".to_string())).style(move |s| {
                    let config = config.get();
                    s.font_size(10.0)
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.6))
                        .padding_bottom(4.0)
                }),
                // Code block
                container(
                    text(code).style(move |s| {
                        let config = config.get();
                        s.font_size(12.0)
                            .font_family("monospace".to_string())
                            .color(config.color(LapceColor::EDITOR_FOREGROUND))
                    })
                ).style(move |s| {
                    let config = config.get();
                    s.padding(8.0)
                        .background(config.color(LapceColor::EDITOR_BACKGROUND))
                        .border_radius(4.0)
                        .border(1.0)
                        .border_color(config.color(LapceColor::LAPCE_BORDER))
                }),
            )).into_any()
        }
        MessageContent::ToolUse { name, status } => {
            let (status_text, status_color) = match status {
                ToolUseStatus::InProgress => ("Running...", LapceColor::EDITOR_FOCUS),
                ToolUseStatus::Success => ("Completed", LapceColor::LAPCE_ICON_ACTIVE),
                ToolUseStatus::Failed => ("Failed", LapceColor::LAPCE_ERROR),
                ToolUseStatus::Cancelled => ("Cancelled", LapceColor::LAPCE_WARN),
            };

            h_stack((
                // Tool icon placeholder
                empty().style(move |s| {
                    let config = config.get();
                    s.width(16.0)
                        .height(16.0)
                        .border_radius(8.0)
                        .background(config.color(status_color))
                }),
                // Tool name
                text(name).style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_weight(floem::text::Weight::MEDIUM)
                        .color(config.color(LapceColor::EDITOR_FOREGROUND))
                }),
                // Status
                text(status_text).style(move |s| {
                    let config = config.get();
                    s.font_size(11.0)
                        .color(config.color(status_color))
                }),
            ))
            .style(move |s| {
                let config = config.get();
                s.align_items(AlignItems::Center)
                    .gap(8.0)
                    .padding(8.0)
                    .background(config.color(LapceColor::PANEL_BACKGROUND))
                    .border_radius(4.0)
            }).into_any()
        }
        MessageContent::Diff { path, diff: _ } => {
            h_stack((
                text(format!("File: {}", path.display())).style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .color(config.color(LapceColor::EDITOR_FOREGROUND))
                }),
            ))
            .style(move |s| {
                let config = config.get();
                s.padding(8.0)
                    .background(config.color(LapceColor::PANEL_BACKGROUND))
                    .border_radius(4.0)
            }).into_any()
        }
        MessageContent::Error(error) => {
            container(
                text(error).style(move |s| {
                    let config = config.get();
                    s.font_size(13.0)
                        .color(config.color(LapceColor::LAPCE_ERROR))
                })
            ).style(move |s| {
                let config = config.get();
                s.padding(8.0)
                    .background(config.color(LapceColor::LAPCE_ERROR).with_alpha(0.1))
                    .border_radius(4.0)
            }).into_any()
        }
        MessageContent::Thinking(thinking) => {
            container(
                text(thinking).style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_style(floem::text::Style::Italic)
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7))
                })
            ).into_any()
        }
    }
}

/// Streaming text indicator
fn streaming_view(
    streaming_text: impl Fn() -> String + 'static,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    v_stack((
        // Role header
        text("Agent").style(move |s| {
            let config = config.get();
            s.font_size(11.0)
                .font_weight(floem::text::Weight::SEMIBOLD)
                .color(config.color(LapceColor::EDITOR_FOCUS))
                .padding_bottom(4.0)
        }),
        // Streaming content with cursor
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
            }),
        )),
    ))
    .style(move |s| {
        let config = config.get();
        s.width_full()
            .padding(12.0)
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
            .border_bottom(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
    })
}

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

/// The message list component
pub fn message_list(
    messages: impl Fn() -> Vector<AgentMessage> + 'static,
    streaming_text: impl Fn() -> String + 'static + Clone,
    is_streaming: impl Fn() -> bool + 'static + Clone,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let messages_fn = messages;
    let streaming_text_fn = streaming_text;
    let is_streaming_fn = is_streaming;

    container(
        scroll(
            v_stack((
                // Messages
                dyn_stack(
                    move || {
                        let msgs = messages_fn();
                        msgs.iter().cloned().enumerate().collect::<Vec<_>>()
                    },
                    |(idx, _msg)| *idx,
                    move |(_, msg)| message_view(msg, config),
                )
                .style(|s| s.flex_col().width_full()),
                // Streaming text
                {
                    let streaming_text_fn = streaming_text_fn.clone();
                    let streaming_text_fn2 = streaming_text_fn.clone();
                    let is_streaming_fn = is_streaming_fn.clone();
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
