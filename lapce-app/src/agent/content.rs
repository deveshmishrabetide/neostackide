//! Main content area for agent mode

use std::rc::Rc;
use std::sync::Arc;

use floem::{
    View,
    action::show_context_menu,
    menu::{Menu, MenuItem},
    reactive::{ReadSignal, SignalGet, SignalUpdate},
    style::{AlignItems, CursorStyle, Display},
    views::{Decorators, container, empty, h_stack, label, svg, v_stack},
};

use super::data::{AgentData, AgentProvider};
use super::icons::provider_icon;
use super::input::{agent_input, error_banner};
use super::todo_panel::{todo_panel, has_todos};
use super::messages::message_list;
use crate::app::clickable_icon;
use crate::config::{LapceConfig, color::LapceColor, icon::LapceIcons};
use crate::window_tab::WindowTabData;

/// Toolbar shown at top of content area
fn content_toolbar(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;
    let agent = window_tab_data.agent.clone();
    let left_open = agent.left_sidebar_open;
    let right_open = agent.right_sidebar_open;

    h_stack((
        // Left controls (show when sidebar collapsed)
        h_stack((
            // Expand left sidebar button
            container(
                clickable_icon(
                    || LapceIcons::SIDEBAR_LEFT,
                    {
                        let agent = agent.clone();
                        move || {
                            agent.left_sidebar_open.update(|open| *open = true);
                        }
                    },
                    || false,
                    || false,
                    || "Show Chats",
                    config,
                )
            ).style(move |s| {
                if left_open.get() {
                    s.display(Display::None)
                } else {
                    s
                }
            }),
            // New chat button (show when sidebar collapsed)
            new_chat_button(agent.clone(), config, !left_open.get()),
        ))
        .style(|s| s.align_items(AlignItems::Center).gap(4.0)),

        // Center - Chat title and usage
        {
            let agent_title = agent.clone();
            let agent_usage = agent.clone();
            h_stack((
                label(move || {
                    agent_title.current_chat()
                        .map(|c| c.title)
                        .unwrap_or_else(|| "No chat selected".to_string())
                })
                .style(move |s| {
                    let config = config.get();
                    s.font_size(12.0)
                        .font_weight(floem::text::Weight::MEDIUM)
                        .color(config.color(LapceColor::EDITOR_FOREGROUND))
                }),
                // Usage info
                label(move || {
                    agent_usage.current_chat()
                        .map(|c| format_usage(c.cost_usd, c.token_count))
                        .unwrap_or_default()
                })
                .style(move |s| {
                    let config = config.get();
                    s.font_size(11.0)
                        .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.6))
                }),
            ))
            .style(|s| s.flex_grow(1.0).justify_center().align_items(AlignItems::Center).gap(8.0))
        },

        // Right controls (show when sidebar collapsed)
        container(
            clickable_icon(
                || LapceIcons::SIDEBAR_RIGHT,
                {
                    let agent = agent.clone();
                    move || {
                        agent.right_sidebar_open.update(|open| *open = true);
                    }
                },
                || false,
                || false,
                || "Show Context",
                config,
            )
        ).style(move |s| {
            if right_open.get() {
                s.display(Display::None)
            } else {
                s
            }
        }),
    ))
    .style(move |s| {
        let config = config.get();
        s.width_full()
            .height(40.0)
            .padding_horiz(12.0)
            .align_items(AlignItems::Center)
            .border_bottom(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
    })
}

/// New chat split button (similar to sidebar)
fn new_chat_button(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
    visible: bool,
) -> impl View {
    let agent_for_new = agent.clone();
    let agent_for_dropdown = agent.clone();
    let current_provider = agent.provider;

    container(
        h_stack((
            // Main new chat button
            container(
                h_stack((
                    svg(move || config.get().ui_svg(LapceIcons::ADD))
                        .style(move |s| {
                            let config = config.get();
                            let size = config.ui.icon_size() as f32;
                            s.width(size)
                                .height(size)
                                .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                        }),
                    svg(move || provider_icon(current_provider.get()).to_string())
                        .style(|s| s.width(10.0).height(10.0).margin_left(-4.0).margin_top(6.0)),
                ))
            )
            .style(move |s| {
                let config = config.get();
                s.padding(4.0)
                    .border_radius(4.0)
                    .cursor(CursorStyle::Pointer)
                    .hover(|s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
            })
            .on_click_stop(move |_| {
                agent_for_new.new_chat();
            }),
            // Separator
            empty().style(move |s| {
                let config = config.get();
                s.width(1.0)
                    .height(14.0)
                    .background(config.color(LapceColor::LAPCE_BORDER))
            }),
            // Dropdown
            container(
                svg(move || config.get().ui_svg(LapceIcons::ITEM_OPENED))
                    .style(move |s| {
                        let config = config.get();
                        let size = (config.ui.icon_size() as f32) * 0.7;
                        s.width(size)
                            .height(size)
                            .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                    }),
            )
            .style(move |s| {
                let config = config.get();
                s.padding_horiz(2.0)
                    .padding_vert(4.0)
                    .border_radius(4.0)
                    .cursor(CursorStyle::Pointer)
                    .hover(|s| s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
            })
            .on_click_stop(move |_| {
                let agent = agent_for_dropdown.clone();
                let mut menu = Menu::new("");
                for provider in AgentProvider::all() {
                    let p = *provider;
                    let agent = agent.clone();
                    menu = menu.entry(
                        MenuItem::new(provider.display_name()).action(move || {
                            agent.provider.set(p);
                            agent.new_chat();
                        })
                    );
                }
                show_context_menu(menu, None);
            }),
        ))
        .style(move |s| {
            let config = config.get();
            s.align_items(AlignItems::Center)
                .border(1.0)
                .border_radius(4.0)
                .border_color(config.color(LapceColor::LAPCE_BORDER))
        }),
    )
    .style(move |s| {
        if visible {
            s
        } else {
            s.display(Display::None)
        }
    })
}

/// Format usage info
fn format_usage(cost: f64, tokens: u64) -> String {
    if cost == 0.0 && tokens == 0 {
        return String::new();
    }

    let cost_str = if cost < 0.01 {
        format!("${:.4}", cost)
    } else {
        format!("${:.2}", cost)
    };

    let tokens_str = if tokens >= 1000 {
        format!("{:.1}K tokens", tokens as f64 / 1000.0)
    } else {
        format!("{} tokens", tokens)
    };

    format!("{} | {}", cost_str, tokens_str)
}

/// The main content area
pub fn agent_content(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;
    let agent = window_tab_data.agent.clone();
    let agent_for_messages = agent.clone();
    let agent_for_streaming = agent.clone();
    let agent_for_thinking = agent.clone();
    let agent_for_is_streaming = agent.clone();
    let agent_for_error = agent.clone();
    let agent_for_todo = agent.clone();
    let agent_for_input = agent.clone();
    let agent_for_send = agent.clone();
    let agent_for_stop = agent.clone();

    v_stack((
        // Toolbar
        content_toolbar(window_tab_data.clone()),
        // Messages area
        message_list(
            move || agent_for_messages.current_messages(),
            move || agent_for_streaming.current_streaming_text(),
            move || agent_for_thinking.current_streaming_thinking(),
            move || agent_for_is_streaming.is_current_streaming(),
            agent.clone(),
            config,
        ),
        // Error banner
        error_banner(
            move || agent_for_error.error_message.get(),
            {
                let agent = agent.clone();
                move || agent.clear_error()
            },
            config,
        ),
        // Todo panel (above input, connected visually)
        container(
            todo_panel(agent_for_todo, config)
        )
        .style(|s| s.width_full().padding_horiz(16.0)),
        // Input area (connected to todo panel above when present)
        agent_input(
            agent_for_input.clone(),
            config,
            has_todos(&agent_for_input),
            // on_send
            move |prompt: String| {
                let agent = agent_for_send.clone();
                // Update chat title if first message
                if let Some(chat_id) = agent.current_chat_id.get() {
                    let messages = agent.current_messages();
                    if messages.is_empty() {
                        agent.update_chat_title(&chat_id, &prompt);
                    }
                }
                // Send via ACP connection
                agent.send_prompt(prompt);
            },
            // on_stop
            move || {
                let agent = agent_for_stop.clone();
                agent.cancel();
            },
        ),
    ))
    .style(move |s| {
        let config = config.get();
        s.flex_grow(1.0)
            .height_full()
            .flex_col()
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
    .debug_name("Agent Content")
}
