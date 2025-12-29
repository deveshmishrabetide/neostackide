//! Authentication module for Lapce
//!
//! Provides device code flow authentication with offline-first design.

pub mod service;
pub mod state;
pub mod storage;
pub mod view;

pub use service::AuthService;
pub use state::{AuthData, AuthState, LoginFlowState, User};
pub use storage::AuthTokens;
pub use view::{auth_view, loading_view};
