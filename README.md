**English** | [简体中文](README.zh-CN.md)

# UU Remote for Linux

### UU远程（Linux测试版）

> [!IMPORTANT]
> An unofficial implementation of NetEase UU Remote.

## Features

- Graphical user interface (GUI).
- Sign in to the official UU Remote client and use remote control on Ubuntu 24.04.
- CPU/OpenH264 decoding and experimental NVIDIA NVDEC decoding.
- Native Linux tray menu with decoder selection and automatic restart.
- Basic support for all features of the Windows UU Remote client.

## Requirements

- Ubuntu 24.04 (tested)
- Wine 11.1 or newer

## Install from Release (recommended)

Download the latest `.deb` from
[GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest),
then run:

```bash
sudo apt install ./uuyc-linux-controller_0.8.1_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
```

### Install from source (optional)

```bash
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uuyc-linux-controller --accept-eula --setup-only
```

## Usage (command line)

```bash
# Start UU Remote
uuyc-linux-controller

# Install or repair the isolated client
uuyc-linux-controller --setup-only
uuyc-linux-controller --repair

# Inspect the installation without changing it
uuyc-linux-controller --diagnose

# Run a safe, compatibility-gated update check
uuyc-linux-controller --check-update

# Select a decoder interactively, or list detected devices
uuyc-linux-controller --select-decoder
uuyc-linux-controller --list-decoders

# Select a policy explicitly
uuyc-linux-controller --decoder auto
uuyc-linux-controller --decoder cpu
uuyc-linux-controller --decoder nvidia:0

# Stop only this project's dedicated Wine prefix
uuyc-linux-controller --stop
```

## Decoder support

| Device/backend | Detection | UU integration | Advertised limit |
|---|---:|---:|---|
| CPU / OpenH264 | Yes | Supported | Upstream path: 1080p/60 fps |
| NVIDIA NVDEC | Per-GPU driver query | Experimental | UU menu up to 4K/144 fps; measure on the actual stream |
| Intel/AMD VA-API | PCI discovery | Not implemented | Reported as unavailable, never silently selected |

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
