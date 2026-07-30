# Changelog

All notable changes to this project are documented here.

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
