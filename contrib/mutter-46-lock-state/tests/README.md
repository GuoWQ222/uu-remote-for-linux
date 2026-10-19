# PJLAB keyboard modifier acceptance

These tests operate the real desktop. They inject input only after their
diagnostic window has focus. They temporarily occupy the remote keyboard;
do not type into the desktop while an automated case is running.

Copy the Python files in this directory to `~/.cache/` before running them.
The test fixture is `pjlab_caps_route_repro.py`; the workload scripts reuse
its native GTK windows, live IBus engine and Mutter RemoteDesktop session.
The UU transport uses the running installed bridge's authenticated loopback
protocol. Endpoint authentication material is never printed or saved in reports.

## Regression cases

- `PJLAB_EXPECT_ALIGNED=1 PJLAB_VERIFY_TEXT=1 python3 -u ~/.cache/pjlab_caps_route_repro.py`
  runs focused raw/IBus/Wayland paths, focus changes while holding Caps,
  GNOME Shell dialogs, consumed Caps press/release, actual `abc/ABC/abc`,
  and left Shift switching between `nihao` and a committed `你好`.
- Set `PJLAB_TEST_BACKEND=x11` for a real XWayland GTK client.
- `python3 -u ~/.cache/pjlab_pressure_campaign.py` temporarily runs both
  virtual monitors at 3840x2160, animates both, loads eight CPU workers and
  runs the four transport/client combinations. The original 2560x1440 +
  3840x2160 layout is restored in the `finally` block.
- Each pressure case checks 400 lock/Shift cycles, 80 bursts of 16 interleaved
  Caps-and-text groups, 160 filtered-event cycles, and eight monitor moves.
  Checks compare actual text with its independent expected value and also
  compare native/client locks; UU cases additionally check published locks.
  Filter modes consume both events, only press, or only release. The random
  Shift sequence is reproducible with seed 20261019.
- `python3 -u ~/.cache/pjlab_reconnect_acceptance_runner.py` restarts the
  actual input/capture bridge 12 times, six for each GTK backend, alternating
  locked/unlocked state. It checks new PIDs, dual capture, published locks,
  and real `ABC` or `abc` without resetting locks during reconnection.
- `python3 -u ~/.cache/pjlab_restart_acceptance.py` performs two real GDM
  desktop restarts, verifies a new compositor PID and repeats native/XWayland
  input checks plus live dual-screen capture checks after each restart.

Results are in `~/.cache/pjlab-pressure-evidence/`. Failures are retained.
Do not count focus/startup aborts as passing cases. Build/unit success alone
is not end-to-end acceptance. This workload verifies the installed local
backport; it does not establish correctness for a future package replacement.

## Upstream basis

- GNOME MR 4546 / 7d0d128: remove the second modifier accounting.
- Dependencies aeac0f3 and 1940c6e: expose the native modifier tuple.
- GNOME MR 4686 / 0828fd6: use each event's captured state before delivery
  and the backend result after delivery, including focus-entry refresh.
- Mutter 46 integration: notify changes even if an IME consumes both key
  events; retain the original exported 46 ABI for existing consumers.

Rollback packages and launchers are retained under
`/usr/local/share/pjlab-recovery/`. The patched package version is
`46.2-1ubuntu0.24.04.18+pjlablock3`.
