//! Authentication view component

use std::rc::Rc;
use std::sync::Arc;

use floem::{
    View,
    event::EventListener,
    peniko::Color,
    reactive::{RwSignal, SignalGet, SignalWith},
    style::{AlignItems, CursorStyle, Display, FlexDirection, JustifyContent},
    views::{container, dyn_container, empty, h_stack, label, stack, svg, v_stack, Decorators},
};

use crate::config::{color::LapceColor, LapceConfig};

use super::service::AuthService;
use super::state::{AuthData, AuthState, LoginFlowState};

/// Main auth view - shows login UI when unauthenticated
pub fn auth_view(
    auth_data: AuthData,
    auth_service: Rc<AuthService>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    let login_flow = auth_data.login_flow;

    container(
        v_stack((
            // Logo placeholder - using text for now
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
                    s.font_size(24.0)
                        .font_weight(floem::text::Weight::BOLD)
                        .color(cfg.color(LapceColor::EDITOR_FOREGROUND))
                        .margin_bottom(10.0)
                }),
            // Subtitle
            label(|| "Sign in to continue")
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(14.0)
                        .color(cfg.color(LapceColor::EDITOR_DIM))
                        .margin_bottom(40.0)
                }),
            // Login flow content
            login_flow_view(auth_data.clone(), auth_service, config),
        ))
        .style(|s| s.align_items(AlignItems::Center)),
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

fn login_flow_view(
    auth_data: AuthData,
    auth_service: Rc<AuthService>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    let login_flow = auth_data.login_flow;

    v_stack((
        // Error message
        error_message(login_flow, config),
        // Device code display (when in device code flow)
        device_code_display(login_flow, config),
        // Login button (when not in flow)
        login_button(auth_data.clone(), auth_service.clone(), config),
        // Refresh status button (when in flow)
        refresh_status_button(auth_data.clone(), auth_service.clone(), config),
        // Cancel button (when in flow)
        cancel_button(auth_data, auth_service, config),
    ))
    .style(|s| s.align_items(AlignItems::Center))
}

fn error_message(
    login_flow: RwSignal<LoginFlowState>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    container(
        label(move || login_flow.with(|f| f.error.clone().unwrap_or_default()))
            .style(move |s| {
                let cfg = config.get();
                s.font_size(13.0)
                    .color(cfg.color(LapceColor::LAPCE_ERROR))
            }),
    )
    .style(move |s| {
        let has_error = login_flow.with(|f| f.error.is_some());
        s.margin_bottom(20.0)
            .apply_if(!has_error, |s| s.display(Display::None))
    })
}

fn device_code_display(
    login_flow: RwSignal<LoginFlowState>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    v_stack((
        label(|| "Enter this code in your browser:")
            .style(move |s| {
                let cfg = config.get();
                s.font_size(14.0)
                    .color(cfg.color(LapceColor::EDITOR_DIM))
                    .margin_bottom(15.0)
            }),
        // User code box
        container(
            label(move || login_flow.with(|f| f.user_code.clone().unwrap_or_default()))
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(28.0)
                        .font_weight(floem::text::Weight::BOLD)
                        .font_family("monospace".to_string())
                        .color(cfg.color(LapceColor::EDITOR_FOREGROUND))
                }),
        )
        .style(move |s| {
            let cfg = config.get();
            s.padding(20.0)
                .border(2.0)
                .border_color(cfg.color(LapceColor::LAPCE_BORDER))
                .border_radius(8.0)
                .background(cfg.color(LapceColor::PANEL_BACKGROUND))
                .margin_bottom(20.0)
        }),
        label(|| "Waiting for authorization...")
            .style(move |s| {
                let cfg = config.get();
                s.font_size(13.0)
                    .color(cfg.color(LapceColor::EDITOR_DIM))
            }),
    ))
    .style(move |s| {
        let has_code = login_flow.with(|f| f.user_code.is_some());
        s.align_items(AlignItems::Center)
            .apply_if(!has_code, |s| s.display(Display::None))
    })
}

fn login_button(
    auth_data: AuthData,
    auth_service: Rc<AuthService>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    let login_flow = auth_data.login_flow;
    let auth_data_clone = auth_data.clone();
    let auth_service_clone = auth_service.clone();

    container(
        label(|| "Sign In with Browser")
            .style(move |s| s.font_size(14.0).font_weight(floem::text::Weight::SEMIBOLD).color(Color::WHITE)),
    )
    .on_click_stop(move |_| {
        auth_service_clone.request_device_code(&auth_data_clone);
        // Poll authorization - the service handles its own background threading
        auth_service.poll_authorization(&auth_data);
    })
    .style(move |s| {
        let cfg = config.get();
        let is_logging_in = login_flow.with(|f| f.is_logging_in);
        s.padding_horiz(30.0)
            .padding_vert(12.0)
            .border_radius(6.0)
            .background(cfg.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND))
            .cursor(CursorStyle::Pointer)
            .hover(|s| {
                s.background(
                    cfg.color(LapceColor::LAPCE_BUTTON_PRIMARY_BACKGROUND)
                        .with_alpha(0.8),
                )
            })
            .apply_if(is_logging_in, |s| s.display(Display::None))
    })
}

fn refresh_status_button(
    auth_data: AuthData,
    auth_service: Rc<AuthService>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    let login_flow = auth_data.login_flow;
    let auth_data_clone = auth_data.clone();
    let auth_service_clone = auth_service.clone();

    container(
        label(|| "Refresh Status")
            .style(move |s| {
                let cfg = config.get();
                s.font_size(13.0)
                    .font_weight(floem::text::Weight::MEDIUM)
                    .color(cfg.color(LapceColor::EDITOR_FOREGROUND))
            }),
    )
    .on_click_stop(move |_| {
        auth_service_clone.check_authorization_once(&auth_data_clone);
    })
    .style(move |s| {
        let cfg = config.get();
        let is_logging_in = login_flow.with(|f| f.is_logging_in);
        s.margin_top(20.0)
            .padding_horiz(20.0)
            .padding_vert(10.0)
            .cursor(CursorStyle::Pointer)
            .border_radius(6.0)
            .border(1.0)
            .border_color(cfg.color(LapceColor::LAPCE_BORDER))
            .hover(|s| s.background(cfg.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
            .apply_if(!is_logging_in, |s| s.display(Display::None))
    })
}

fn cancel_button(
    auth_data: AuthData,
    auth_service: Rc<AuthService>,
    config: RwSignal<Arc<LapceConfig>>,
) -> impl View {
    let login_flow = auth_data.login_flow;

    container(
        label(|| "Cancel")
            .style(move |s| {
                let cfg = config.get();
                s.font_size(13.0).color(cfg.color(LapceColor::EDITOR_DIM))
            }),
    )
    .on_click_stop(move |_| {
        auth_service.cancel_login(&auth_data);
    })
    .style(move |s| {
        let cfg = config.get();
        let is_logging_in = login_flow.with(|f| f.is_logging_in);
        s.margin_top(10.0)
            .padding(10.0)
            .cursor(CursorStyle::Pointer)
            .border_radius(4.0)
            .hover(|s| s.background(cfg.color(LapceColor::PANEL_HOVERED_BACKGROUND)))
            .apply_if(!is_logging_in, |s| s.display(Display::None))
    })
}

/// Loading view shown during auth initialization
pub fn loading_view(config: RwSignal<Arc<LapceConfig>>) -> impl View {
    container(
        v_stack((
            label(|| "Loading...")
                .style(move |s| {
                    let cfg = config.get();
                    s.font_size(16.0)
                        .color(cfg.color(LapceColor::EDITOR_DIM))
                }),
        ))
        .style(|s| s.align_items(AlignItems::Center)),
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
