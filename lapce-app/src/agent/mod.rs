//! Agent module for AI coding assistant integration
//!
//! This module provides the UI for the Agent mode, including:
//! - Left sidebar with chat history
//! - Main chat content area
//! - Right sidebar with context info

pub mod data;
pub mod icons;
pub mod sidebar;
pub mod view;

pub use data::{AgentData, AgentProvider, Chat, ChatStatus};
pub use view::agent_view;
