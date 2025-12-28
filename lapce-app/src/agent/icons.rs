//! Provider icons for the agent sidebar

use super::data::AgentProvider;

/// Claude AI icon SVG
pub const CLAUDE_ICON: &str = include_str!("icons/claude.svg");

/// OpenAI/Codex icon SVG (dark theme version)
pub const OPENAI_ICON: &str = include_str!("icons/openai_dark.svg");

/// Google Gemini icon SVG
pub const GEMINI_ICON: &str = include_str!("icons/gemini.svg");

/// Get the SVG icon string for a provider
pub fn provider_icon(provider: AgentProvider) -> &'static str {
    match provider {
        AgentProvider::Claude => CLAUDE_ICON,
        AgentProvider::Codex => OPENAI_ICON,
        AgentProvider::Gemini => GEMINI_ICON,
    }
}
