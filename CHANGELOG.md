# Changelog

All notable changes to this project are documented here.

## [1.1.34] - 2026-08-23

- Add separate `uu-remote-for-linux-x11` and
  `uu-remote-for-linux-wayland` APT entry packages. The X11 package installs
  only the shared UU runtime, while the Noble amd64 Wayland package declares
  the four verified Mutter packages as minimum-version ordinary dependencies
  so APT resolves, unpacks, and configures the complete set in one transaction
  while allowing later Ubuntu security updates to supersede the repair.
- Add a dedicated archive-keyring package, deterministic Debian repository
  metadata generation, signed `InRelease`/`Release.gpg` output, exact binary
  and source indexes, and isolated signature and package-relationship tests.
  No maintainer script invokes APT or silently changes Mutter.

## [1.1.33] - 2026-08-23

- Add a version- and SHA256-bound outer installer that verifies the Ubuntu
  Noble amd64 host and the UU package, completes the main APT transaction
  first, and invokes the existing fail-closed Mutter manager only for GNOME
  Wayland. The same one-command entry point installs UU alone on X11 without
  nesting APT inside a Debian maintainer script.
- Reject package hash or metadata mismatches, unhealthy dpkg/APT state,
  unsupported Wayland desktops, and main-package plans containing removals
  before requesting administrator privileges. Report the exact post-install
  Mutter state and require logout only when the running GNOME Shell still maps
  the previous library.

## [1.1.32] - 2026-08-22

- Mirror GNOME Wayland's XKB Caps Lock, Num Lock, and Scroll Lock state into
  Wine so UU's `GetKeyState` checks match the keys forwarded through the
  RemoteDesktop Portal. Caps Lock now changes state with one press instead of
  requiring a second request to compensate for stale Wine state.
- Cache the XKB query for high-rate pointer traffic, invalidate it immediately
  after a lock-key event, and fail closed when XKB state is unavailable. Add
  Wayland and Wine regressions that require exactly one down/up pair per lock
  key and verify immediate Caps state toggle and restore.

## [1.1.31] - 2026-08-22

- Let APT complete the configuration phase for the four explicitly verified
  Mutter packages. Version 1.1.30 incorrectly disabled pending configuration,
  so a real transaction could unpack all four packages successfully and then
  fail the installed-state check; the recovery transaction had the same flaw.
- Add a transaction regression that distinguishes unpacked packages from fully
  configured packages, while retaining the locked pre-invoke health check and
  exact four-package pre-install plan validation.

## [1.1.30] - 2026-08-22

- Reject zero-length `CORRUPTED` PipeWire cursor-only buffers before any video
  transform and publish each accepted capture sample atomically, eliminating
  the recycled transform-pool frames that made Wayland video and IME preedit
  text alternate between old and new states.
- Process every accepted damage-driven sample instead of discarding a unique
  final state at a fixed-rate deadline, and negotiate the native four-byte
  BGRA format without an unnecessary full-frame color conversion.
- Bound Mutter 46.2's detached MemFd screen-cast callback at default GLib
  priority and retry full-frame PipeWire overruns, removing the compositor-side
  0.1–1.3 second IME presentation stalls found on Ubuntu 24.04 GNOME Wayland.
- Ship the exact Noble amd64 Mutter repair and official offline recovery set
  inside the system package, with an explicit PolicyKit-gated manager that
  verifies the OS, architecture, package set, hashes, solver plan, and rollback
  data before changing any desktop package. Installing or removing UU itself
  never installs, pins, or rolls back Mutter.
- Increase the shared PipeWire cursor consumer request from two to three video
  buffers and recover a failed cursor sidecar inside the existing Portal
  session, avoiding full bridge restarts and two-slot starvation windows.
- Add frame-helper, corrupted-buffer, capture-priority, Mutter bundle, package
  transaction, and real Wayland IME timing regressions.

## [1.1.29] - 2026-08-21

- Request and consume every Wayland Portal monitor stream, composite them in
  Wine's primary-relative virtual-desktop coordinates, and route absolute
  pointer events to the matching PipeWire stream. This restores secondary
  portrait-screen capture that was lost when the Wayland shared-frame bridge
  previously kept only `streams[0]`.
- Default remote viewer placement to manual control and make the primary/dual
  monitor reflow policies opt-in, preventing GNOME Wayland/XWayland from
  snapping a user-moved or maximized viewer back to the portrait primary
  monitor during the tray proxy's periodic scan.
- Add a native PipeWire cursor-metadata sidecar for Wayland, publish the real
  GNOME cursor image and hotspot to the Wine capture hook, suppress UU's fixed
  duplicate cursor, and keep the controller pointer releasable outside the
  viewer while preserving text, resize, and application-specific shapes.
- Extend the shared-frame protocol with the virtual-desktop origin and correct
  the top-left Portal to bottom-up DIB crop conversion, eliminating the large
  black region and vertically shifted partial image on mixed-orientation
  monitor layouts.
- Supervise the Wayland Portal bridge and fail fast when any PipeWire stream or
  cursor helper stalls, so systemd recreates the capture session instead of
  leaving a frozen frame or permanent black screen after reconnecting.
- Keep dynamically reconstructed native cursor handles stable beyond the
  fixed cache capacity and expose cursor/capture sequence telemetry for live
  verification without allowing handle reuse to corrupt later cursor shapes.
- Move potentially blocking Wine hook injection and streamer initialization to
  independently polled workers, preventing one hung remote thread from
  freezing monitoring or delaying reinjection after a module reload.
- Make NVDEC and NVENC deployment exact-file transactions, restoring both
  pre-existing and previously absent files after a partial install or failed
  deep probe instead of leaving a mixed bridge state.
- Harden PE profile parsing against truncated headers, invalid section counts,
  duplicate names, and virtual-only ranges before calculating patch offsets.
- Separate NVENC registration ownership from per-frame D3D11 texture upload,
  serialize lifecycle transitions, validate resource identity and dimensions,
  and propagate the selected adapter LUID into the client service to prevent
  stale frames, cross-thread teardown races, and adapter mismatches.
- Make controller recovery resistant to PID reuse and `/proc` names with
  non-UTF-8 bytes, and isolate integration-test configuration state from the
  user's real desktop settings.

## [1.1.28] - 2026-08-20

- Serialize lazy CUDA symbol initialization and publish the completed API table
  atomically, so concurrent encoder creation cannot observe partially resolved
  driver entry points.
- Balance every CUDA primary-context push with a matching pop across normal and
  failure paths, preventing context-stack growth during repeated NVENC resource
  registration and teardown.
- Keep remote-injection argument memory alive when the injected thread exceeds
  its wait deadline, eliminating a use-after-free while preserving bounded
  launcher waits.
- Suspend and inspect peer threads before applying the 12-byte streamer
  hotpatch, reject any thread whose instruction pointer overlaps the patch
  window, serialize concurrent patch attempts, and track streamer module
  generations so the injector retries initialization after each load or reload.
- Stage UU update rollback data transactionally, restore both client and bridge
  state after partial failures, and reject corrupt rollback archives before they
  can replace a verified installation.
- Validate every encoder-policy JSON field by type and value, and rate-limit
  watcher retries by input signature so malformed or partially written state
  cannot trigger a CPU-intensive retry loop.
- Defer Wine compatibility initialization until execution is outside
  `DllMain`'s loader lock while retaining the earliest safe preload marker.
- Retain cached native cursor handles for the process lifetime so callers never
  receive an `HCURSOR` that another cache rotation has already destroyed.

## [1.1.27] - 2026-08-20

- Add a hash-bound semantic compatibility profile for the NVENC encoder path
  and all four portrait Original-quality scaling stages. After an official UU
  update, the launcher now derives and fully re-probes that profile instead of
  trusting offsets from the previous client binary.
- Treat post-4.38 client updates as one fail-closed compatibility transaction:
  regenerate and runtime-validate the enabled NVENC, NVDEC, portrait, and WOL
  adaptations, persist their generated profiles, and restore the previous UU
  client plus bridge state if any required semantic target cannot be proven.
- Require a real NV12 D3D11 registration, mapping, and upload in the NVENC
  deep probe, and serialize native NVENC/CUDA calls per encoder session so
  concurrent UU worker threads cannot race resource registration or teardown.
- Preserve the selected portrait monitor's native capture resolution instead
  of applying UU's landscape-oriented 2160-pixel height ceiling. The
  fail-closed runtime profile now repairs all four verified scaling stages,
  keeping a 1440x2560 source at 1440x2560 in Original quality.
- Synchronize complete X11 cursor images and hotspots to the Windows
  controller through an XFixes ARGB bridge and native Win64 `HCURSOR`
  reconstruction. Text I-beams, diagonal resize arrows, and application
  cursors no longer collapse to UU's hard-coded default arrow.
- Add independent portrait-scaling and cursor-shape telemetry plus dynamic
  Wine regression tests for cursor dimensions, pixels, and hotspot changes.
- Restore NVIDIA NVDEC capability negotiation for official UU 4.38.0.9292.
  Replace the compiler-byte-blob profiler with a fail-closed PE semantic
  profiler that locates the unique batch-probe function from its exception
  boundary and seven detector data-flow markers, then binds both patch sites
  through the same stack variable. This remains resilient to function moves,
  frame-layout changes, and register-allocation changes after UU updates. The
  update transaction now has an end-to-end regression proving that a future
  unlisted version is profiled, validated with UU's own H.264/H.265 detector,
  persisted locally, and returned to the selected NVIDIA decoder automatically.
- Derive fail-closed Wake-on-LAN patch profiles automatically from the
  official `GameViewerServer.exe` after UU updates. The profiler requires a
  unique PE exception-function boundary that references the WOL source path
  and both `Enabled`/`Disabled` capability states, then records the original
  and patched SHA-256 hashes, entry offset, and before/after bytes locally.
- Restore the verified original server before an update, regenerate and
  deploy the profile afterward, and include generated WOL profiles in the
  transactional rollback snapshot. Ambiguous future binaries remain
  untouched and continue through the Win32 adapter-mapping fallback.

## [1.1.26] - 2026-08-19

- Keep the real Qt client alive in a transient user service with
  `ExitType=cgroup`, so systemd does not tear it down when UU's short-lived
  outer launcher exits and an application-menu click reliably opens UU.
- Add a persistent native-tray controlled-display selector with stable monitor
  identities, primary-display fallback, and automatic hot-plug, rotation, and
  layout refresh.
- Map absolute controlled-host input into the selected X11 monitor while
  preserving the distinct virtual-desktop coordinate space, and return cursor
  feedback in the same screen-local geometry used by the Windows viewer.
- Mirror the real XKB Caps Lock, Num Lock, and Scroll Lock state through UU's
  Win32 `GetKeyState` path while continuing to inject the key through XTest.
  This makes each requested lock-state transition happen exactly once and
  restores one-press switching between uppercase input and Rime.
- Extend the fake-X11, Wine hook, tray, Wayland, and launcher regressions to
  cover mixed-orientation monitors, both absolute coordinate domains, the UU
  desired-lock-state algorithm, and the persistent systemd client lifecycle.

## [1.1.25] - 2026-08-19

- Add a persistent native-tray remote-window layout selector. Single-window
  mode keeps the real viewer on the primary monitor work area, while
  dual-window mode distributes two real viewer windows across two monitors.
- Read monitor work areas through GDK and pulse only prefix-owned remote-view
  windows so mixed portrait/landscape setups cannot leave Qt rendering a
  1440-pixel canvas inside a 2494-pixel primary-screen window.
- Distinguish the UTF-8 UU home-window title from real viewer windows and retry
  the finite reflow during viewer initialization without creating a periodic
  resize loop.
- Preserve and surface the exact cause of official update-check failures,
  including DNS, proxy, connection, HTTP, TLS, timeout, reset, empty-response,
  redirect, CA configuration, and unrecognized-version errors.

## [1.1.24] - 2026-08-11

- Correct the encoder-capability implementation enum: `0` is NVENC V8 and
  `1` is NVENC V11, while the previously reused NVDEC batch ID `33` falls
  through the streamer's implementation switch to `SoftWare`. Migrate only
  deeply verified H.264/HEVC rows to NVENC V8 and prove the enum mapping from
  the official binary before removing OpenH264.
- Fix the final controlled-host NVENC fallback at the value actually consumed
  by `VideoEncoderFactory`: the queued-frame accessor that locks the frame
  store and calls concrete-frame virtual slot `+0x30`.
- Leave all four long getter-sharing vtables untouched. Live call counters and
  encoder-log disassembly proved that the constructor/`+0x44` candidate is not
  on the encoding path, while patching the other three corrupts WebRTC media
  negotiation and causes a reconnect loop.
- Fail closed after an official client update unless both the offline policy
  probe and runtime hook find exactly one encoder-path accessor fingerprint;
  expose its independent call count for real-session verification.

- Derive a fail-closed NVDEC `streamer.dll` patch profile automatically after
  an official UU client update when both semantic instruction fingerprints
  remain unique and retain their verified relationship.
- Enumerate the real DXGI adapter inside the Wine/DXVK environment and validate
  each generated profile synchronously with UU's own
  `StreamerCodecDetector.exe` before the updated client may start.
- Restore the official decoder files, discard the generated profile, and roll
  back the update transaction if H.264 and H.265 NVDEC are not both confirmed,
  while preserving CPU fallback for an unrecognized future binary layout.

## [1.1.23] - 2026-08-11

- Restore NVIDIA NVDEC capability negotiation for official UU 4.36.0.9155,
  allowing the controller to request the remote host's 20M and higher quality
  tiers instead of being capped at the OpenH264 software-decoder profile.
- Move the `streamer.dll` compatibility patch into an independent per-version
  profile with original and patched SHA-256 values, instruction offsets, and
  before/after byte fingerprints, so unsupported future clients fail closed.
- Resolve decoder profiles by the installed version during deployment and by
  verified file hashes during cleanup or transactional rollback.

## [1.1.22] - 2026-08-11

- Run only `GameViewerServer.exe` with Wine's Windows 7 application profile,
  preventing UU 4.36+ from repeatedly installing Windows IDD, gvInput, and
  ViGEm kernel drivers into Wine's emulated device database.
- Restore reliable server IPC initialization, automatic login, and creation of
  the real Qt home window so tray activation no longer targets a transparent
  placeholder after the Windows-driver startup path fails.
- Verify the server-only compatibility profile during setup, repair, and
  diagnostics while leaving the Qt/WebView2 controller on Wine's normal
  Windows version.
- After an authorized tray restore, let the native Linux tray locate only the
  mapped, prefix-owned `gameviewer.exe` home window and issue a one-pixel X11
  size pulse. This forces a real `ConfigureNotify` instead of relying on Wine
  to forward a Win32 repaint that can be discarded during remapping.
- Apply the same one-shot pulse when the hook first adopts an already-visible
  home window, covering the equivalent stale surface on initial UU 4.36
  startup without introducing a periodic resize loop.
- Delay the tray pulse for 2.5 seconds so UU's deferred Qt/WebView2 remap has
  finished, retry briefly until `WM_STATE` confirms the home window is really
  mapped, and keep the one-pixel geometry for 80 ms before restoring it. Both
  X11 changes are synchronized separately so neither UU nor the window manager
  can overwrite or coalesce the intermediate configure event.

## [1.1.21] - 2026-08-11

- Check the official UU release on every application launch and periodically
  while running, independently of UU's in-app automatic-update switch.
- Install the latest official client even before a version-specific binary
  profile exists; keep the generic Wine, UI, tray, and input bridges active
  while safely suspending NVDEC and WOL patches that require exact hashes.
- Preserve the selected NVIDIA device for automatic reactivation by a future
  matching profile, and report the temporary CPU fallback in diagnostics.
- Snapshot and restore version-bound bridge state together with the Wine
  prefix, so a failed latest-version transaction still performs a complete
  rollback.

## [1.1.20] - 2026-08-07

- Mark the native tray's transient exit helper explicitly and prevent the
  helper from stopping its own systemd unit before it reaches `wineserver -k`.
- Complete tray-requested shutdown as one transaction instead of leaving the
  Qt controller alive in a non-interactive, half-finished exit state.
- Add tray and launcher regressions proving the exit helper survives long
  enough to stop Wine and never includes its own unit in the stop request.

## [1.1.19] - 2026-08-06

- Detect an application-level Qt/Wine UI hard stall from two consecutive,
  generation-bound heartbeat timeouts even when no sticky `WM_NULL` or event
  loop guard evidence is present.
- Validate the matching hook status before recovering, defer recovery while a
  controlled RTC session is active, and restart only the controller after two
  idle confirmations so the background host stays online.
- Refuse tray and command-line show requests while the controller is proven
  hung, and cancel stale recovery state when the target window disappears, a
  delayed heartbeat is finally dispatched, or the machine resumes from sleep.
- Clear coalesced window-arbitration state when its target is destroyed so a
  later home-window activation cannot inherit a permanently pending request.

## [1.1.18] - 2026-08-05

- Remove the former upstream repository name and attribution text from the
  English and Chinese documentation and packaged third-party notice.

## [1.1.17] - 2026-08-05

- Remove the former repository identity from the launcher, user installer,
  Debian relationships, issue template, tests, and runtime migration paths so
  this repository is packaged only as `uu-remote-for-linux`.
- Rename every project-owned identifier in the modified NVIDIA CUDA relay,
  remove the obsolete CUDA environment-variable alias, and ship a rebuilt
  v0.8 source archive and binary with only `UU_REMOTE_*` names.

## [1.1.16] - 2026-08-04

- Treat a live update bridge without versioned migration state as an unknown
  older instance, stop it, and replace it with the packaged bridge instead of
  letting a missing status file abort the launcher under `set -e -o pipefail`.
- Add an upgrade regression that proves the stale bridge is replaced and the
  Qt controller still starts when no update-bridge status file exists.

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
- Make the CUDA relay honor the launcher's `UU_REMOTE_CUDA_DEVICE` variable.
- Add a real NVIDIA/Wine shared-NV12 regression probe covering cross-device
  visibility and CUDA-device selection.
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
