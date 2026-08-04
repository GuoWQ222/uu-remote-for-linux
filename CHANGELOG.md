# Changelog

All notable changes to this project are documented here.

## [Unreleased]

## [1.1.15] - 2026-08-04

- Replace the silent `Upgrade.exe` blocker with a Win64 handoff stub that
  creates one prefix-local update request before UU begins shutting down.
- Make the native update watcher atomically validate and consume that request,
  wait briefly for UU's normal exit, terminate only the matching prefix's Qt
  controller if shutdown stalls, and run the existing hash-gated update path.
- Keep the native tray alive during the intentional update handoff, reject
  unsupported upstream versions without touching compatibility files, and
  always restart the last fully verified client after a check or safe rollback.
- Close the launcher's operation-lock descriptor before spawning the restored
  client so the replacement process cannot deadlock while acquiring its own
  startup lock.

## [1.1.14] - 2026-08-04

- Restore the main window through UU's own single-instance activation path so
  Qt updates its `QWidget` visibility state and resumes the embedded WebView2
  surface together with the native Wine/X11 window.
- Turn the native tray request into a short-lived one-shot authorization only.
  The injected hook can permit UU's real show operation but can no longer map
  the top-level HWND directly and expose an unpainted white shell.
- Add Wine and launcher regression coverage proving that an authorization file
  alone never shows the home window, while the subsequent application-driven
  show is accepted exactly once and launches one activation instance.

## [1.1.13] - 2026-08-04

- Replace the controller worker's unconditional 250 ms `WM_NULL` wakeup with
  a coalesced private arbitration message emitted only when the top-level
  window signature or an explicit tray request actually changes.
- Detect a complete `WM_NULL` fingerprint that Wine returns repeatedly despite
  `PM_REMOVE`, quarantine the sticky message, continue dequeuing every other
  message class, and add a bounded wait backoff so Qt can paint and dispatch
  input instead of spinning one CPU core forever.
- Count only a real `WM_UU_UI_HEALTH` dispatch as a UI acknowledgement. Message
  throughput can no longer cancel a pending heartbeat; verified sticky-message
  or empty-queue evidence is carried separately into controller recovery.
- Add Win64 state-machine coverage for sticky-message detection, require v16
  recovery evidence in the native tray, and verify that a deliberately paused
  UI never fabricates heartbeat acknowledgements or triggers a restart.

## [1.1.12] - 2026-08-03

- Prevent the controller watchdog from terminating a healthy incoming remote
  session. Heartbeats are now bound to the exact top-level window generation;
  destroying, replacing, or making message progress on that window cancels
  the pending timeout instead of turning a stale acknowledgement into a
  restart request.
- Require direct Qt/Wine event-loop livelock evidence in addition to a stable
  unresponsive window before the injected hook can request recovery. A plain
  10-second UI pause is recorded as safely suppressed and never kills UU.
- Make the native tray track controlled RTC sessions from the local server's
  `stable`/`closed` transitions without logging connection contents. Recovery
  requests are rejected while a session is active or its state is unknown,
  require the v15 evidence payload, and require two idle observations before
  the controller can be restarted.
- Add regression coverage for legitimate blocked UI work, invalid recovery
  payloads, active controlled sessions, RTC close transitions, and the
  two-pass idle confirmation path.

## [1.1.11] - 2026-08-03

- Break the Wine 11/Qt 5 empty-queue livelock at its actual event-dispatcher
  boundary. The process-local hook now verifies repeated `PeekMessageW` empty
  results followed by stale `MsgWaitForMultipleObjectsEx` message wakes, then
  returns one correct timeout with a 1 ms yield so Qt can enter its normal
  blocking wait instead of pinning the UI thread and leaving a white window.
- Coalesce duplicate `WM_QT_SENDPOSTEDEVENTS` wakeups for Qt's hidden event
  dispatcher window while preserving every queued Qt event. This bounds the
  cross-thread wakeup burst that makes the Wine timing race more likely after
  a control session closes or fails.
- Add an independent posted-message heartbeat from the injected worker to the
  real Qt top-level window. After a verified 10-second UI stall, the native
  tray restarts only the Qt controller process; the controlled-host server, health
  service, Wine prefix, settings, and tray remain alive. Resume gaps are
  explicitly ignored so laptop suspend cannot trigger a false recovery. The
  controller lookup also recognizes UU's runtime `source=start.exe` identity
  inside the matching Wine prefix, without widening the kill target to other
  UU processes.
- Extend Wine regression coverage to verify all three event-loop IAT hooks,
  the guard state machine, UI heartbeat acknowledgement, timeout signaling,
  and the tray's controller-only recovery path.

## [1.1.10] - 2026-08-03

- Prevent Wine's blocking non-client right-click loop from freezing UU's Qt
  UI when X11 loses the matching button-release message. Caption and system-
  menu right clicks on the home and remote-video windows are now absorbed by
  the process-local WndProc arbiter, while client-area and dialog right clicks
  keep their normal behavior.

## [1.1.9] - 2026-08-03

- Make the native tray's “Show main window” action an active one-shot command.
  A live v12 controller hook now restores and activates the real hidden Qt home
  window on its UI thread instead of waiting for UU to issue another unreliable
  `ShowWindow` call; stale requests expire safely, and live instances bypass the
  full Wine startup path.

## [1.1.8] - 2026-08-03

- Add a first-launch graphical license flow. A normal desktop or command-line
  launch now downloads and displays NetEase's official UU Remote agreement in
  a read-only Zenity window with explicit accept and reject actions.
- Continue directly through isolated Wine, UU client, and WebView2 setup before
  opening the application when the user accepts. Rejecting or closing the
  dialog records nothing and performs no installation or launch.
- Keep non-graphical and unattended sessions non-interactive: they still
  require the explicit `--accept-eula --setup-only` command, and autostart can
  never accept a license on the user's behalf.
- Add regression coverage for graphical acceptance, refusal, full first-run
  setup and launch, official-page parsing, and the headless fallback.

## [1.1.7] - 2026-08-03

- Preserve an explicitly hidden home window while a remote-video window is
  still visible. Automatic `show`, raise, activation, and `SWP_SHOWWINDOW`
  paths are rejected until the native tray creates a one-shot explicit-show
  request or the remote window closes.
- Move Qt top-level enumeration, subclass maintenance, and late-module focus
  patching onto the controller UI message thread. The worker now only queues
  coalesced passes, preventing the cross-thread USER32 wait that could freeze
  both its diagnostics heartbeat and the GameViewer message loop.
- Scope activation-message suppression to the currently latched remote window,
  remove repeated forced `SetWindowPos`/`SetActiveWindow` feedback, and rate
  limit any remaining foreground repair. Genuine external focus changes now
  continue to reach Qt instead of leaving its window state inconsistent.
- Write focus diagnostics as one atomically replaced snapshot and extend the
  Wine regression probe to cover hidden-home persistence, explicit tray
  restore, worker liveness, bounded focus repair, and modal handoff.

## [1.1.6] - 2026-08-03

- Flush CUDA-bounce uploads on the producer D3D11 context before reporting
  resource unmap completion. This prevents a second DXVK D3D11 device from
  observing stale or zero-filled shared NV12/P010 frames.
- Make the CUDA relay honor the launcher's `UU_REMOTE_CUDA_DEVICE` variable;
  retain `UUYC_CUDA_DEVICE` as a backwards-compatible fallback.
- Add a real NVIDIA/Wine shared-NV12 regression probe covering cross-device
  visibility and both CUDA-device-selection variable names.
- Keep framework-installed outer window procedures above the controller focus
  hook. Reinstalling the hook above a Qt wrapper that already chained to it
  could create a `hook -> Qt -> hook` recursion cycle and pin the UI thread at
  100% CPU.
- Add a Wine regression probe for late Qt WndProc chaining, expose the observed
  external-chain count in hook diagnostics, and provide
  `UU_REMOTE_DISABLE_FOCUS_STABILIZER=1` as an emergency startup fallback.

## [1.1.5] - 2026-07-31

- Replace the ineffective controller `DispatchMessageW`-only path with direct
  WndProc subclassing for every real GameViewer Qt top-level window.
- Add a takeover state machine: a visible takeover dialog owns activation,
  then hands focus to the remote-video window exactly once after closing.
- Detect rapid home/video activation loops, latch the correct window, and
  absorb `SetForegroundWindow`, `SetActiveWindow`, `BringWindowToTop`,
  `SetWindowPos`, and activating `ShowWindow` calls until the loop settles.
  Genuine external focus loss and explicit user clicks remain available.
- Restore Wine's standard `WM_TAKE_FOCUS` protocol specifically for
  `gameviewer.exe`, and report real subclass, transition, storm-resolution,
  blocked-raise, and modal-handoff counters instead of treating IAT patch bits
  alone as proof that stabilization is active.

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
