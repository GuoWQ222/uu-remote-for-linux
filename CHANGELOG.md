# Changelog

All notable changes to this project are documented here.

## [1.1.4] - 2026-07-31

- Add a process-local controller focus stabilizer for `bin/GameViewer.exe`.
  Intra-process transitions between the video window, quality/frame-rate
  menus, Qt helper windows, and the D3D11 render surface no longer deactivate
  the outer viewer under Wine/XWayland.
- Debounce UU's low-level keyboard-hook teardown for 350 ms and reuse the
  existing hook when focus immediately returns. A real departure from the UU
  process still releases the hook after the grace interval.
- Add a module-qualified injection watchdog, runtime diagnostics, and a real
  Wine regression probe covering focus grouping, border activation, and hook
  reuse/release behavior.

## [1.0.7] - 2026-07-31

- Add a native Linux system-proxy bridge that maps active GNOME, KDE,
  environment-variable, PAC, and WPAD settings into Wine's standard Internet
  Settings registry before UU starts.
- Strip proxy credentials from bridge output and logs; authenticated proxy
  users are directed to UU's existing manual proxy form.
- Remove proxy environment variables from UU's Wine client, server, and health
  processes so “No proxy”, “System proxy”, and “Manual” remain independent.

## [1.0.6] - 2026-07-31

- Make the native Linux tray follow UU's “Exit program” close policy even when
  Wine leaves a stale `GameViewer.exe` wrapper process after the application
  has completed its own shutdown.
- Monitor UU's append-only client log for the confirmed
  `quitApplication` event, then stop the isolated Wine prefix and remove the
  StatusNotifierItem immediately. Decoder-selection restarts remain excluded
  from this cleanup path.
- Retain process-disappearance monitoring as a fallback and make it perform
  full prefix cleanup whenever `CloseOption=1`.

## [1.0.5] - 2026-07-31

- Add a narrowly scoped Win64 PowerShell compatibility bridge for UU's
  Windows-only `Get-NetAdapterPowerManagement`,
  `Get-NetAdapterAdvancedProperty`, and WMI Wake-on-LAN checks. The bridge
  reports enabled properties only after native Linux Magic Packet and PCI
  wake-up have both been verified.
- Deploy and verify the bridge during normal startup, repair, and safe updates,
  and expose its actual query status through `--diagnose`.
- Fix the guided remote-wake wizard incorrectly reporting “no wired adapter”
  after IP Helper had already found a connected physical Ethernet interface.

## [1.0.4] - 2026-07-31

- Match UU's complete physical-Ethernet filter in the Win64 Wake-on-LAN
  compatibility hook, including 802.3 media, hardware connector, access, and
  connection fields returned by `GetIfTable2`.
- Record real IP Helper API call and patch counts in `--diagnose`, so a loaded
  hook can be distinguished from a WOL mapping that UU actually consumed.
- Extend the Wine integration probe to enforce the same adapter criteria found
  in UU's production `GetEthernetInterface` routine.

## [1.0.3] - 2026-07-31

- Complete the synthetic Win32 Ethernet adapter with a stable name,
  description, IPv4 prefix, subnet mask, and default gateway. This matches
  UU's real startup filter instead of only the basic IP Helper probe.
- Detect long Wine process names through CSV task output, eliminating the
  false “input injector failed to start” warning.
- Make migration process cleanup race-safe and make receive-directory
  diagnostics independent of an outer filesystem sandbox.

## [1.0.2] - 2026-07-31

- Preload the Win64 input/WOL hook as a static dependency of the Event Log
  compatibility DLL, before `GameViewerServer.exe` executes its first
  instruction. This removes the service-start injection race and makes UU see
  the mapped physical Ethernet adapter during its initial WOL capability scan.
- Stop stale autostart, sleep, update, keyboard, input, and tray helpers from
  the previous project identity even after their data directories have already
  been migrated. This releases inherited Linux sleep inhibitor locks.

## [1.0.1] - 2026-07-31

- Made the native sleep inhibitor follow `setting.ini` directly, so disabling
  “Prevent computer sleep” releases the Linux inhibitor even when UU omits its
  periodic state log.
- Added process-local Win64 IP Helper API mapping for Wake-on-LAN. UU now sees
  the physical Linux Ethernet MAC and gateway while retaining a Clash TUN
  source address used by the active Windows-side route.
- Added diagnostics for the WOL network mapping, drag-and-drop overlay,
  writable mobile-file receive directory, and native close-to-tray takeover.
- Added creation and permission validation for the mobile-file receive
  directory during every compatibility repair.

## [1.0.0] - 2026-07-31

- Renamed every project-owned command, runtime directory, helper, artifact,
  desktop file, application ID, source file, test fixture, environment
  variable, and Debian package to the `uu-remote-for-linux` identity.
- Added one-time migration of the previous Wine prefix, state, cache,
  autostart entry, compatibility backups, and input-hook files.
- Added Debian conflict/replacement metadata so installing the new package
  safely replaces the former package identity.

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
