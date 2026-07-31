# UU Remote for Linux 0.9.2

This release fixes the controlled-host cursor shape path.

## Root cause

`GameViewerServer.exe` receives the remote input, but UU performs its GDI
screen and cursor capture in a separately loaded `streamer.dll`. That module
has its own import entries for `GetCursorInfo`, `GetIconInfo`, and `SendInput`.
Hook versions 1 and 2 patched only the executable's import table, so native
X11 movement and clicks worked while the capture module still received Wine's
hidden or unusable cursor handle. The Windows controller then hid its local
pointer and had no remote cursor image to draw.

## Root fix

Hook version 3 waits for `streamer.dll` and patches its module-local
`GetCursorInfo` and `SendInput` imports as part of the injection handshake. It
returns the tracked native coordinates together with a stable shared arrow
cursor that Wine can successfully expose through `GetIconInfo`.

The Wine integration test now loads a separate fixture named `streamer.dll`
and verifies all of the following before passing:

- the module-local `SendInput` call reaches the XTest bridge;
- `GetCursorInfo` reports the synchronized coordinates and `CURSOR_SHOWING`;
- the returned cursor handle has an extractable mask or color bitmap.
