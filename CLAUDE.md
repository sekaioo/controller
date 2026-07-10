# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Controller is a Windows system-tray application (C++23, Win32) that manages a
"kernel" child process: switching between profiles, updating profiles from
remote URLs, starting/stopping the kernel, and optionally blocking network
traffic when the kernel is not running. The app requires administrator
privileges (needed for the firewall rule) and runs as a single instance.

## Build

Uses CMake presets (Ninja generator, MSVC `cl.exe`) with vcpkg for dependencies.
`VCPKG_ROOT` must be set in the environment; the only external dependency is
`rapidjson` (see `vcpkg.json`).

```powershell
# Configure (pick one preset)
cmake --preset x64-msvc-release-static   # or -release-shared, -debug-static, -debug-shared

# Build
cmake --build --preset x64-msvc-release-static
```

Preset naming: `x64-msvc-{debug|release}-{static|shared}` selects build type and
the MSVC runtime linkage (`static` uses the `x64-windows-static` vcpkg triplet).
Build output lands in `out/build/<preset>/`. There is no test suite, linter, or
CI configured — verification is by building and running.

The linker embeds a `requireAdministrator` UAC manifest, so the produced
`controller.exe` triggers an elevation prompt on launch.

## Runtime layout

The executable expects, relative to its own directory:
- `data/config.json` — main config (`CONFIG_FILE` in `include/constants.h`)
- `data/profiles/` — downloaded profile files (`PROFILES_DIR`)
- `lang/<code>.json` — UI strings, e.g. `lang/en-US.json`, `lang/zh-CN.json`

The kernel binary and its config live wherever `config.json` points (`kernel.path`,
`kernel.config_path`). See `README.md` for a `config.json` example.

## Architecture

C++23 **modules** are the primary unit — source files are `.ixx` (module
interface, compiled as `CXX_MODULES`) not `.cpp`. `src/main.cpp` is the only
`.cpp`. `CMakeLists.txt` globs `src/*.cpp` and `src/*.ixx` recursively, so new
modules are picked up automatically (re-run configure). `include/` holds the two
non-module headers included via the global-module fragment (`module;` block):
`constants.h` (paths, mutex/class/rule names) and `resource.h` (Win32 message and
menu IDs).

Startup flow (`src/main.cpp`): load `Config` → initialize `I18n` and
`network_blocker` from it → acquire the single-instance named mutex
(`MutexGuard`) → construct and `run()` a `Service`.
A top-level `ScopeGuard` guarantees the firewall rule is removed on any exit path.

**Module map (`export module` name → file):**
- `components.Service` — owns the hidden message-only main window and the Win32
  message loop; wires everything together. It routes tray/menu commands and the
  custom `WM_*` messages (defined in `resource.h`) to handlers, owns the
  `KernelService` and `TrayManager`, and holds a thread-safe error queue used to
  pass async profile-update errors back to the UI thread via `PostMessage`.
- `components.KernelService` — launches the kernel as a hidden process and runs a
  `std::jthread` monitor. If the kernel exits on its own it posts
  `WM_KERNEL_TERMINATED`; on requested stop it sends a graceful `CTRL_C_EVENT`
  (via `AttachConsole`) with an 8s timeout before `TerminateProcess`. Blocks the
  network whenever the process is not running (fail-closed).
- `components.NetworkBlocker` — free functions in the `network_blocker`
  namespace (`initialize`/`block`/`unblock`) wrapping Windows Firewall COM
  (`INetFwPolicy2`) to add/remove an outbound-block rule named `NetworkBlock`.
  `block()` is gated by the `block_network` config flag; mutex-guarded.
  Uses module-local RAII helpers `ComInit` / `ComPtr`.
- `components.TrayManager` — the `NOTIFYICONDATAW` tray icon and the popup menu,
  built dynamically from the profile names passed in by `Service`.
- `components.Config` — parses/validates `config.json` with rapidjson; the
  validation helpers (`check_field`, type-predicate lambdas) throw
  `std::runtime_error` with a field path on any missing/mistyped field.
- `components.I18n` — singleton loading `lang/<code>.json`; use the free
  functions `tr(key)` (UTF-8 `string`) and `wtr(key)` (`wstring`) for all
  user-facing text. Missing keys return `"MISSING KEY: <key>"` rather than throw.
- `profile.Manager` — free functions. `switch_profile` copies a file from
  `data/profiles/` over the kernel's config path; `update_profile` delegates to
  the downloader. Profile names/menu order derive from `Config::profiles` (a
  sorted `std::map`); `Service` caches them in `profile_names_` and hands them
  to `TrayManager`.
- `profile.Downloader` — `download_profile` shells out to `curl` (hidden
  process, 8s timeout) into a temp file, validates it parses as JSON when the
  target is `.json`, then copies into place.
- `common.Utils` — `utf8_to_wide`, `launch_hidden_process` (the single
  `CreateProcessW` wrapper used for both kernel and curl), `get_executable_directory`.
- `common.Common` — RAII guards: `MutexGuard` (single-instance), `HandleGuard`
  (owns a `HANDLE`), `ScopeGuard` (dismissable cleanup lambda).

## Conventions

- **Wide/UTF-8 boundary:** config and lang JSON are UTF-8 (`string`); all Win32
  API calls use the `...W` variants and `wstring`. Convert at the boundary with
  `utf8_to_wide` / `wtr`. The project builds with `UNICODE`/`_UNICODE`.
- **Error handling:** logic throws `std::runtime_error` with a (usually
  translated) message; the top-level `try/catch` in `WinMain` shows a
  `MessageBoxW`. Async errors from the profile-update background thread are
  pushed onto `Service`'s error queue and surfaced via a posted window message.
- **Threading:** the Win32 message loop runs on the main thread. Background work
  (kernel monitoring, profile updates) runs on separate threads and communicates
  back only via `PostMessageW` to `main_window_` — do not touch UI state directly
  from those threads.
- **Comments** in the codebase are predominantly in Chinese; match the
  surrounding language when editing a file.
- `// @formatter:off/on` markers guard hand-aligned class declarations from the
  IDE formatter — preserve them.
