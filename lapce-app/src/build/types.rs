//! Build system types

use serde::{Deserialize, Serialize};

/// Build state
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum BuildState {
    #[default]
    Idle,
    Building,
    Running,
    Cancelling,
}

/// Build target from UBT QueryTargets
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildTarget {
    #[serde(alias = "Name")]
    pub name: String,
    #[serde(alias = "Path")]
    pub path: String,
    #[serde(alias = "Type")]
    pub target_type: String, // Game, Editor, Program, Server, Client
}

/// UBT QueryTargets JSON output
#[derive(Debug, Deserialize)]
pub struct TargetInfoJson {
    #[serde(rename = "Targets")]
    pub targets: Vec<BuildTarget>,
}

/// Build configuration
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub enum BuildConfig {
    Debug,
    DebugGame,
    #[default]
    Development,
    Test,
    Shipping,
}

impl BuildConfig {
    pub fn as_str(&self) -> &'static str {
        match self {
            BuildConfig::Debug => "Debug",
            BuildConfig::DebugGame => "DebugGame",
            BuildConfig::Development => "Development",
            BuildConfig::Test => "Test",
            BuildConfig::Shipping => "Shipping",
        }
    }

    pub fn display_name(&self) -> &'static str {
        self.as_str()
    }

    pub fn description(&self) -> &'static str {
        match self {
            BuildConfig::Debug => "Full debugging, no optimization. Slowest but best for debugging.",
            BuildConfig::DebugGame => "Game code debug, engine optimized. Good balance for game debugging.",
            BuildConfig::Development => "Optimized with some debugging. Recommended for daily development.",
            BuildConfig::Test => "Optimized with test features enabled. For QA testing.",
            BuildConfig::Shipping => "Fully optimized, no debugging. For final release builds.",
        }
    }

    pub fn all() -> &'static [BuildConfig] {
        &[
            BuildConfig::Debug,
            BuildConfig::DebugGame,
            BuildConfig::Development,
            BuildConfig::Test,
            BuildConfig::Shipping,
        ]
    }
}

impl std::fmt::Display for BuildConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

/// Platform with SDK availability
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildPlatform {
    pub name: String,
    pub display_name: String,
    pub available: bool,
    pub sdk_info: Option<String>,
}

/// Result of a build operation
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildResult {
    pub success: bool,
    pub exit_code: i32,
    pub output: String,
    pub error: String,
    pub duration_ms: u64,
}

/// Build event emitted during async build
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildEvent {
    pub event_type: BuildEventType,
    pub message: String,
    pub progress: Option<f32>, // 0.0 - 100.0
    pub timestamp: u64,
}

/// Types of build events
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum BuildEventType {
    Started,
    Progress,
    Output,
    Warning,
    Error,
    Completed,
    Failed,
    Cancelled,
}

/// Information about an installed Unreal Engine
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstalledEngine {
    /// Unique identifier (version like "5.7" or GUID for source builds)
    pub id: String,
    /// Display name (e.g., "UE 5.7.0" or "UE 5.7 (Source)")
    pub display_name: String,
    /// Full path to engine root directory
    pub path: String,
    /// Engine version (e.g., "5.7.0")
    pub version: Option<String>,
    /// Is this a source build (vs Epic Games Launcher install)
    pub is_source_build: bool,
    /// Is this the default/recommended engine
    pub is_default: bool,
}
