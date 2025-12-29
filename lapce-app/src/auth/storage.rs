//! Secure token storage with encryption

use std::path::PathBuf;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

use lapce_core::directory::Directory;

/// Configuration for the auth module
pub const API_BASE: &str = "https://api.neostack.dev";
pub const POLL_INTERVAL_MS: u64 = 5000;
pub const POLL_TIMEOUT_MS: u64 = 300000; // 5 minutes
pub const TOKEN_REFRESH_BUFFER_SECS: u64 = 300; // 5 minutes before expiry

/// Auth tokens with expiration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AuthTokens {
    pub access_token: String,
    pub refresh_token: String,
    /// Unix timestamp when access token expires
    pub access_expires_at: u64,
    /// Unix timestamp when refresh token expires
    pub refresh_expires_at: u64,
}

impl AuthTokens {
    /// Check if the access token is expired
    pub fn is_access_expired(&self) -> bool {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or(Duration::ZERO)
            .as_secs();
        now >= self.access_expires_at
    }

    /// Check if access token needs refresh (within buffer period)
    pub fn needs_refresh(&self) -> bool {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or(Duration::ZERO)
            .as_secs();
        now + TOKEN_REFRESH_BUFFER_SECS >= self.access_expires_at
    }

    /// Check if refresh token is expired
    pub fn is_refresh_expired(&self) -> bool {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or(Duration::ZERO)
            .as_secs();
        now >= self.refresh_expires_at
    }
}

/// Encrypted storage wrapper
#[derive(Debug, Clone, Serialize, Deserialize)]
struct EncryptedData {
    /// Base64-encoded nonce
    nonce: String,
    /// Base64-encoded ciphertext
    ciphertext: String,
}

/// Stored auth data (tokens + user info)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StoredAuth {
    pub tokens: AuthTokens,
    pub user: super::state::User,
}

/// Token storage manager
pub struct TokenStorage {
    path: PathBuf,
}

impl TokenStorage {
    /// Create a new token storage instance
    pub fn new() -> Result<Self> {
        let auth_dir = Directory::config_directory()
            .ok_or_else(|| anyhow!("Cannot get config directory"))?
            .join("auth");

        if !auth_dir.exists() {
            std::fs::create_dir_all(&auth_dir)?;
        }

        Ok(Self {
            path: auth_dir.join("tokens.enc"),
        })
    }

    /// Get the machine-specific encryption key
    fn get_encryption_key(&self) -> Result<[u8; 32]> {
        use sha2::{Digest, Sha256};

        // Get machine-specific identifier
        let machine_id = self.get_machine_id()?;

        // Derive key using SHA256
        let mut hasher = Sha256::new();
        hasher.update(machine_id.as_bytes());
        hasher.update(b"lapce-neostack-auth-v1"); // Application salt

        let result = hasher.finalize();
        let mut key = [0u8; 32];
        key.copy_from_slice(&result);
        Ok(key)
    }

    /// Get a machine-specific identifier
    #[cfg(target_os = "macos")]
    fn get_machine_id(&self) -> Result<String> {
        use std::process::Command;
        let output = Command::new("ioreg")
            .args(["-rd1", "-c", "IOPlatformExpertDevice"])
            .output()?;
        let output_str = String::from_utf8_lossy(&output.stdout);
        // Extract IOPlatformUUID
        for line in output_str.lines() {
            if line.contains("IOPlatformUUID") {
                if let Some(uuid) = line.split('"').nth(3) {
                    return Ok(uuid.to_string());
                }
            }
        }
        // Fallback
        Ok("macos-default-id".to_string())
    }

    #[cfg(target_os = "linux")]
    fn get_machine_id(&self) -> Result<String> {
        std::fs::read_to_string("/etc/machine-id")
            .or_else(|_| std::fs::read_to_string("/var/lib/dbus/machine-id"))
            .map(|s| s.trim().to_string())
            .or_else(|_| Ok("linux-default-id".to_string()))
    }

    #[cfg(target_os = "windows")]
    fn get_machine_id(&self) -> Result<String> {
        use std::process::Command;
        let output = Command::new("wmic")
            .args(["csproduct", "get", "UUID"])
            .output()?;
        let output_str = String::from_utf8_lossy(&output.stdout);
        output_str
            .lines()
            .nth(1)
            .map(|s| s.trim().to_string())
            .ok_or_else(|| anyhow!("Could not get machine ID"))
    }

    #[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
    fn get_machine_id(&self) -> Result<String> {
        Ok("unknown-platform-id".to_string())
    }

    /// Save auth data to encrypted storage
    pub fn save(&self, data: &StoredAuth) -> Result<()> {
        use base64::{engine::general_purpose::STANDARD, Engine};
        use chacha20poly1305::{
            aead::{Aead, KeyInit},
            XChaCha20Poly1305, XNonce,
        };
        use rand::RngCore;

        let key = self.get_encryption_key()?;
        let cipher = XChaCha20Poly1305::new_from_slice(&key)
            .map_err(|e| anyhow!("Failed to create cipher: {}", e))?;

        // Generate random nonce
        let mut nonce_bytes = [0u8; 24];
        rand::thread_rng().fill_bytes(&mut nonce_bytes);
        let nonce = XNonce::from_slice(&nonce_bytes);

        // Serialize and encrypt
        let plaintext = serde_json::to_vec(data)?;
        let ciphertext = cipher
            .encrypt(nonce, plaintext.as_slice())
            .map_err(|e| anyhow!("Encryption failed: {}", e))?;

        let encrypted = EncryptedData {
            nonce: STANDARD.encode(nonce_bytes),
            ciphertext: STANDARD.encode(ciphertext),
        };

        let file_data = serde_json::to_string_pretty(&encrypted)?;
        std::fs::write(&self.path, file_data)?;

        Ok(())
    }

    /// Load auth data from encrypted storage
    pub fn load(&self) -> Result<Option<StoredAuth>> {
        use base64::{engine::general_purpose::STANDARD, Engine};
        use chacha20poly1305::{
            aead::{Aead, KeyInit},
            XChaCha20Poly1305, XNonce,
        };

        if !self.path.exists() {
            return Ok(None);
        }

        let file_data = std::fs::read_to_string(&self.path)?;
        let encrypted: EncryptedData = serde_json::from_str(&file_data)?;

        let key = self.get_encryption_key()?;
        let cipher = XChaCha20Poly1305::new_from_slice(&key)
            .map_err(|e| anyhow!("Failed to create cipher: {}", e))?;

        let nonce_bytes = STANDARD.decode(&encrypted.nonce)?;
        let nonce = XNonce::from_slice(&nonce_bytes);
        let ciphertext = STANDARD.decode(&encrypted.ciphertext)?;

        let plaintext = cipher
            .decrypt(nonce, ciphertext.as_slice())
            .map_err(|e| anyhow!("Decryption failed: {}", e))?;

        let data: StoredAuth = serde_json::from_slice(&plaintext)?;
        Ok(Some(data))
    }

    /// Clear stored auth data
    pub fn clear(&self) -> Result<()> {
        if self.path.exists() {
            std::fs::remove_file(&self.path)?;
        }
        Ok(())
    }
}

impl Default for TokenStorage {
    fn default() -> Self {
        Self::new().expect("Failed to create token storage")
    }
}
