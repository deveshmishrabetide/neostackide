//! Engine discovery and path resolution

use std::fs;
use std::path::{Path, PathBuf};

use super::types::InstalledEngine;

#[cfg(target_os = "windows")]
use winreg::enums::*;
#[cfg(target_os = "windows")]
use winreg::RegKey;

/// Discover all installed Unreal Engine versions on the system
pub fn list_installed_engines() -> Vec<InstalledEngine> {
    let mut engines = Vec::new();

    #[cfg(target_os = "windows")]
    {
        // 1. Check Epic Games Launcher installations (versioned)
        if let Ok(hklm) = RegKey::predef(HKEY_LOCAL_MACHINE)
            .open_subkey("SOFTWARE\\EpicGames\\Unreal Engine")
        {
            for key_name in hklm.enum_keys().filter_map(|k| k.ok()) {
                // Skip non-version keys
                if !key_name.chars().next().map(|c| c.is_ascii_digit()).unwrap_or(false) {
                    continue;
                }

                if let Ok(subkey) = hklm.open_subkey(&key_name) {
                    if let Ok(path) = subkey.get_value::<String, _>("InstalledDirectory") {
                        let engine_path = PathBuf::from(&path);
                        if engine_path.exists() {
                            let version = get_engine_version(&engine_path);
                            let display = if let Some(ref v) = version {
                                format!("UE {}", v)
                            } else {
                                format!("UE {}", key_name)
                            };

                            engines.push(InstalledEngine {
                                id: key_name.clone(),
                                display_name: display,
                                path,
                                version,
                                is_source_build: false,
                                is_default: false,
                            });
                        }
                    }
                }
            }
        }

        // 2. Check source builds (GUID-based in HKCU)
        if let Ok(hkcu) = RegKey::predef(HKEY_CURRENT_USER)
            .open_subkey("SOFTWARE\\Epic Games\\Unreal Engine\\Builds")
        {
            for (name, _value) in hkcu.enum_values().filter_map(|v| v.ok()) {
                if let Ok(path_str) = hkcu.get_value::<String, _>(&name) {
                    let engine_path = PathBuf::from(&path_str);
                    if engine_path.exists() {
                        let version = get_engine_version(&engine_path);
                        let display = if let Some(ref v) = version {
                            format!("UE {} (Source)", v)
                        } else {
                            "UE Source Build".to_string()
                        };

                        engines.push(InstalledEngine {
                            id: name,
                            display_name: display,
                            path: path_str,
                            version,
                            is_source_build: true,
                            is_default: false,
                        });
                    }
                }
            }
        }
    }

    #[cfg(target_os = "macos")]
    {
        // Check common macOS installation paths
        let search_paths = [
            "/Users/Shared/Epic Games",
            "/Applications/Epic Games",
        ];

        for base in &search_paths {
            let base_path = PathBuf::from(base);
            if let Ok(entries) = fs::read_dir(&base_path) {
                for entry in entries.filter_map(|e| e.ok()) {
                    let path = entry.path();
                    let name = entry.file_name().to_string_lossy().to_string();

                    // Look for UE_X.X folders
                    if name.starts_with("UE_") && path.is_dir() {
                        let version = get_engine_version(&path);
                        let id = name.trim_start_matches("UE_").to_string();

                        engines.push(InstalledEngine {
                            id: id.clone(),
                            display_name: format!("UE {}", version.as_ref().unwrap_or(&id)),
                            path: path.to_string_lossy().to_string(),
                            version,
                            is_source_build: false,
                            is_default: false,
                        });
                    }
                }
            }
        }
    }

    #[cfg(target_os = "linux")]
    {
        // Check home directory for engine installations
        if let Ok(home) = std::env::var("HOME") {
            let home_path = PathBuf::from(&home);

            // Check for UnrealEngine-X.X folders
            if let Ok(entries) = fs::read_dir(&home_path) {
                for entry in entries.filter_map(|e| e.ok()) {
                    let path = entry.path();
                    let name = entry.file_name().to_string_lossy().to_string();

                    if name.starts_with("UnrealEngine") && path.is_dir() {
                        let version = get_engine_version(&path);
                        let id = name.trim_start_matches("UnrealEngine-").to_string();

                        engines.push(InstalledEngine {
                            id: id.clone(),
                            display_name: format!("UE {}", version.as_ref().unwrap_or(&id)),
                            path: path.to_string_lossy().to_string(),
                            version,
                            is_source_build: true,
                            is_default: false,
                        });
                    }
                }
            }
        }
    }

    // Sort by version (newest first)
    engines.sort_by(|a, b| {
        let va = a.version.as_ref().map(|v| v.as_str()).unwrap_or("0.0.0");
        let vb = b.version.as_ref().map(|v| v.as_str()).unwrap_or("0.0.0");
        vb.cmp(va)
    });

    // Mark the first one as default
    if let Some(first) = engines.first_mut() {
        first.is_default = true;
    }

    engines
}

/// Get engine path from .uproject file's EngineAssociation
pub fn find_engine_path(project_path: &Path) -> Result<PathBuf, String> {
    let project_data = fs::read_to_string(project_path)
        .map_err(|e| format!("Failed to read project file: {}", e))?;

    let project_json: serde_json::Value = serde_json::from_str(&project_data)
        .map_err(|e| format!("Failed to parse project file: {}", e))?;

    let engine_association = project_json["EngineAssociation"]
        .as_str()
        .ok_or("EngineAssociation not found in .uproject file")?;

    get_engine_path_for_association(engine_association)
        .ok_or_else(|| format!("Could not find engine for association: {}", engine_association))
}

/// Find engine installation path for a given EngineAssociation
fn get_engine_path_for_association(association: &str) -> Option<PathBuf> {
    #[cfg(target_os = "windows")]
    {
        // Check registry for Epic Games Launcher installations
        if let Ok(hklm) = RegKey::predef(HKEY_LOCAL_MACHINE)
            .open_subkey(format!("SOFTWARE\\EpicGames\\Unreal Engine\\{}", association))
        {
            if let Ok(path) = hklm.get_value::<String, _>("InstalledDirectory") {
                let path = PathBuf::from(path);
                if path.exists() {
                    return Some(path);
                }
            }
        }

        // Check for source builds (GUID-based associations)
        if let Ok(hkcu) = RegKey::predef(HKEY_CURRENT_USER)
            .open_subkey("SOFTWARE\\Epic Games\\Unreal Engine\\Builds")
        {
            if let Ok(path) = hkcu.get_value::<String, _>(association) {
                let path = PathBuf::from(path);
                if path.exists() {
                    return Some(path);
                }
            }
        }
    }

    #[cfg(target_os = "macos")]
    {
        // Check common macOS installation paths
        let paths = [
            format!("/Users/Shared/Epic Games/UE_{}", association),
            format!("/Applications/Epic Games/UE_{}", association),
        ];
        for p in &paths {
            let path = PathBuf::from(p);
            if path.exists() {
                return Some(path);
            }
        }
    }

    #[cfg(target_os = "linux")]
    {
        // Check common Linux paths
        if let Ok(home) = std::env::var("HOME") {
            let path = PathBuf::from(format!("{}/UnrealEngine-{}", home, association));
            if path.exists() {
                return Some(path);
            }
        }
    }

    None
}

/// Get engine version from Build.version file
pub fn get_engine_version(engine_path: &Path) -> Option<String> {
    let version_file = engine_path.join("Engine").join("Build").join("Build.version");
    if let Ok(content) = fs::read_to_string(&version_file) {
        if let Ok(json) = serde_json::from_str::<serde_json::Value>(&content) {
            let major = json.get("MajorVersion")?.as_u64()?;
            let minor = json.get("MinorVersion")?.as_u64()?;
            let patch = json.get("PatchVersion")?.as_u64()?;
            return Some(format!("{}.{}.{}", major, minor, patch));
        }
    }
    None
}

/// Get the build script path for the engine
pub fn get_build_script(engine_path: &Path) -> PathBuf {
    engine_path
        .join("Engine")
        .join("Build")
        .join("BatchFiles")
        .join(if cfg!(windows) {
            "Build.bat"
        } else if cfg!(target_os = "macos") {
            "Mac/Build.sh"
        } else {
            "Linux/Build.sh"
        })
}

/// Get the editor executable path for the engine
pub fn get_editor_executable(engine_path: &Path) -> PathBuf {
    #[cfg(target_os = "windows")]
    {
        engine_path
            .join("Engine")
            .join("Binaries")
            .join("Win64")
            .join("UnrealEditor.exe")
    }

    #[cfg(target_os = "macos")]
    {
        engine_path
            .join("Engine")
            .join("Binaries")
            .join("Mac")
            .join("UnrealEditor.app")
            .join("Contents")
            .join("MacOS")
            .join("UnrealEditor")
    }

    #[cfg(target_os = "linux")]
    {
        engine_path
            .join("Engine")
            .join("Binaries")
            .join("Linux")
            .join("UnrealEditor")
    }
}

/// Get current platform name for UE
pub fn get_current_platform() -> &'static str {
    #[cfg(target_os = "windows")]
    { "Win64" }
    #[cfg(target_os = "macos")]
    { "Mac" }
    #[cfg(target_os = "linux")]
    { "Linux" }
}
