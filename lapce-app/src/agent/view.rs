//! Main agent view layout

use std::rc::Rc;

use floem::{
    View,
    reactive::SignalGet,
    style::Display,
    views::{Decorators, container, h_stack, label},
};

use super::content::agent_content;
use super::sidebar::agent_left_sidebar;
use crate::{
    config::color::LapceColor,
    window_tab::WindowTabData,
};

/// Placeholder right sidebar for context info
fn agent_right_sidebar(
    window_tab_data: Rc<WindowTabData>,
) -> impl View {
    let config = window_tab_data.common.config;
    let agent = window_tab_data.agent.clone();
    let is_open = agent.right_sidebar_open;

    container(
        label(|| "Context").style(move |s| {
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
        agent_content(window_tab_data.clone()),
        agent_right_sidebar(window_tab_data),
    ))
    .style(move |s| {
        let config = config.get();
        s.size_full()
            .background(config.color(LapceColor::EDITOR_BACKGROUND))
    })
    .debug_name("Agent View")
}
