//! Authentication state types

use std::sync::Arc;

use floem::reactive::{RwSignal, Scope, SignalGet};
use serde::{Deserialize, Serialize};

use super::storage::AuthTokens;

/// User information from the auth backend
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct User {
    pub id: String,
    pub email: String,
    pub username: String,
    pub name: Option<String>,
}

/// Main authentication state
#[derive(Debug, Clone)]
pub enum AuthState {
    /// Initial loading - checking stored tokens
    Loading,
    /// No valid tokens, user needs to login
    Unauthenticated,
    /// User is authenticated
    Authenticated {
        user: User,
        tokens: Arc<AuthTokens>,
        /// Whether we have confirmed connectivity to auth server
        online: bool,
    },
}

impl Default for AuthState {
    fn default() -> Self {
        AuthState::Loading
    }
}

/// State of the login flow (device code)
#[derive(Debug, Clone, Default)]
pub struct LoginFlowState {
    /// Whether login is in progress
    pub is_logging_in: bool,
    /// User code to display (for device code flow)
    pub user_code: Option<String>,
    /// Device code (internal, for polling)
    pub device_code: Option<String>,
    /// Auth URL to open in browser
    pub auth_url: Option<String>,
    /// Error message if login failed
    pub error: Option<String>,
}

/// Reactive auth data for Floem UI
#[derive(Clone)]
pub struct AuthData {
    pub state: RwSignal<AuthState>,
    pub login_flow: RwSignal<LoginFlowState>,
    pub scope: Scope,
}

impl AuthData {
    pub fn new(cx: Scope) -> Self {
        Self {
            state: cx.create_rw_signal(AuthState::Loading),
            login_flow: cx.create_rw_signal(LoginFlowState::default()),
            scope: cx,
        }
    }

    pub fn is_authenticated(&self) -> bool {
        matches!(
            self.state.get_untracked(),
            AuthState::Authenticated { .. }
        )
    }

    pub fn is_loading(&self) -> bool {
        matches!(self.state.get_untracked(), AuthState::Loading)
    }

    pub fn get_user(&self) -> Option<User> {
        match self.state.get_untracked() {
            AuthState::Authenticated { user, .. } => Some(user),
            _ => None,
        }
    }
}
