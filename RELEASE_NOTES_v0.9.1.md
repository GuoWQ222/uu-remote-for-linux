# UU Remote for Linux 0.9.1

This maintenance release fixes cursor synchronization when the Ubuntu machine
is controlled from another computer.

## Root fix

UU's Windows server uses `SendInput` for incoming control events and reads
`GetCursorInfo` / `GetCursorPos` while building GDI capture metadata. The
0.9.0 native bridge correctly moved and clicked the X11 pointer through XTest,
but Wine's cached Win32 cursor position did not follow that native movement.
The controller could therefore hide its local pointer and draw UU's remote
cursor at an old position, making the cursor appear frozen or missing.

The version-2 input hook now:

- tracks every successfully forwarded absolute or relative mouse move;
- converts Windows normalized absolute coordinates to the capture desktop;
- returns the tracked position through both cursor-query APIs;
- keeps one authoritative XTest input path, avoiding duplicate movement;
- exposes per-event counters in `uu-remote-for-linux --diagnose`.

## Validation

The automated Wine injection test verifies that an absolute `SendInput` move
is forwarded to the native bridge and immediately becomes observable through
both `GetCursorInfo` and `GetCursorPos`.
