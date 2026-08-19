**English** | [简体中文](README.zh-CN.md)

# UU Remote for Linux

### UU远程（Linux测试版）

> [!IMPORTANT]
> An unofficial implementation of NetEase UU Remote.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Install from Release (recommended)](#install-from-release-recommended)
  - [Install from source (optional)](#install-from-source-optional)
- [Usage (command line)](#usage-command-line)
- [Decoder support](#decoder-support)
- [Controlled-host encoder support](#controlled-host-encoder-support)
- [Desktop backends](#desktop-backends)
- [License and attribution](#license-and-attribution)

## Features

- Graphical user interface (GUI), with support for Ubuntu 24.04 on X11 and Wayland.
- Checks for an official UU update on every launch and periodically while
  running, then installs the latest version transactionally.
- Sign in to the official UU Remote client and use remote control on Ubuntu 24.04.
- CPU/OpenH264 decoding, experimental NVIDIA NVDEC decoding, and NVIDIA
  NVENC H.264/HEVC encoding when Linux is the controlled host.
- Native Linux tray menu with decoder selection, automatic restart, and
  single-primary or dual-monitor remote-window layouts.
- Native mouse, keyboard, and remote-cursor support when Linux is controlled:
  XTest on X11, or the RemoteDesktop portal on Wayland.
- Compatibility bridges for autostart, sleep inhibition, safe updates, the
  Linux system proxy, file transfer, and Wake-on-LAN.

## Requirements

- Ubuntu 24.04 (tested)
- Wine 11.1 or newer
- NVENC/NVDEC requires the proprietary NVIDIA driver with usable
  `libcuda.so.1` and `libnvidia-encode.so.1`; the launcher probes and validates
  both automatically.
- X11, or GNOME Wayland with XWayland and XDG Desktop Portal. The launcher
  detects the current session automatically. The first Wayland controlled-host
  start asks the user to authorize screen sharing and remote interaction.

## Install from Release (recommended)

Download the latest `.deb` from
[GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest),
then run:

```bash
sudo apt install ./uu-remote-for-linux_1.1.25_amd64.deb
```

Then launch **UU Remote for Linux** from the Ubuntu application menu, or run
`uu-remote-for-linux`. On first launch, a window displays NetEase's official
UU Remote license agreement. Accepting it automatically installs the isolated
Windows client and WebView2 runtime, then opens UU Remote; rejecting it leaves
the client uninstalled and exits.

For a terminal-only session, read the [official agreement](https://uuyc.163.com/contact/20240402/40294_1146065.html)
and explicitly run `uu-remote-for-linux --accept-eula --setup-only`.

### Install from source (optional)

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
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

# Check now and install the latest official UU version
uu-remote-for-linux --check-update

# Select a decoder interactively, or list detected devices
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders

# Select a policy explicitly
uu-remote-for-linux --decoder auto
uu-remote-for-linux --decoder cpu
uu-remote-for-linux --decoder nvidia:0

# Enable or disable the automatic NVENC controlled-host policy
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode

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

## Controlled-host encoder support

| Device/backend | UU integration | Codecs |
|---|---:|---|
| NVIDIA NVENC | Automatically probed and preferred | H.264, HEVC |
| CPU / OpenH264 | Safe fallback when NVENC validation fails | H.264 |
| Intel/AMD hardware encoding | Not implemented | Unavailable |

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

## License and attribution

Original project code is available under the Zero-Clause BSD license
([LICENSE](LICENSE)). Bundled optional hardware codec components retain their
respective zlib and LGPL licenses. Exact revisions, corresponding source, and
checksums are documented in [NOTICE.md](NOTICE.md) and
[third_party/HWDECODE.md](third_party/HWDECODE.md).

The repository does not include the proprietary NetEase installer or client
binaries. The official application icon is included only for product
identification; its artwork and all NetEase/UU marks remain the property of
NetEase.
