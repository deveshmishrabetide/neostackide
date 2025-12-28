//! Chat input component for agent mode

use std::sync::Arc;

use floem::{
    View,
    event::{Event, EventListener},
    keyboard::{Key, NamedKey},
    peniko::Color,
    reactive::{ReadSignal, SignalGet, SignalUpdate},
    style::{AlignItems, CursorStyle, Display},
    views::{Decorators, container, empty, h_stack, label, svg, text_input, v_stack},
};

use super::data::AgentData;
use super::icons::provider_icon;
use crate::app::clickable_icon;
use crate::config::{LapceConfig, color::LapceColor, icon::LapceIcons};

/// Chat input component with send/stop button
pub fn agent_input(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
    on_send: impl Fn(String) + 'static + Clone,
    on_stop: impl Fn() + 'static + Clone,
) -> impl View {
    let input_value = agent.input_value;
    let agent_streaming = agent.clone();
    let agent_streaming2 = agent.clone();
    let agent_streaming3 = agent.clone();
    let agent_streaming4 = agent.clone();
    let agent_streaming5 = agent.clone();
    let on_send_key = on_send.clone();
    let on_send_btn = on_send.clone();

    v_stack((
        // Input area
        container(
            h_stack((
                // Text input
                text_input(input_value)
                    .placeholder("Message the agent...")
                    .style(move |s| {
                        let config = config.get();
                        s.flex_grow(1.0)
                            .padding(12.0)
                            .font_size(13.0)
                            .background(Color::TRANSPARENT)
                            .border(0.0)
                            .color(config.color(LapceColor::EDITOR_FOREGROUND))
                    })
                    .on_event_stop(EventListener::KeyDown, move |event| {
                        if let Event::KeyDown(key_event) = event {
                            if key_event.key.logical_key == Key::Named(NamedKey::Enter)
                                && !key_event.modifiers.shift()
                            {
                                let value = input_value.get();
                                let is_streaming = agent_streaming.is_current_streaming();
                                if !value.trim().is_empty() && !is_streaming {
                                    on_send_key(value.clone());
                                    input_value.set(String::new());
                                }
                            }
                        }
                    }),
                // Send/Stop button
                container(
                    svg(move || {
                        let is_streaming = agent_streaming2.is_current_streaming();
                        if is_streaming {
                            config.get().ui_svg(LapceIcons::CLOSE)
                        } else {
                            config.get().ui_svg(LapceIcons::START)
                        }
                    })
                    .style(move |s| {
                        let config = config.get();
                        let size = config.ui.icon_size() as f32;
                        let is_streaming = agent_streaming3.is_current_streaming();
                        let color = if is_streaming {
                            config.color(LapceColor::LAPCE_ERROR)
                        } else if input_value.get().trim().is_empty() {
                            config.color(LapceColor::LAPCE_ICON_ACTIVE).with_alpha(0.4)
                        } else {
                            config.color(LapceColor::LAPCE_ICON_ACTIVE)
                        };
                        s.width(size)
                            .height(size)
                            .color(color)
                    }),
                )
                .style(move |s| {
                    let config = config.get();
                    let is_streaming = agent_streaming4.is_current_streaming();
                    let can_send = !input_value.get().trim().is_empty() || is_streaming;
                    s.padding(8.0)
                        .margin_right(4.0)
                        .border_radius(4.0)
                        .cursor(if can_send { CursorStyle::Pointer } else { CursorStyle::Default })
                        .hover(move |s| {
                            if can_send {
                                s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
                            } else {
                                s
                            }
                        })
                })
                .on_click_stop(move |_| {
                    let is_streaming = agent_streaming5.is_current_streaming();
                    if is_streaming {
                        on_stop();
                    } else {
                        let value = input_value.get();
                        if !value.trim().is_empty() {
                            on_send_btn(value.clone());
                            input_value.set(String::new());
                        }
                    }
                }),
            ))
            .style(move |s| {
                let config = config.get();
                s.width_full()
                    .align_items(AlignItems::Center)
                    .border(2.0)
                    .border_radius(8.0)
                    .border_color(config.color(LapceColor::LAPCE_BORDER))
                    .background(config.color(LapceColor::EDITOR_BACKGROUND))
            }),
        ),
        // Bottom toolbar with model selector
        h_stack((
            // Model/provider indicator
            h_stack((
                svg(move || provider_icon(agent.provider.get()).to_string())
                    .style(|s| s.width(14.0).height(14.0)),
                label(move || agent.provider.get().display_name().to_string())
                    .style(move |s| {
                        let config = config.get();
                        s.font_size(11.0)
                            .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.7))
                    }),
            ))
            .style(move |s| {
                s.align_items(AlignItems::Center)
                    .gap(4.0)
                    .padding(4.0)
            }),
            // Spacer
            empty().style(|s| s.flex_grow(1.0)),
            // Hint text
            label(|| "Enter to send, Shift+Enter for new line")
                .style(move |s| {
                    let config = config.get();
                    s.font_size(10.0)
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.5))
                }),
        ))
        .style(move |s| {
            s.width_full()
                .padding_top(8.0)
                .align_items(AlignItems::Center)
        }),
    ))
    .style(move |s| {
        s.width_full()
            .padding(16.0)
    })
    .debug_name("Agent Input")
}

/// Error banner component
pub fn error_banner(
    error: impl Fn() -> Option<String> + 'static + Clone,
    on_dismiss: impl Fn() + 'static,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let error_fn = error.clone();
    let error_fn2 = error.clone();

    container(
        h_stack((
            // Error icon
            svg(move || config.get().ui_svg(LapceIcons::ERROR))
                .style(move |s| {
                    let config = config.get();
                    s.width(16.0)
                        .height(16.0)
                        .color(config.color(LapceColor::LAPCE_ERROR))
                }),
            // Error text
            label(move || error_fn().unwrap_or_default())
                .style(move |s| {
                    let config = config.get();
                    s.flex_grow(1.0)
                        .font_size(12.0)
                        .color(config.color(LapceColor::LAPCE_ERROR))
                }),
            // Dismiss button
            clickable_icon(
                || LapceIcons::CLOSE,
                on_dismiss,
                || false,
                || false,
                || "Dismiss",
                config,
            ),
        ))
        .style(|s| s.align_items(AlignItems::Center).gap(8.0)),
    )
    .style(move |s| {
        let config = config.get();
        let has_error = error_fn2().is_some();
        let s = s
            .width_full()
            .padding(12.0)
            .background(config.color(LapceColor::LAPCE_ERROR).with_alpha(0.1))
            .border_radius(4.0)
            .margin_bottom(8.0);
        if has_error {
            s
        } else {
            s.display(Display::None)
        }
    })
    .debug_name("Error Banner")
}
