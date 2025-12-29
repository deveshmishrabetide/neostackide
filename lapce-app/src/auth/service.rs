//! Auth service for HTTP operations

use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, Result};
use floem::ext_event::{create_ext_action, create_signal_from_channel};
use floem::reactive::create_effect;
use floem::reactive::{SignalGet, SignalUpdate};
use serde::Deserialize;

use super::state::{AuthData, AuthState, LoginFlowState, User};
use super::storage::{AuthTokens, StoredAuth, TokenStorage, API_BASE, POLL_INTERVAL_MS, POLL_TIMEOUT_MS};

/// Device code response from server
#[derive(Debug, Deserialize)]
pub struct DeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub auth_url: String,
    #[serde(default)]
    pub interval: Option<u64>,
}

/// Poll response from server
#[derive(Debug, Deserialize)]
pub struct PollResponse {
    pub status: String,
    pub access_token: Option<String>,
    pub refresh_token: Option<String>,
    pub expires_in: Option<u64>,
    pub user: Option<UserResponse>,
}

#[derive(Debug, Deserialize)]
pub struct UserResponse {
    pub id: String,
    pub email: String,
    pub username: Option<String>,
    pub name: Option<String>,
}

/// Refresh token response
#[derive(Debug, Deserialize)]
pub struct RefreshResponse {
    pub access_token: String,
    pub expires_in: u64,
    #[serde(default)]
    pub refresh_token: Option<String>,
}

/// Auth service for all authentication operations
#[derive(Clone)]
pub struct AuthService {
    storage: Arc<TokenStorage>,
}

impl AuthService {
    pub fn new() -> Result<Self> {
        Ok(Self {
            storage: Arc::new(TokenStorage::new()?),
        })
    }

    /// Initialize auth - load stored tokens
    pub fn initialize(&self, auth_data: &AuthData) {
        let storage = self.storage.clone();
        let state = auth_data.state;

        let send = create_ext_action(auth_data.scope, move |result: AuthState| {
            state.set(result);
        });

        std::thread::spawn(move || {
            let result = match storage.load() {
                Ok(Some(data)) => {
                    // Check if refresh token is expired
                    if data.tokens.is_refresh_expired() {
                        let _ = storage.clear();
                        AuthState::Unauthenticated
                    } else {
                        AuthState::Authenticated {
                            user: data.user,
                            tokens: Arc::new(data.tokens),
                            online: true, // Assume online, will update on first API call
                        }
                    }
                }
                Ok(None) => AuthState::Unauthenticated,
                Err(e) => {
                    tracing::error!("Failed to load tokens: {:?}", e);
                    AuthState::Unauthenticated
                }
            };
            send(result);
        });
    }

    /// Request a device code to start login flow
    pub fn request_device_code(&self, auth_data: &AuthData) {
        let login_flow = auth_data.login_flow;
        let scope = auth_data.scope;

        login_flow.update(|flow| {
            flow.is_logging_in = true;
            flow.error = None;
        });

        let send = create_ext_action(scope, move |result: Result<DeviceCodeResponse>| {
            match result {
                Ok(response) => {
                    // Open browser
                    let _ = open::that(&response.auth_url);

                    login_flow.update(|flow| {
                        flow.user_code = Some(response.user_code);
                        flow.device_code = Some(response.device_code);
                        flow.auth_url = Some(response.auth_url);
                    });
                }
                Err(e) => {
                    login_flow.update(|flow| {
                        flow.is_logging_in = false;
                        flow.error = Some(e.to_string());
                    });
                }
            }
        });

        std::thread::spawn(move || {
            let result = Self::request_device_code_sync();
            send(result);
        });
    }

    /// Poll for authorization
    pub fn poll_authorization(&self, auth_data: &AuthData) {
        let login_flow = auth_data.login_flow;
        let state = auth_data.state;
        let storage = self.storage.clone();
        let scope = auth_data.scope;

        let device_code = match login_flow.get_untracked().device_code {
            Some(code) => code,
            None => return,
        };

        let send = create_ext_action(scope, move |result: Result<(AuthTokens, User)>| {
            match result {
                Ok((tokens, user)) => {
                    let tokens = Arc::new(tokens);
                    state.set(AuthState::Authenticated {
                        user,
                        tokens,
                        online: true,
                    });
                    login_flow.set(LoginFlowState::default());
                }
                Err(e) => {
                    let msg = e.to_string();
                    if msg.contains("expired") || msg.contains("timeout") {
                        login_flow.update(|flow| {
                            flow.is_logging_in = false;
                            flow.error = Some("Login expired. Please try again.".to_string());
                            flow.user_code = None;
                            flow.device_code = None;
                        });
                    }
                    // For "pending" we continue polling (handled in poll loop)
                }
            }
        });

        std::thread::spawn(move || {
            let start = std::time::Instant::now();

            while start.elapsed().as_millis() < POLL_TIMEOUT_MS as u128 {
                match Self::poll_once_sync(&device_code) {
                    Ok(response) => {
                        if response.status == "authorized" {
                            if let (Some(access_token), Some(refresh_token), Some(user)) =
                                (response.access_token, response.refresh_token, response.user)
                            {
                                let now = std::time::SystemTime::now()
                                    .duration_since(std::time::UNIX_EPOCH)
                                    .unwrap()
                                    .as_secs();

                                let tokens = AuthTokens {
                                    access_token,
                                    refresh_token,
                                    access_expires_at: now + response.expires_in.unwrap_or(900),
                                    refresh_expires_at: now + 86400 * 30, // 30 days
                                };

                                let user = User {
                                    id: user.id,
                                    email: user.email.clone(),
                                    username: user.username.unwrap_or_else(|| {
                                        user.email.split('@').next().unwrap_or("user").to_string()
                                    }),
                                    name: user.name,
                                };

                                // Save tokens
                                let _ = storage.save(&StoredAuth {
                                    tokens: tokens.clone(),
                                    user: user.clone(),
                                });

                                send(Ok((tokens, user)));
                                return;
                            }
                        } else if response.status == "expired" {
                            send(Err(anyhow!("Login expired")));
                            return;
                        }
                        // "pending" - continue polling
                    }
                    Err(e) => {
                        tracing::warn!("Poll error (will retry): {:?}", e);
                    }
                }

                std::thread::sleep(Duration::from_millis(POLL_INTERVAL_MS));
            }

            send(Err(anyhow!("Login timeout")));
        });
    }

    /// Cancel login flow
    pub fn cancel_login(&self, auth_data: &AuthData) {
        auth_data.login_flow.set(LoginFlowState::default());
    }

    /// Check authorization once (manual refresh)
    pub fn check_authorization_once(&self, auth_data: &AuthData) {
        let login_flow = auth_data.login_flow;
        let state = auth_data.state;
        let storage = self.storage.clone();
        let scope = auth_data.scope;

        let device_code = match login_flow.get_untracked().device_code {
            Some(code) => code,
            None => return,
        };

        let send = create_ext_action(scope, move |result: Result<(AuthTokens, User)>| {
            match result {
                Ok((tokens, user)) => {
                    let tokens = Arc::new(tokens);
                    state.set(AuthState::Authenticated {
                        user,
                        tokens,
                        online: true,
                    });
                    login_flow.set(LoginFlowState::default());
                }
                Err(e) => {
                    let msg = e.to_string();
                    if msg.contains("expired") {
                        login_flow.update(|flow| {
                            flow.is_logging_in = false;
                            flow.error = Some("Login expired. Please try again.".to_string());
                            flow.user_code = None;
                            flow.device_code = None;
                        });
                    } else if !msg.contains("pending") {
                        login_flow.update(|flow| {
                            flow.error = Some(format!("Check failed: {}", msg));
                        });
                    }
                    // "pending" - user hasn't authorized yet, no error to show
                }
            }
        });

        std::thread::spawn(move || {
            match Self::poll_once_sync(&device_code) {
                Ok(response) => {
                    if response.status == "authorized" {
                        if let (Some(access_token), Some(refresh_token), Some(user)) =
                            (response.access_token, response.refresh_token, response.user)
                        {
                            let now = std::time::SystemTime::now()
                                .duration_since(std::time::UNIX_EPOCH)
                                .unwrap()
                                .as_secs();

                            let tokens = AuthTokens {
                                access_token,
                                refresh_token,
                                access_expires_at: now + response.expires_in.unwrap_or(900),
                                refresh_expires_at: now + 86400 * 30,
                            };

                            let user = User {
                                id: user.id,
                                email: user.email.clone(),
                                username: user.username.unwrap_or_else(|| {
                                    user.email.split('@').next().unwrap_or("user").to_string()
                                }),
                                name: user.name,
                            };

                            let _ = storage.save(&StoredAuth {
                                tokens: tokens.clone(),
                                user: user.clone(),
                            });

                            send(Ok((tokens, user)));
                        }
                    } else if response.status == "expired" {
                        send(Err(anyhow!("Login expired")));
                    } else {
                        send(Err(anyhow!("pending")));
                    }
                }
                Err(e) => {
                    send(Err(e));
                }
            }
        });
    }

    /// Logout - clear tokens and reset state
    pub fn logout(&self, auth_data: &AuthData) {
        let _ = self.storage.clear();
        auth_data.state.set(AuthState::Unauthenticated);
        auth_data.login_flow.set(LoginFlowState::default());
    }

    /// Start background token refresh
    /// Spawns a thread that periodically checks and refreshes tokens
    pub fn start_background_refresh(&self, auth_data: &AuthData) {
        let storage = self.storage.clone();
        let state = auth_data.state;

        // Use channel + signal pattern for repeated background updates
        let (tx, rx) = std::sync::mpsc::channel::<StoredAuth>();
        let refresh_signal = create_signal_from_channel(rx);

        // Create effect to update state when signal changes
        create_effect(move |_| {
            if let Some(data) = refresh_signal.get() {
                state.update(|s| {
                    if let AuthState::Authenticated { tokens, user, .. } = s {
                        *tokens = Arc::new(data.tokens);
                        *user = data.user;
                    }
                });
            }
        });

        // Background refresh loop
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(Duration::from_secs(60)); // Check every minute

                if let Ok(Some(data)) = storage.load() {
                    if data.tokens.needs_refresh() && !data.tokens.is_refresh_expired() {
                        match Self::refresh_tokens_sync(&data.tokens.refresh_token) {
                            Ok(new_tokens) => {
                                let new_data = StoredAuth {
                                    tokens: new_tokens,
                                    user: data.user.clone(),
                                };
                                let _ = storage.save(&new_data);
                                let _ = tx.send(new_data);
                            }
                            Err(e) => {
                                tracing::error!("Token refresh failed: {:?}", e);
                                // Only logout on explicit 401
                                if e.to_string().contains("401") {
                                    let _ = storage.clear();
                                }
                            }
                        }
                    }
                }
            }
        });
    }

    // Synchronous helper methods for background threads

    fn request_device_code_sync() -> Result<DeviceCodeResponse> {
        let url = format!("{}/auth/device", API_BASE);
        let client = reqwest::blocking::Client::new();
        let resp = client
            .post(&url)
            .header("Content-Type", "application/json")
            .timeout(Duration::from_secs(10))
            .send()?;

        if !resp.status().is_success() {
            return Err(anyhow!("Failed to request device code: {}", resp.status()));
        }
        let response: DeviceCodeResponse = resp.json()?;
        Ok(response)
    }

    fn poll_once_sync(device_code: &str) -> Result<PollResponse> {
        let url = format!("{}/auth/device/{}/poll", API_BASE, device_code);
        let client = reqwest::blocking::Client::new();
        let resp = client
            .get(&url)
            .timeout(Duration::from_secs(10))
            .send()?;

        if !resp.status().is_success() {
            return Err(anyhow!("Poll failed: {}", resp.status()));
        }
        let response: PollResponse = resp.json()?;
        Ok(response)
    }

    fn refresh_tokens_sync(refresh_token: &str) -> Result<AuthTokens> {
        let url = format!("{}/auth/refresh", API_BASE);
        let client = reqwest::blocking::Client::new();
        let resp = client
            .post(&url)
            .header("Content-Type", "application/json")
            .json(&serde_json::json!({ "refresh_token": refresh_token }))
            .timeout(Duration::from_secs(10))
            .send()?;

        if resp.status() == reqwest::StatusCode::UNAUTHORIZED {
            return Err(anyhow!("401: Refresh token invalid"));
        }

        if !resp.status().is_success() {
            return Err(anyhow!("Refresh failed: {}", resp.status()));
        }

        let data: RefreshResponse = resp.json()?;
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();

        Ok(AuthTokens {
            access_token: data.access_token,
            refresh_token: data.refresh_token.unwrap_or_else(|| refresh_token.to_string()),
            access_expires_at: now + data.expires_in,
            refresh_expires_at: now + 86400 * 30, // 30 days
        })
    }
}

impl Default for AuthService {
    fn default() -> Self {
        Self::new().expect("Failed to create auth service")
    }
}
