//! NeoStack plugin management for Unreal Engine projects
//!
//! This module handles checking, installing, and updating the NeoStack
//! plugin in Unreal Engine projects.

use std::fs;
use std::path::{Path, PathBuf};
use serde::Deserialize;

use super::types::PluginVersionInfo;

/// .uplugin file structure (partial - we only need version info)
#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct UpluginFile {
    /// Numeric version (e.g., 1, 2, 3)
    #[serde(default)]
    version: Option<i32>,
    /// Version name string (e.g., "1.0.0")
    #[serde(default)]
    version_name: Option<String>,
}

/// Get the path to the bundled plugin
///
/// The plugin is bundled in the resources directory alongside the executable.
pub fn bundled_plugin_path() -> Option<PathBuf> {
    // Try to find the bundled plugin relative to the executable
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|p| p.to_path_buf()))?;

    // Check various possible locations
    let candidates = [
        // Development: next to executable
        exe_dir.join("resources").join("plugins").join("NeoStack"),
        // macOS app bundle
        exe_dir.join("..").join("Resources").join("plugins").join("NeoStack"),
        // Linux/Windows installed
        exe_dir.join("share").join("lapce").join("plugins").join("NeoStack"),
    ];

    for candidate in &candidates {
        if candidate.join("NeoStack.uplugin").exists() {
            return Some(candidate.clone());
        }
    }

    // Fallback: check if we're in a development environment
    #[cfg(debug_assertions)]
    {
        // In development, check relative to workspace root
        if let Ok(manifest_dir) = std::env::var("CARGO_MANIFEST_DIR") {
            let dev_path = PathBuf::from(manifest_dir)
                .join("resources")
                .join("plugins")
                .join("NeoStack");
            if dev_path.join("NeoStack.uplugin").exists() {
                return Some(dev_path);
            }
        }
    }

    None
}

/// Check if a directory is an Unreal Engine project
///
/// Returns true if the directory contains a .uproject file.
pub fn is_unreal_project(path: &Path) -> bool {
    if !path.is_dir() {
        return false;
    }

    // Check for any .uproject file in the directory
    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            if let Some(ext) = entry.path().extension() {
                if ext == "uproject" {
                    return true;
                }
            }
        }
    }

    false
}

/// Find the .uproject file in a directory
pub fn find_uproject_file(path: &Path) -> Option<PathBuf> {
    if !path.is_dir() {
        return None;
    }

    if let Ok(entries) = fs::read_dir(path) {
        for entry in entries.flatten() {
            let entry_path = entry.path();
            if let Some(ext) = entry_path.extension() {
                if ext == "uproject" {
                    return Some(entry_path);
                }
            }
        }
    }

    None
}

/// Check plugin version status in a project
///
/// Compares the installed plugin version (if any) with the bundled version.
pub fn check_plugin_version(project_path: &Path) -> Result<PluginVersionInfo, String> {
    // Get bundled plugin info
    let bundled_path = bundled_plugin_path()
        .ok_or_else(|| "Bundled NeoStack plugin not found".to_string())?;

    let bundled_uplugin = bundled_path.join("NeoStack.uplugin");
    let (bundled_version, bundled_version_name) = read_uplugin_version(&bundled_uplugin)?;

    // Check if plugin is installed in project
    let installed_uplugin = project_path
        .join("Plugins")
        .join("NeoStack")
        .join("NeoStack.uplugin");

    if !installed_uplugin.exists() {
        return Ok(PluginVersionInfo {
            installed: false,
            installed_version: None,
            installed_version_name: None,
            bundled_version,
            bundled_version_name,
            update_available: false,
        });
    }

    // Read installed version
    let (installed_version, installed_version_name) = read_uplugin_version(&installed_uplugin)?;
    let update_available = bundled_version > installed_version;

    Ok(PluginVersionInfo {
        installed: true,
        installed_version: Some(installed_version),
        installed_version_name: Some(installed_version_name),
        bundled_version,
        bundled_version_name,
        update_available,
    })
}

/// Install or update the NeoStack plugin in a project
///
/// Copies the bundled plugin to the project's Plugins directory.
/// If the plugin already exists, it will be replaced.
pub fn install_plugin(project_path: &Path) -> Result<(), String> {
    let bundled_path = bundled_plugin_path()
        .ok_or_else(|| "Bundled NeoStack plugin not found".to_string())?;

    if !bundled_path.exists() {
        return Err("Bundled NeoStack plugin directory does not exist".to_string());
    }

    // Ensure Plugins directory exists
    let plugins_dir = project_path.join("Plugins");
    if !plugins_dir.exists() {
        fs::create_dir_all(&plugins_dir)
            .map_err(|e| format!("Failed to create Plugins directory: {}", e))?;
    }

    // Target plugin directory
    let target_plugin = plugins_dir.join("NeoStack");

    // Remove existing plugin if present
    if target_plugin.exists() {
        fs::remove_dir_all(&target_plugin)
            .map_err(|e| format!("Failed to remove existing plugin: {}", e))?;
    }

    // Copy the plugin
    copy_dir_all(&bundled_path, &target_plugin)?;

    tracing::info!(
        "NeoStack plugin installed to {}",
        target_plugin.display()
    );

    Ok(())
}

/// Uninstall the NeoStack plugin from a project
pub fn uninstall_plugin(project_path: &Path) -> Result<(), String> {
    let target_plugin = project_path.join("Plugins").join("NeoStack");

    if !target_plugin.exists() {
        return Err("NeoStack plugin is not installed".to_string());
    }

    fs::remove_dir_all(&target_plugin)
        .map_err(|e| format!("Failed to remove plugin: {}", e))?;

    tracing::info!(
        "NeoStack plugin uninstalled from {}",
        target_plugin.display()
    );

    Ok(())
}

/// Read version information from a .uplugin file
fn read_uplugin_version(path: &Path) -> Result<(i32, String), String> {
    let content = fs::read_to_string(path)
        .map_err(|e| format!("Failed to read .uplugin file: {}", e))?;

    let uplugin: UpluginFile = serde_json::from_str(&content)
        .map_err(|e| format!("Failed to parse .uplugin JSON: {}", e))?;

    Ok((
        uplugin.version.unwrap_or(1),
        uplugin.version_name.unwrap_or_else(|| "1.0".to_string()),
    ))
}

/// Recursively copy a directory
fn copy_dir_all(src: &Path, dst: &Path) -> Result<(), String> {
    if !dst.exists() {
        fs::create_dir_all(dst)
            .map_err(|e| format!("Failed to create directory {}: {}", dst.display(), e))?;
    }

    for entry in fs::read_dir(src).map_err(|e| format!("Failed to read directory: {}", e))? {
        let entry = entry.map_err(|e| format!("Failed to read entry: {}", e))?;
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());

        if src_path.is_dir() {
            copy_dir_all(&src_path, &dst_path)?;
        } else {
            fs::copy(&src_path, &dst_path)
                .map_err(|e| format!("Failed to copy {}: {}", src_path.display(), e))?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    fn create_test_uplugin(dir: &Path, version: i32, version_name: &str) {
        let plugin_dir = dir.join("Plugins").join("NeoStack");
        fs::create_dir_all(&plugin_dir).unwrap();

        let uplugin_content = format!(r#"{{
            "FileVersion": 3,
            "Version": {},
            "VersionName": "{}",
            "FriendlyName": "NeoStack"
        }}"#, version, version_name);

        let mut file = fs::File::create(plugin_dir.join("NeoStack.uplugin")).unwrap();
        file.write_all(uplugin_content.as_bytes()).unwrap();
    }

    fn create_test_uproject(dir: &Path) {
        let content = r#"{"FileVersion": 3, "EngineAssociation": "5.4"}"#;
        let mut file = fs::File::create(dir.join("TestProject.uproject")).unwrap();
        file.write_all(content.as_bytes()).unwrap();
    }

    #[test]
    fn test_is_unreal_project() {
        let temp = TempDir::new().unwrap();

        // Not a UE project initially
        assert!(!is_unreal_project(temp.path()));

        // Create .uproject file
        create_test_uproject(temp.path());
        assert!(is_unreal_project(temp.path()));
    }

    #[test]
    fn test_find_uproject_file() {
        let temp = TempDir::new().unwrap();

        assert!(find_uproject_file(temp.path()).is_none());

        create_test_uproject(temp.path());
        let uproject = find_uproject_file(temp.path());
        assert!(uproject.is_some());
        assert!(uproject.unwrap().ends_with("TestProject.uproject"));
    }

    #[test]
    fn test_read_uplugin_version() {
        let temp = TempDir::new().unwrap();
        create_test_uplugin(temp.path(), 5, "2.1.0");

        let uplugin_path = temp.path()
            .join("Plugins")
            .join("NeoStack")
            .join("NeoStack.uplugin");

        let (version, version_name) = read_uplugin_version(&uplugin_path).unwrap();
        assert_eq!(version, 5);
        assert_eq!(version_name, "2.1.0");
    }

    #[test]
    fn test_copy_dir_all() {
        let src = TempDir::new().unwrap();
        let dst = TempDir::new().unwrap();

        // Create some test files
        fs::create_dir_all(src.path().join("subdir")).unwrap();
        fs::write(src.path().join("file1.txt"), "content1").unwrap();
        fs::write(src.path().join("subdir").join("file2.txt"), "content2").unwrap();

        let target = dst.path().join("copied");
        copy_dir_all(src.path(), &target).unwrap();

        assert!(target.join("file1.txt").exists());
        assert!(target.join("subdir").join("file2.txt").exists());
        assert_eq!(
            fs::read_to_string(target.join("file1.txt")).unwrap(),
            "content1"
        );
    }
}
