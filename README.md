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
- Native mouse, keyboard, and remote-cursor support when Linux is controlled:
  XTest on X11, or the RemoteDesktop portal on Wayland.
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
sudo apt install ./uu-remote-for-linux_1.1.0_amd64.deb
uu-remote-for-linux --accept-eula --setup-only
```

### Install from source (optional)

```bash
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
| Wayland | XWayland | XDG RemoteDesktop Portal | Portal stream authorized; end-to-end UU capture remains experimental |

On Wayland, `uu-remote-for-linux --diagnose` reports
`wayland-xwayland` and `wayland-portal` after authorization. Portal permission
is mandatory and cannot be bypassed by the application. X11 remains the stable
choice for unattended controlled-host use until the proprietary UU capture
module has been validated against native Wayland windows.

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
