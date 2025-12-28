//! Main agent view layout

use std::rc::Rc;
use std::sync::Arc;

use floem::{
    View,
    reactive::{ReadSignal, SignalGet},
    style::Display,
    views::{Decorators, container, h_stack, text},
};

use super::sidebar::agent_left_sidebar;
use crate::{
    config::{LapceConfig, color::LapceColor},
    window_tab::WindowTabData,
};

/// Placeholder content area for the agent chat
fn agent_content(
    config: ReadSignal<Arc<LapceConfig>>,
) -> impl View {
    container(
        text("Agent Chat Content").style(move |s| {
            let config = config.get();
            s.font_size(16.0)
                .color(config.color(LapceColor::EDITOR_FOREGROUND).with_alpha(0.5))
        }),
    )
    .style(move |s| {
        let config = config.get();
        s.flex_grow(1.0)
            .height_full()
            .flex_col()
            .items_center()
            .justify_center()
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
    .debug_name("Agent Content")
}

/// Placeholder right sidebar for context info
fn agent_right_sidebar(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;
    let agent = window_tab_data.agent.clone();
    let is_open = agent.right_sidebar_open;

    container(
        text("Context").style(move |s| {
            let config = config.get();
            s.font_size(13.0)
                .font_weight(floem::text::Weight::SEMIBOLD)
                .color(config.color(LapceColor::PANEL_FOREGROUND))
                .padding(10.0)
        }),
    )
    .style(move |s| {
        let config = config.get();
        let s = s
            .width(200.0)
            .height_full()
            .flex_col()
            .border_left(1.0)
            .border_color(config.color(LapceColor::LAPCE_BORDER))
            .background(config.color(LapceColor::PANEL_BACKGROUND));
        if !is_open.get() {
            s.display(Display::None)
        } else {
            s
        }
    })
    .debug_name("Agent Right Sidebar")
}

/// Main agent view with sidebars and content area
pub fn agent_view(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;

    h_stack((
        agent_left_sidebar(window_tab_data.clone()),
        agent_content(config),
        agent_right_sidebar(window_tab_data),
    ))
    .style(move |s| {
        let config = config.get();
        s.size_full()
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
    .debug_name("Agent View")
}
