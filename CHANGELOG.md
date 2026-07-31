# Changelog

All notable changes to this project are documented here.

## [0.9.2] - 2026-07-31

- Fixed the remaining invisible-cursor path by patching the independent
  `streamer.dll` GDI capture module, not only `GameViewerServer.exe`.
- Forced a stable shared arrow cursor while the native bridge is active so
  `GetIconInfo` can always provide usable cursor bitmap and hotspot data.
- Extended the real Wine injection test with a separate `streamer.dll` probe
  that verifies module-local `SendInput`, cursor coordinates, visibility, and
  cursor bitmap extraction.

## [0.9.1] - 2026-07-31

- Fixed a controlled-host cursor desynchronization where native XTest movement
  succeeded but UU's GDI capture kept reporting Wine's stale cursor position.
- Added `GetCursorInfo` and `GetCursorPos` coordinate synchronization to the
  Win64 input hook.
- Added hook-version enforcement and mouse-move, mouse-button, and keyboard
  event counters to diagnostics.

## [0.9.0] - 2026-07-31

- Added an authenticated Win64 `SendInput` hook and native X11/XTest input
  daemon for Linux controlled-host mouse and keyboard support.
- Added a same-architecture injection watchdog that reloads the hook after a
  `GameViewerServer.exe` restart without relying on Wine AppInit behavior.
- Added automatic stuck-key/button release, per-launch token rotation, and
  remote cursor visibility correction.
- Added lifecycle supervision, diagnostics, package integration, and
  update-safe redeployment for the input bridge.

## [0.8.1] - 2026-07-30

- Renamed the public project and application display name to **UU Remote for
  Linux** / **UU 远程（Linux 版本）**.
- Made tray and lint validation portable to headless GitHub-hosted runners.

## [0.8.0] - 2026-07-30

- Added an X11 focus-aware keyboard bridge for `Super+Space`.
- Added exact restoration after focus loss, normal exit, or crash recovery.

## [0.7.2] - 2026-07-30

- Added decoder selection to the native tray menu with validated automatic
  restart.

## [0.7.1] - 2026-07-30

- Updated desktop and tray identification to the official UU Remote icon.

## [0.7.0] - 2026-07-30

- Added a native AppIndicator/StatusNotifierItem tray proxy.
- Fixed Wine XEmbed tooltip encoding and first-menu focus failures.

## [0.6.0] - 2026-07-30

- Added hash-gated automatic updates, compatibility-bridge reapplication,
  validation, and transactional rollback.

## [0.5.1] - 2026-07-30

- Added user-level systemd supervision for UU server and health processes.

## [0.5.0] - 2026-07-30

- Added native Wake-on-LAN configuration and a version-locked UU capability
  bridge.

## [0.4.0] - 2026-07-30

- Bridged the upstream prevent-sleep switch to `systemd-logind`.

## [0.3.3] - 2026-07-30

- Completed live Wine registry to XDG login-autostart synchronization.

## [0.3.0] - 2026-07-30

- Added CPU/GPU discovery, a graphical decoder selector, per-GPU NVDEC
  capability queries, and NVIDIA multi-GPU binding.

## [0.2.0] - 2026-07-30

- Added the experimental NVIDIA NVDEC-to-D3D11 CPU-bounce bridge.

## [0.1.0] - 2026-07-30

- Initial Ubuntu/Debian controller-side validation release.
