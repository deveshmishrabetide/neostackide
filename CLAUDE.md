# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NeoStack IDE is a fork of Lapce, a lightning-fast code editor written in pure Rust. It uses:
- **Floem** for the UI framework (reactive, cross-platform)
- **wgpu** for GPU-accelerated rendering
- **Xi-Editor's rope science** for efficient text manipulation
- **Tree-sitter** for syntax highlighting
- **LSP** for language intelligence
- **WASI** for plugin system (plugins compile to WebAssembly)

## Build Commands

```bash
# Development build (fast iteration)
cargo build

# Development run with debug logging
cargo dev

# Fast development build (optimized dependencies, debug app code)
cargo build --profile fastdev
cargo run --profile fastdev --bin lapce

# Release build with LTO (for production)
cargo build --profile release-lto

# Run tests
cargo test --doc --workspace

# Format code (required before submitting PRs)
cargo fmt --all

# Lint with Clippy
cargo clippy
```

## Crate Architecture

The workspace contains 4 main crates:

- **lapce-app**: Main application with UI, editor views, panels, configuration, and window management. Entry point at `lapce-app/src/bin/lapce.rs`.

- **lapce-proxy**: Runs as a separate process handling LSP servers, file operations, search, terminal emulation, and plugin execution via Wasmtime. Entry point at `lapce-proxy/src/bin/lapce-proxy.rs`.

- **lapce-rpc**: Defines RPC protocol and data types for communication between app and proxy.

- **lapce-core**: Core utilities including language definitions, syntax highlighting, directory management, and encoding detection.

## Key Architectural Patterns

### App-Proxy Split
The editor runs as two processes: the UI app (`lapce`) communicates with a proxy process (`lapce-proxy`) via RPC. This enables remote development—the proxy can run on a remote machine while the UI remains local.

### Reactive UI with Floem
The UI uses Floem's reactive signal system. Components like `window_tab.rs`, `main_split.rs`, and `editor.rs` use `RwSignal` and `create_effect` for reactive state management.

### Configuration System
Configuration lives in `lapce-app/src/config/` with defaults in `defaults/`. Settings use TOML format with structured types for color themes, keymaps, editor settings, and icon themes.

### Plugin System
Plugins are WASI modules executed by Wasmtime in `lapce-proxy/src/plugin/`. They communicate via JSON-RPC and can provide LSP capabilities, syntax highlighting, and custom commands.

## Code Style

- Max line width: 85 characters (`.rustfmt.toml`)
- Run `cargo fmt --all` and `cargo clippy` before submitting PRs
- Rust edition 2024, minimum Rust version 1.87.0

## Platform-Specific Notes

- **macOS**: Uses `Makefile` for building `.app` bundles and `.dmg` files
- **Windows**: Links CRT statically (no Visual C++ Redist required)
- **Linux**: Requires system dependencies (see `docs/building-from-source.md`)
