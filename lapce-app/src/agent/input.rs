//! Chat input component for agent mode

use std::sync::Arc;

use floem::{
    View,
    action::show_context_menu,
    event::{Event, EventListener},
    keyboard::{Key, NamedKey},
    menu::{Menu, MenuItem},
    reactive::{ReadSignal, SignalGet, SignalUpdate, SignalWith, create_memo},
    style::{AlignItems, CursorStyle, Display, FlexDirection},
    views::{Decorators, container, empty, h_stack, label, svg, v_stack},
};
use lapce_core::buffer::Buffer;
use lapce_core::buffer::rope_text::RopeText;

use super::data::AgentData;
use crate::app::clickable_icon;
use crate::config::{LapceConfig, color::LapceColor, icon::LapceIcons};
use crate::text_input::TextInputBuilder;

/// Get a short display name for a model ID
fn get_model_short_name(model_id: &str) -> String {
    // Extract a user-friendly short name from model IDs
    // e.g., "claude-sonnet-4-20250514" -> "Sonnet 4"
    // e.g., "claude-opus-4-5-20251101" -> "Opus 4.5"
    if model_id.contains("opus") {
        if model_id.contains("4-5") || model_id.contains("4.5") {
            "Opus 4.5".to_string()
        } else {
            "Opus 4".to_string()
        }
    } else if model_id.contains("sonnet") {
        "Sonnet 4".to_string()
    } else if model_id.contains("haiku") {
        "Haiku".to_string()
    } else if model_id.contains("gpt-4o-mini") {
        "4o Mini".to_string()
    } else if model_id.contains("gpt-4o") {
        "GPT-4o".to_string()
    } else if model_id.contains("gpt-4") {
        "GPT-4".to_string()
    } else {
        // Fallback: use the model ID with some cleanup
        model_id.split('-').next().unwrap_or(model_id).to_string()
    }
}

/// Model selector button component using dynamic models from agent
fn model_selector(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let agent_for_menu = agent.clone();
    let available_models = agent.available_models;
    let current_model_id = agent.current_model_id;

    h_stack((
        // Model icon
        svg(move || config.get().ui_svg(LapceIcons::LIGHTBULB))
            .style(move |s| {
                let config = config.get();
                s.width(12.0)
                    .height(12.0)
                    .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
            }),
        // Model name - shows current model or "Select Model" if none
        label(move || {
            if let Some(id) = current_model_id.get() {
                // Find the model name from available models
                available_models.with(|models| {
                    models.iter()
                        .find(|m| m.id == id)
                        .map(|m| get_model_short_name(&m.name))
                        .unwrap_or_else(|| get_model_short_name(&id))
                })
            } else if available_models.with(|m| m.is_empty()) {
                "Model".to_string()
            } else {
                "Select".to_string()
            }
        })
        .style(move |s| {
            let config = config.get();
            s.font_size(11.0)
                .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.8))
        }),
        // Dropdown arrow
        svg(move || config.get().ui_svg(LapceIcons::ITEM_OPENED))
            .style(move |s| {
                let config = config.get();
                s.width(10.0)
                    .height(10.0)
                    .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.5))
            }),
    ))
    .style(move |s| {
        let config = config.get();
        s.align_items(AlignItems::Center)
            .gap(4.0)
            .padding_horiz(8.0)
            .padding_vert(4.0)
            .border_radius(4.0)
            .cursor(CursorStyle::Pointer)
            .hover(move |s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
    })
    .on_click_stop(move |_| {
        let models = agent_for_menu.available_models.get();
        let current_id = agent_for_menu.current_model_id.get();

        if models.is_empty() {
            // No models available yet - show a placeholder menu
            let menu = Menu::new("").entry(
                MenuItem::new("   Connecting to agent...")
            );
            show_context_menu(menu, None);
            return;
        }

        let mut menu = Menu::new("");
        for model in models {
            let model_id = model.id.clone();
            let model_id_for_action = model.id.clone();
            let is_selected = current_id.as_ref() == Some(&model_id);
            let display_name = if model.name.is_empty() {
                get_model_short_name(&model.id)
            } else {
                model.name.clone()
            };

            let item = if is_selected {
                MenuItem::new(format!("✓ {}", display_name))
            } else {
                MenuItem::new(format!("   {}", display_name))
            };

            let agent_clone = agent_for_menu.clone();
            menu = menu.entry(item.action(move || {
                agent_clone.set_model(&model_id_for_action);
            }));
        }
        show_context_menu(menu, None);
    })
}

/// Chat input component with send/stop button
pub fn agent_input(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
    on_send: impl Fn(String) + 'static + Clone,
    on_stop: impl Fn() + 'static + Clone,
) -> impl View {
    let editor = agent.input_editor.clone();
    let editor_for_height = agent.input_editor.clone();
    let doc = editor.doc_signal();
    let agent_streaming = agent.clone();
    let agent_streaming2 = agent.clone();
    let agent_streaming3 = agent.clone();
    let agent_streaming4 = agent.clone();
    let agent_for_send = agent.clone();
    let agent_for_btn = agent.clone();
    let agent_for_model = agent.clone();
    let on_send_key = on_send.clone();
    let on_send_btn = on_send.clone();

    // Create a memo to check if the input is empty
    let is_empty = {
        let doc = doc.get();
        create_memo(move |_| {
            doc.buffer.with(|b: &Buffer| b.is_empty() || b.to_string().trim().is_empty())
        })
    };
    let is_empty2 = is_empty;

    // Calculate dynamic height based on content (min 1 line, max 8 lines)
    let input_height = {
        let doc = editor_for_height.doc_signal().get();
        create_memo(move |_| {
            let line_count = doc.buffer.with(|b: &Buffer| {
                let text = b.to_string();
                text.lines().count().max(1)
            });
            let line_height = 20.0_f64; // Approximate line height
            let min_height = 36.0_f64;  // Single line minimum
            let max_height = 160.0_f64; // ~8 lines maximum
            let calculated = (line_count as f64 * line_height).max(min_height).min(max_height);
            calculated as f32
        })
    };

    // Main container
    container(
        v_stack((
            // Text input area
            container(
                TextInputBuilder::new()
                    .build_editor(editor.clone())
                    .placeholder(|| "Message the agent... (@ for context, / for commands)".to_string())
                    .keyboard_navigable()
                    .on_event(EventListener::KeyDown, move |event| {
                        if let Event::KeyDown(key_event) = event {
                            if key_event.key.logical_key == Key::Named(NamedKey::Enter) {
                                if key_event.modifiers.shift() {
                                    // Shift+Enter: insert newline using do_insert for proper cursor update
                                    let doc = editor.doc();
                                    let mut cursor = editor.cursor().get_untracked();
                                    let config = editor.common.config.get_untracked();
                                    doc.do_insert(&mut cursor, "\n", &config);
                                    editor.cursor().set(cursor);
                                    return floem::event::EventPropagation::Stop;
                                } else {
                                    // Enter: send message
                                    let value = agent_for_send.get_input_text();
                                    let is_streaming = agent_streaming.is_current_streaming();
                                    if !value.trim().is_empty() && !is_streaming {
                                        on_send_key(value);
                                        agent_for_send.clear_input();
                                    }
                                    // Stop propagation for plain Enter
                                    return floem::event::EventPropagation::Stop;
                                }
                            }
                        }
                        // Continue propagation for all other keys
                        floem::event::EventPropagation::Continue
                    })
                    .style(move |s| {
                        s.width_full()
                            .min_height(input_height.get())
                    }),
            )
            .style(move |s| {
                s.width_full()
                    .padding_horiz(12.0)
                    .padding_top(12.0)
                    .padding_bottom(4.0)
            }),

            // Bottom toolbar (inside the input container)
            h_stack((
                // Left: Model selector
                model_selector(agent_for_model, config),

                // Center: Spacer + hint text
                empty().style(|s| s.flex_grow(1.0)),

                label(|| "↵ Send · ⇧↵ New line")
                    .style(move |s| {
                        let config = config.get();
                        s.font_size(10.0)
                            .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.4))
                            .margin_right(8.0)
                    }),

                // Right: Send/Stop button
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
                        let size = 14.0_f32;
                        let is_streaming = agent_streaming3.is_current_streaming();
                        let color = if is_streaming {
                            config.color(LapceColor::EDITOR_BACKGROUND)
                        } else if is_empty2.get() {
                            config.color(LapceColor::EDITOR_BACKGROUND).with_alpha(0.6)
                        } else {
                            config.color(LapceColor::EDITOR_BACKGROUND)
                        };
                        s.width(size)
                            .height(size)
                            .color(color)
                    }),
                )
                .style(move |s| {
                    let config = config.get();
                    let is_streaming = agent_streaming4.is_current_streaming();
                    let is_empty_val = is_empty.get();

                    let bg_color = if is_streaming {
                        config.color(LapceColor::LAPCE_ERROR)
                    } else if is_empty_val {
                        config.color(LapceColor::LAPCE_ICON_ACTIVE).with_alpha(0.3)
                    } else {
                        config.color(LapceColor::LAPCE_ICON_ACTIVE)
                    };

                    s.padding(6.0)
                        .border_radius(6.0)
                        .background(bg_color)
                        .cursor(if !is_empty_val || is_streaming {
                            CursorStyle::Pointer
                        } else {
                            CursorStyle::Default
                        })
                        .hover(move |s| {
                            if !is_empty_val || is_streaming {
                                s.background(bg_color.with_alpha(0.8))
                            } else {
                                s
                            }
                        })
                })
                .on_click_stop(move |_| {
                    let is_streaming = agent_for_btn.is_current_streaming();
                    if is_streaming {
                        on_stop();
                    } else {
                        let value = agent_for_btn.get_input_text();
                        if !value.trim().is_empty() {
                            on_send_btn(value);
                            agent_for_btn.clear_input();
                        }
                    }
                }),
            ))
            .style(move |s| {
                s.width_full()
                    .padding_horiz(8.0)
                    .padding_bottom(8.0)
                    .align_items(AlignItems::Center)
            }),
        ))
        .style(|s| s.width_full().flex_direction(FlexDirection::Column)),
    )
    .style(move |s| {
        let config = config.get();
        s.margin_horiz(16.0)
            .margin_bottom(16.0)
            .border(1.5)
            .border_radius(12.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .background(config.color(LapceColor::PANEL_BACKGROUND))
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
            .padding(12.0)
            .margin_horiz(16.0)
            .background(config.color(LapceColor::LAPCE_ERROR).with_alpha(0.1))
            .border_radius(8.0)
            .margin_bottom(8.0);
        if has_error {
            s
        } else {
            s.display(Display::None)
        }
    })
    .debug_name("Error Banner")
}
