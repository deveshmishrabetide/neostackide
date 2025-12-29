//! Build execution and editor launching

use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Instant;

use crossbeam_channel::Sender;

use super::engine::{find_engine_path, get_build_script, get_current_platform, get_editor_executable};
use super::types::{BuildConfig, BuildEvent, BuildEventType, BuildResult};

/// Global build runner state
static BUILD_RUNNER: std::sync::OnceLock<Mutex<Option<BuildRunnerState>>> = std::sync::OnceLock::new();

struct BuildRunnerState {
    child: Child,
    cancel_flag: Arc<AtomicBool>,
}

fn get_build_runner() -> &'static Mutex<Option<BuildRunnerState>> {
    BUILD_RUNNER.get_or_init(|| Mutex::new(None))
}

/// Build runner for managing build processes
pub struct BuildRunner {
    event_sender: Sender<BuildEvent>,
}

impl BuildRunner {
    pub fn new(event_sender: Sender<BuildEvent>) -> Self {
        Self { event_sender }
    }

    /// Emit a build event
    fn emit(&self, event_type: BuildEventType, message: String, progress: Option<f32>) {
        let event = BuildEvent {
            event_type,
            message,
            progress,
            timestamp: now_ms(),
        };
        let _ = self.event_sender.send(event);
    }

    /// Build a project target (blocking with streaming events)
    pub fn build(
        &self,
        project_path: &Path,
        target_name: &str,
        config: BuildConfig,
    ) -> Result<BuildResult, String> {
        let start_time = Instant::now();

        // Check if a build is already in progress
        {
            let build_state = get_build_runner().lock().unwrap();
            if build_state.is_some() {
                return Err("A build is already in progress".to_string());
            }
        }

        // Emit started event
        self.emit(
            BuildEventType::Started,
            format!("Building {} ({} {})", target_name, config, get_current_platform()),
            None,
        );

        let engine_path = find_engine_path(project_path)?;
        let build_script = get_build_script(&engine_path);

        if !build_script.exists() {
            let msg = format!("Build script not found at: {}", build_script.display());
            self.emit(BuildEventType::Failed, msg.clone(), None);
            return Err(msg);
        }

        let project_dir = project_path
            .parent()
            .ok_or("Invalid project path")?;

        // Spawn build process
        let mut cmd = Command::new(&build_script);
        cmd.arg(target_name)
            .arg(get_current_platform())
            .arg(config.as_str())
            .arg(format!("-Project={}", project_path.display()))
            .arg("-Progress")
            .arg("-NoHotReloadFromIDE")
            .current_dir(project_dir)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());

        // On Windows, create a new process group for proper termination
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            cmd.creation_flags(0x00000200); // CREATE_NEW_PROCESS_GROUP
        }

        let mut child = cmd.spawn().map_err(|e| {
            let msg = format!("Failed to start build: {}", e);
            self.emit(BuildEventType::Failed, msg.clone(), None);
            msg
        })?;

        // Create cancellation flag
        let cancel_flag = Arc::new(AtomicBool::new(false));
        let cancel_flag_reader = cancel_flag.clone();

        // Take stdout and stderr
        let stdout = child.stdout.take();
        let stderr = child.stderr.take();

        // Store child process for cancellation
        {
            let mut build_state = get_build_runner().lock().unwrap();
            *build_state = Some(BuildRunnerState {
                child,
                cancel_flag: cancel_flag.clone(),
            });
        }

        // Collect output
        let mut full_output = String::new();
        let mut full_error = String::new();
        let mut was_cancelled = false;

        // Stream stdout in a thread
        let event_sender = self.event_sender.clone();
        let stdout_handle = stdout.map(|stdout| {
            let cancel_flag = cancel_flag_reader.clone();
            thread::spawn(move || {
                let reader = BufReader::new(stdout);
                let mut output = String::new();
                for line in reader.lines() {
                    if cancel_flag.load(Ordering::Relaxed) {
                        break;
                    }
                    if let Ok(line) = line {
                        output.push_str(&line);
                        output.push('\n');

                        // Parse progress and emit events
                        let progress = parse_build_progress(&line);
                        let event_type = if line.contains("error") || line.contains("Error") {
                            BuildEventType::Error
                        } else if line.contains("warning") || line.contains("Warning") {
                            BuildEventType::Warning
                        } else if progress.is_some() {
                            BuildEventType::Progress
                        } else {
                            BuildEventType::Output
                        };

                        let _ = event_sender.send(BuildEvent {
                            event_type,
                            message: line,
                            progress,
                            timestamp: now_ms(),
                        });
                    }
                }
                output
            })
        });

        // Stream stderr in a thread
        let event_sender = self.event_sender.clone();
        let stderr_handle = stderr.map(|stderr| {
            let cancel_flag = cancel_flag_reader;
            thread::spawn(move || {
                let reader = BufReader::new(stderr);
                let mut error = String::new();
                for line in reader.lines() {
                    if cancel_flag.load(Ordering::Relaxed) {
                        break;
                    }
                    if let Ok(line) = line {
                        error.push_str(&line);
                        error.push('\n');

                        let _ = event_sender.send(BuildEvent {
                            event_type: BuildEventType::Error,
                            message: line,
                            progress: None,
                            timestamp: now_ms(),
                        });
                    }
                }
                error
            })
        });

        // Wait for output threads
        if let Some(handle) = stdout_handle {
            if let Ok(output) = handle.join() {
                full_output = output;
            }
        }
        if let Some(handle) = stderr_handle {
            if let Ok(error) = handle.join() {
                full_error = error;
            }
        }

        // Get the child process back and wait for it
        let duration = start_time.elapsed();
        let result = {
            let mut build_state = get_build_runner().lock().unwrap();
            if let Some(mut state) = build_state.take() {
                was_cancelled = state.cancel_flag.load(Ordering::Relaxed);

                if was_cancelled {
                    // Kill the process
                    let _ = state.child.kill();
                    let _ = state.child.wait();
                    BuildResult {
                        success: false,
                        exit_code: -1,
                        output: full_output,
                        error: "Build cancelled by user".to_string(),
                        duration_ms: duration.as_millis() as u64,
                    }
                } else {
                    // Wait for process to complete
                    match state.child.wait() {
                        Ok(status) => BuildResult {
                            success: status.success(),
                            exit_code: status.code().unwrap_or(-1),
                            output: full_output,
                            error: full_error,
                            duration_ms: duration.as_millis() as u64,
                        },
                        Err(e) => BuildResult {
                            success: false,
                            exit_code: -1,
                            output: full_output,
                            error: format!("Build process failed: {}", e),
                            duration_ms: duration.as_millis() as u64,
                        },
                    }
                }
            } else {
                BuildResult {
                    success: false,
                    exit_code: -1,
                    output: full_output,
                    error: "Build state was unexpectedly cleared".to_string(),
                    duration_ms: duration.as_millis() as u64,
                }
            }
        };

        // Emit completion event
        if was_cancelled {
            self.emit(BuildEventType::Cancelled, "Build cancelled by user".to_string(), None);
        } else if result.success {
            self.emit(
                BuildEventType::Completed,
                format!("Build completed in {:.1}s", duration.as_secs_f32()),
                Some(100.0),
            );
        } else {
            self.emit(
                BuildEventType::Failed,
                format!("Build failed with exit code {}", result.exit_code),
                None,
            );
        }

        Ok(result)
    }
}

/// Build a project (convenience function)
pub fn build_project(
    project_path: &Path,
    target_name: &str,
    config: BuildConfig,
    event_sender: Sender<BuildEvent>,
) -> Result<BuildResult, String> {
    let runner = BuildRunner::new(event_sender);
    runner.build(project_path, target_name, config)
}

/// Cancel an in-progress build
pub fn cancel_build() -> bool {
    let build_state = get_build_runner().lock().unwrap();
    if let Some(ref state) = *build_state {
        state.cancel_flag.store(true, Ordering::Relaxed);
        true
    } else {
        false
    }
}

/// Check if a build is in progress
pub fn is_building() -> bool {
    let build_state = get_build_runner().lock().unwrap();
    build_state.is_some()
}

/// Launch the Unreal Editor with the project
pub fn launch_editor(project_path: &Path, ws_port: Option<u16>) -> Result<(), String> {
    let engine_path = find_engine_path(project_path)?;
    let editor_exe = get_editor_executable(&engine_path);

    if !editor_exe.exists() {
        return Err(format!("UnrealEditor not found at: {}", editor_exe.display()));
    }

    let mut cmd = Command::new(&editor_exe);
    cmd.arg(project_path);

    // Add NeoStack IDE connection argument if port provided
    if let Some(port) = ws_port {
        cmd.arg(format!("-NeoStackIDE=ws://127.0.0.1:{}", port));
    }

    cmd.spawn()
        .map_err(|e| format!("Failed to launch editor: {}", e))?;

    Ok(())
}

/// Parse UBT output line for progress info
fn parse_build_progress(line: &str) -> Option<f32> {
    // UBT outputs progress like: [1/42] Compiling Module.cpp
    if let Some(captures) = line.strip_prefix('[') {
        if let Some(end) = captures.find(']') {
            let nums = &captures[..end];
            if let Some((current, total)) = nums.split_once('/') {
                if let (Ok(c), Ok(t)) = (current.trim().parse::<f32>(), total.trim().parse::<f32>()) {
                    if t > 0.0 {
                        return Some((c / t) * 100.0);
                    }
                }
            }
        }
    }
    None
}

/// Get current timestamp in ms
fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_build_progress() {
        assert_eq!(parse_build_progress("[1/10] Compiling"), Some(10.0));
        assert_eq!(parse_build_progress("[5/10] Linking"), Some(50.0));
        assert_eq!(parse_build_progress("[10/10] Done"), Some(100.0));
        assert_eq!(parse_build_progress("Some other output"), None);
    }
}
