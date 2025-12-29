//! UE5 Build System Module
//!
//! Provides build system integration for Unreal Engine projects:
//! - Engine discovery (installed UE versions)
//! - Target discovery via UBT QueryTargets
//! - Async build execution with streaming output
//! - Editor launching
//! - Build cancellation

mod engine;
mod runner;
mod targets;
mod types;

pub use engine::{
    find_engine_path,
    get_build_script,
    get_current_platform,
    get_editor_executable,
    get_engine_version,
    list_installed_engines,
};

pub use runner::{
    build_project,
    cancel_build,
    launch_editor,
    BuildRunner,
};

pub use targets::{
    find_build_targets,
    find_build_targets_fast,
    get_build_configurations,
};

pub use types::{
    BuildConfig,
    BuildEvent,
    BuildEventType,
    BuildPlatform,
    BuildResult,
    BuildState,
    BuildTarget,
    InstalledEngine,
};
