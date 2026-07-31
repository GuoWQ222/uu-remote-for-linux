# UU Remote for Linux 0.9.0

This release adds the first Linux controlled-host input path for X11.

## Root fix

The official Windows server first tries its proprietary `gvinput` kernel HID
driver. Wine cannot expose that Windows kernel driver as a Linux input device,
so the server falls back to `USER32.SendInput`; those injected messages remain
inside Wine and do not become native Linux desktop input.

0.9.0 adds two original project components:

- a Win64 import-table hook loaded only into `GameViewerServer.exe`;
- a same-architecture Win64 injection watchdog that handles server restarts;
- a native, same-user X11/XTest daemon.

The hook reroutes mouse and keyboard `SendInput` records through a versioned
loopback protocol. Each daemon start selects a random port and 128-bit token,
stored in a mode-0600 endpoint file inside the isolated Wine prefix. The daemon
releases stuck keys and mouse buttons after an input timeout and on shutdown.

## Scope

- Supported: Ubuntu 24.04 X11 desktop, mouse, wheel, keyboard, common extended
  keys, and remote cursor visibility correction.
- Not supported: native Wayland sessions, display managers/lock screens,
  gamepads, multitouch, or privileged secure desktops.
