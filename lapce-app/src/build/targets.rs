//! Build target discovery via UBT

use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};

use super::engine::{find_engine_path, get_build_script};
use super::types::{BuildConfig, BuildTarget, TargetInfoJson};

/// Query available build targets using UBT -Mode=QueryTargets
pub fn find_build_targets(project_path: &Path) -> Result<Vec<BuildTarget>, String> {
    let engine_path = find_engine_path(project_path)?;
    let build_script = get_build_script(&engine_path);

    if !build_script.exists() {
        return Err(format!("Build script not found at: {}", build_script.display()));
    }

    // Run UBT QueryTargets mode
    let output = Command::new(&build_script)
        .arg("-Mode=QueryTargets")
        .arg(format!("-Project={}", project_path.display()))
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .map_err(|e| format!("Failed to execute UBT: {}", e))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("UBT QueryTargets failed: {}", stderr));
    }

    // Read the generated TargetInfo.json
    let project_dir = project_path
        .parent()
        .ok_or("Invalid project path")?;

    let target_info_path = project_dir.join("Intermediate").join("TargetInfo.json");

    if !target_info_path.exists() {
        return Err("TargetInfo.json was not generated".to_string());
    }

    let content = fs::read_to_string(&target_info_path)
        .map_err(|e| format!("Failed to read TargetInfo.json: {}", e))?;

    let target_info: TargetInfoJson = serde_json::from_str(&content)
        .map_err(|e| format!("Failed to parse TargetInfo.json: {}", e))?;

    // Filter to only include project-specific targets (not engine targets)
    let project_targets: Vec<BuildTarget> = target_info
        .targets
        .into_iter()
        .filter(|t| {
            // Engine targets contain "/Engine/" in their path
            !t.path.contains("/Engine/") && !t.path.contains("\\Engine\\")
        })
        .collect();

    Ok(project_targets)
}

/// Find build targets by parsing .Target.cs files directly
/// This is a fallback when UBT QueryTargets is unavailable or slow
pub fn find_build_targets_fast(project_path: &Path) -> Result<Vec<BuildTarget>, String> {
    let project_dir = project_path
        .parent()
        .ok_or("Invalid project path")?;

    let source_dir = project_dir.join("Source");
    if !source_dir.exists() {
        return Ok(Vec::new());
    }

    let mut targets = Vec::new();

    // Look for *.Target.cs files in Source directory
    if let Ok(entries) = fs::read_dir(&source_dir) {
        for entry in entries.filter_map(|e| e.ok()) {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().to_string();

            if name.ends_with(".Target.cs") && path.is_file() {
                let target_name = name.trim_end_matches(".Target.cs").to_string();

                // Try to determine target type from file content
                let target_type = if let Ok(content) = fs::read_to_string(&path) {
                    parse_target_type(&content)
                } else {
                    "Game".to_string()
                };

                targets.push(BuildTarget {
                    name: target_name,
                    path: path.to_string_lossy().to_string(),
                    target_type,
                });
            }
        }
    }

    // Sort: Editor targets first, then alphabetically
    targets.sort_by(|a, b| {
        let a_is_editor = a.target_type == "Editor";
        let b_is_editor = b.target_type == "Editor";
        match (a_is_editor, b_is_editor) {
            (true, false) => std::cmp::Ordering::Less,
            (false, true) => std::cmp::Ordering::Greater,
            _ => a.name.cmp(&b.name),
        }
    });

    Ok(targets)
}

/// Parse target type from .Target.cs file content
fn parse_target_type(content: &str) -> String {
    // Look for: Type = TargetType.Editor; or Type = TargetType.Game;
    for line in content.lines() {
        let line = line.trim();
        if line.contains("Type") && line.contains("TargetType.") {
            if line.contains("TargetType.Editor") {
                return "Editor".to_string();
            } else if line.contains("TargetType.Game") {
                return "Game".to_string();
            } else if line.contains("TargetType.Server") {
                return "Server".to_string();
            } else if line.contains("TargetType.Client") {
                return "Client".to_string();
            } else if line.contains("TargetType.Program") {
                return "Program".to_string();
            }
        }
    }
    "Game".to_string()
}

/// Get all available build configurations
/// These are defined by UE and never change
pub fn get_build_configurations() -> Vec<BuildConfig> {
    BuildConfig::all().to_vec()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_target_type() {
        assert_eq!(
            parse_target_type("Type = TargetType.Editor;"),
            "Editor"
        );
        assert_eq!(
            parse_target_type("Type = TargetType.Game;"),
            "Game"
        );
        assert_eq!(
            parse_target_type("// Some comment\nType = TargetType.Server;"),
            "Server"
        );
    }
}
