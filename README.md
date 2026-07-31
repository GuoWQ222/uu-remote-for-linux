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
- Native mouse, keyboard, and remote-cursor support when Linux is controlled
  in an X11 session.
- Compatibility bridges for autostart, sleep inhibition, safe updates, file
  transfer, and Wake-on-LAN.

## Requirements

- Ubuntu 24.04 (tested)
- Wine 11.1 or newer
- An X11 session when Linux is the controlled host (native Wayland input is not
  supported yet)

## Install from Release (recommended)

Download the latest `.deb` from
[GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest),
then run:

```bash
sudo apt install ./uu-remote-for-linux_1.0.0_amd64.deb
uu-remote-for-linux --accept-eula --setup-only
```

Installing 1.0.0 replaces the previous package identity. On first launch it
moves the existing Wine prefix, login, settings, cache, logs, and autostart
entry into the new `uu-remote-for-linux` paths.

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
