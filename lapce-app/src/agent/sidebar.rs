//! Agent left sidebar with chat history

use std::rc::Rc;
use std::sync::Arc;

use chrono::{DateTime, Utc};
use floem::{
    View,
    action::show_context_menu,
    menu::{Menu, MenuItem},
    peniko::Color,
    reactive::{ReadSignal, SignalGet, SignalUpdate, SignalWith},
    style::{AlignItems, CursorStyle, Display},
    views::{Decorators, container, dyn_stack, empty, h_stack, scroll, svg, text, v_stack},
};

use super::{
    data::{AgentData, AgentProvider, Chat, ChatStatus},
    icons::provider_icon,
};
use crate::{
    app::clickable_icon,
    config::{LapceConfig, color::LapceColor, icon::LapceIcons},
    window_tab::WindowTabData,
};

/// Width of the left sidebar in pixels
const SIDEBAR_WIDTH: f64 = 240.0;

/// Group chats by date category
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum DateGroup {
    Today,
    Yesterday,
    Older,
}

impl DateGroup {
    pub fn label(&self) -> &'static str {
        match self {
            DateGroup::Today => "TODAY",
            DateGroup::Yesterday => "YESTERDAY",
            DateGroup::Older => "OLDER",
        }
    }

    pub fn from_datetime(dt: DateTime<Utc>) -> Self {
        let now = Utc::now();
        let today = now.date_naive();
        let chat_date = dt.date_naive();

        if chat_date == today {
            DateGroup::Today
        } else if chat_date == today.pred_opt().unwrap_or(today) {
            DateGroup::Yesterday
        } else {
            DateGroup::Older
        }
    }
}

/// Group chats by date
fn group_chats_by_date(chats: &[Chat]) -> Vec<(DateGroup, Vec<Chat>)> {
    let mut today = Vec::new();
    let mut yesterday = Vec::new();
    let mut older = Vec::new();

    for chat in chats {
        match DateGroup::from_datetime(chat.created_at) {
            DateGroup::Today => today.push(chat.clone()),
            DateGroup::Yesterday => yesterday.push(chat.clone()),
            DateGroup::Older => older.push(chat.clone()),
        }
    }

    let mut groups = Vec::new();
    if !today.is_empty() {
        groups.push((DateGroup::Today, today));
    }
    if !yesterday.is_empty() {
        groups.push((DateGroup::Yesterday, yesterday));
    }
    if !older.is_empty() {
        groups.push((DateGroup::Older, older));
    }
    groups
}

/// Header with "Chats" title, new chat split button, and collapse button
fn sidebar_header(
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let agent_for_new = agent.clone();
    let agent_for_dropdown = agent.clone();
    let agent_for_collapse = agent.clone();
    let current_provider = agent.provider;

    h_stack((
        // Title
        text("Chats").style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .font_weight(floem::text::Weight::SEMIBOLD)
                .color(config.color(LapceColor::PANEL_FOREGROUND))
                .flex_grow(1.0)
        }),
        // Split button container: [+ with provider icon] [▼]
        h_stack((
            // Main new chat button with provider icon overlay
            container(
                h_stack((
                    // Plus icon
                    svg(move || config.get().ui_svg(LapceIcons::ADD))
                        .style(move |s| {
                            let config = config.get();
                            let size = config.ui.icon_size() as f32;
                            s.width(size)
                                .height(size)
                                .color(config.color(LapceColor::LAPCE_ICON_ACTIVE))
                        }),
                    // Small provider icon overlay
                    svg(move || provider_icon(current_provider.get()).to_string())
                        .style(move |s| {
                            s.width(10.0)
                                .height(10.0)
                                .margin_left(-4.0)
                                .margin_top(6.0)
                        }),
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
            // Separator line
            empty().style(move |s| {
                let config = config.get();
                s.width(1.0)
                    .height(14.0)
                    .background(config.color(LapceColor::LAPCE_BORDER))
            }),
            // Dropdown arrow button
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
        // Collapse button
        clickable_icon(
            || LapceIcons::SIDEBAR_LEFT,
            move || {
                agent_for_collapse.left_sidebar_open.update(|open| *open = !*open);
            },
            || false,
            || false,
            || "Toggle Sidebar",
            config,
        ),
    ))
    .style(move |s| {
        let config = config.get();
        s.width_full()
            .padding(10.0)
            .align_items(AlignItems::Center)
            .gap(4.0)
            .border_bottom(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
    })
}

/// Date group header (TODAY, YESTERDAY, OLDER)
fn date_group_header(
    group: DateGroup,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    text(group.label()).style(move |s| {
        let config = config.get();
        s.font_size(11.0)
            .font_weight(floem::text::Weight::SEMIBOLD)
            .color(config.color(LapceColor::PANEL_FOREGROUND).with_alpha(0.6))
            .padding_horiz(12.0)
            .padding_vert(8.0)
    })
}

/// Individual chat item row
fn chat_item(
    chat: Chat,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    let chat_id_for_click = chat.id.clone();
    let chat_id_for_style = chat.id.clone();
    let provider = chat.provider;
    let title = chat.title.clone();
    let status = chat.status;
    let current_chat_id = agent.current_chat_id;

    h_stack((
        // Provider icon
        svg(move || provider_icon(provider).to_string())
            .style(move |s| {
                s.width(16.0)
                    .height(16.0)
                    .min_width(16.0)
            }),
        // Chat title
        text(title).style(move |s| {
            let config = config.get();
            s.font_size(12.0)
                .color(config.color(LapceColor::PANEL_FOREGROUND))
                .flex_grow(1.0)
                .text_ellipsis()
                .min_width(0.0)
        }),
        // Status indicator (dot)
        empty().style(move |s| {
            let config = config.get();
            let color = match status {
                ChatStatus::Idle => Color::TRANSPARENT,
                ChatStatus::Streaming => config.color(LapceColor::EDITOR_FOCUS),
                ChatStatus::Completed => config.color(LapceColor::LAPCE_ICON_ACTIVE),
                ChatStatus::Error => config.color(LapceColor::LAPCE_ERROR),
            };
            s.width(6.0)
                .height(6.0)
                .border_radius(3.0)
                .background(color)
                .display(if status == ChatStatus::Idle { Display::None } else { Display::Flex })
        }),
    ))
    .style(move |s| {
        let config = config.get();
        // Check is_active reactively
        let is_active = current_chat_id.with(|id| id.as_ref() == Some(&chat_id_for_style));
        let bg = if is_active {
            config.color(LapceColor::PANEL_CURRENT_BACKGROUND)
        } else {
            Color::TRANSPARENT
        };
        s.width_full()
            .padding_horiz(12.0)
            .padding_vert(8.0)
            .align_items(AlignItems::Center)
            .gap(8.0)
            .background(bg)
            .cursor(CursorStyle::Pointer)
            .hover(move |s| {
                if !is_active {
                    s.background(config.color(LapceColor::PANEL_HOVERED_BACKGROUND))
                } else {
                    s
                }
            })
    })
    .on_click_stop(move |_| {
        agent.select_chat(&chat_id_for_click);
    })
}

/// A group of chats under a date header
fn chat_group(
    group: DateGroup,
    chats: Vec<Chat>,
    agent: AgentData,
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    v_stack((
        date_group_header(group, config),
        dyn_stack(
            move || chats.clone(),
            |chat| chat.id.clone(),
            move |chat| {
                chat_item(chat, agent.clone(), config)
            },
        )
        .style(|s| s.flex_col().width_full()),
    ))
    .style(|s| s.width_full())
}

/// The complete left sidebar for agent mode
pub fn agent_left_sidebar(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;
    let agent = window_tab_data.agent.clone();
    let agent_for_header = agent.clone();
    let is_open = agent.left_sidebar_open;

    container(
        v_stack((
            sidebar_header(agent_for_header, config),
            scroll(
                dyn_stack(
                    move || {
                        agent.chats.with(|chats| {
                            group_chats_by_date(&chats.iter().cloned().collect::<Vec<_>>())
                        })
                    },
                    |(group, _)| group.clone(),
                    move |(group, chats)| {
                        chat_group(group, chats, agent.clone(), config)
                    },
                )
                .style(|s| s.flex_col().width_full()),
            )
            .style(|s| s.flex_grow(1.0).width_full()),
        ))
        .style(|s| s.width_full().height_full()),
    )
    .style(move |s| {
        let config = config.get();
        let s = s
            .width(SIDEBAR_WIDTH)
            .height_full()
            .flex_col()
            .border_right(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .background(config.color(LapceColor::PANEL_BACKGROUND));
        if !is_open.get() {
            s.display(Display::None)
        } else {
            s
        }
    })
    .debug_name("Agent Left Sidebar")
}
