**English** | [简体中文](README.zh-CN.md)

# UU Remote for Linux

### UU远程（Linux测试版）

> [!IMPORTANT]
> An unofficial implementation of NetEase UU Remote.

## Features

- Graphical user interface (GUI).
- Supports updates in sync with the Windows version of NetEase UU Remote.
- Sign in to the official UU Remote client and use remote control on Ubuntu 24.04.
- CPU/OpenH264 decoding and experimental NVIDIA NVDEC decoding.
- Native Linux tray menu with decoder selection and automatic restart.
- Stabilizes every real Qt top-level window through a process-local WndProc
  arbiter. During device takeover, the confirmation dialog exclusively owns
  activation and hands it to the remote-video window exactly once after it
  closes. Repeated programmatic raises from the home/video pair are rate
  detected and blocked, while a real user click or focus leaving UU remains
  available. The controller also opts back into Wine's standard
  `WM_TAKE_FOCUS` protocol instead of inheriting the wrapper's legacy global
  override.
- While a controller window is focused, temporarily pin the local source to a
  physical XKB keyboard and forward `Super+Space` to the remote OS. This keeps
  local IBus/Rime from consuming Chinese composition keys. Wine XIM is also
  disabled for the UU controller executable so the same shortcut cannot enable
  a local IME inside Wine. The original source and shortcuts are restored after
  focus leaves the viewer.
- Native mouse, keyboard, and remote-cursor support when Linux is controlled:
  XTest on X11, or the RemoteDesktop portal on Wayland.
- Wayland screen capture through an XDG ScreenCast/PipeWire shared-frame
  bridge, so the process-local Win64 hook feeds the real GNOME desktop to UU's
  GDI capture instead of the unreadable rootless XWayland surface.
- Compatibility bridges for autostart, sleep inhibition, safe updates, the
  Linux system proxy, file transfer, and Wake-on-LAN.

## Requirements

- Ubuntu 24.04 (tested)
- Wine 11.1 or newer
- X11, or GNOME Wayland with XWayland and XDG Desktop Portal. The launcher
  detects the current session automatically. The first Wayland controlled-host
  start asks the user to authorize screen sharing and remote interaction.

## Install from Release (recommended)

Download the latest `.deb` from
[GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest),
then run:

```bash
sudo apt install ./uu-remote-for-linux_1.1.6_amd64.deb
uu-remote-for-linux --accept-eula --setup-only
```

### Install from source (optional)

```bash
sudo apt install python3-gi gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux --accept-eula --setup-only
```

## Usage (command line)

```bash
# Start UU Remote
uu-remote-for-linux

# Install or repair the isolated client
uu-remote-for-linux --setup-only
uu-remote-for-linux --repair

# Inspect the installation without changing it
uu-remote-for-linux --diagnose

# Run a safe, compatibility-gated update check
uu-remote-for-linux --check-update

# Select a decoder interactively, or list detected devices
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders

# Select a policy explicitly
uu-remote-for-linux --decoder auto
uu-remote-for-linux --decoder cpu
uu-remote-for-linux --decoder nvidia:0

# Stop only this project's dedicated Wine prefix
uu-remote-for-linux --stop

# Emergency fallback if controller focus handling is incompatible
UU_REMOTE_DISABLE_FOCUS_STABILIZER=1 uu-remote-for-linux
```

## Decoder support

| Device/backend | UU integration | Advertised limit |
|---|---:|---|
| CPU / OpenH264 | Supported | Upstream path: 1080p/60 fps |
| NVIDIA NVDEC | Experimental | UU menu up to 4K/144 fps; measure on the actual stream |
| Intel/AMD VA-API | Not implemented | Unavailable |

## Desktop backends

| Session | Wine UI | Controlled-host input | Native desktop capture |
|---|---|---|---|
| X11 | Wine X11 driver | XTest | Supported by the existing UU path |
| Wayland | XWayland | XDG RemoteDesktop Portal | ScreenCast/PipeWire → shared frames → process-local Win64 GDI hook |

On Wayland, `uu-remote-for-linux --diagnose` reports
`wayland-xwayland`, `wayland-portal`, and an active `Wayland screen bridge`
after authorization. Portal permission is mandatory and cannot be bypassed by
the application. The bridge requests the monitor without an embedded cursor;
UU's synchronized remote cursor remains responsible for pointer rendering.
Display-manager and lock-screen capture are still outside the logged-in user's
Portal session, so X11 remains the safer choice for pre-login unattended use.

## Chinese input from the controller

The viewer transports physical keyboard events; it cannot turn a committed
Linux IBus/Fcitx candidate into remote keystrokes. Keep the viewer focused and
press `Super+Space` (Win+Space) to select the **remote device's** Chinese IME,
then type the pinyin normally. Since 1.1.3, the launcher applies Wine's
per-application `UseXIM=N` option to `gameviewer.exe`, preventing the controller
from opening a local IBus context. The keyboard bridge also pins the local source
to XKB while focused and treats Wine helper windows as part of the viewer. The
original Linux source is restored about 0.75 seconds after focus really leaves
the viewer.

## License and attribution

Original project code is available under the Zero-Clause BSD license
([LICENSE](LICENSE)). Some compatibility ideas and the Event Log shim are
adapted from `ParticleG/uuyc-wine`; bundled optional decoding components retain
their respective zlib and LGPL licenses. Exact revisions, corresponding source,
and checksums are documented in [NOTICE.md](NOTICE.md) and
[third_party/HWDECODE.md](third_party/HWDECODE.md).

The repository does not include the proprietary NetEase installer or client
binaries. The official application icon is included only for product
identification; its artwork and all NetEase/UU marks remain the property of
NetEase.
