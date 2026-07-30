**English** | [简体中文](README.zh-CN.md)

# UU Remote for Linux

An unofficial Linux compatibility wrapper that runs the official Windows
NetEase UU Remote client in a dedicated Wine prefix. The current scope is the
**controller side**: use Linux to connect to Windows or macOS computers.

> [!IMPORTANT]
> This community project is not affiliated with, endorsed by, or supported by
> NetEase or NetEase UU Remote. NetEase, UU, the application icon, and related
> trademarks belong to their respective owners.

## What works

- Sign in and discover devices with the official UU Remote client.
- Remote video, audio, keyboard, mouse, clipboard, and file transfer.
- CPU/OpenH264 decoding and an experimental NVIDIA NVDEC bridge.
- Automatic decoder discovery and a graphical CPU/GPU selector.
- Native Linux tray menu with decoder selection and automatic restart.
- XDG login autostart synchronized with the upstream setting.
- Native `systemd-logind` sleep inhibition.
- Wake-on-LAN configuration and a version-locked UU capability bridge.
- Hash-gated automatic updates with rollback and compatibility-DLL restoration.
- An X11 keyboard bridge for `Super+Space` in the focused remote window.

This is not a native Linux client. Linux host/unattended mode, login-screen
control, privacy screen, virtual displays, HDR, and guaranteed 4K/144 Hz are
not currently supported.

## Requirements

- Ubuntu 24.04 or a compatible Debian-based distribution
- x86_64/amd64
- X11, or XWayland in a Wayland session
- Wine 11.1 or newer
- A normal user session with systemd

The Debian package declares the required runtime utilities. Ubuntu 24.04's
standard Wine 9.0 package is too old; install Wine 11.1+ by following the
[official WineHQ instructions](https://gitlab.winehq.org/wine/wine/-/wikis/Debian-Ubuntu).

NVIDIA hardware decoding additionally requires a working proprietary NVIDIA
driver exposing `libcuda.so.1` and `libnvcuvid.so.1`, plus Vulkan 1.3 support.
Intel and AMD GPUs are detected and shown by the selector, but are not yet
connected to UU because the project does not currently provide a VA-API bridge.

## Install

Download the latest `.deb` from
[GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest),
then run:

```bash
sudo apt install ./uuyc-linux-controller_0.8.1_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
uuyc-linux-controller --diagnose
uuyc-linux-controller
```

The package does **not** contain or redistribute NetEase application binaries.
After explicit EULA acceptance, first-time setup downloads about 90 MB from
NetEase's official release endpoint and installs the client and WebView2 in an
isolated Wine prefix. Setup can take several minutes.

You remain subject to the
[NetEase UU Remote Software License and Service Agreement](https://uuyc.163.com/contact/20240402/40294_1146065.html).

### Install from source

```bash
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uuyc-linux-controller --accept-eula --setup-only
```

The user-level installation goes under `~/.local` and does not require root.

## Usage

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

# Configure or remove the UU Wake-on-LAN compatibility patch
uuyc-linux-controller --enable-wol
uuyc-linux-controller --disable-wol

# Stop only this project's dedicated Wine prefix
uuyc-linux-controller --stop
```

The native tray menu also provides **Select decoder…**. Saving a valid choice
stops this isolated prefix, deploys or removes the decoding bridge, and
restarts UU Remote automatically.

## Decoder support

| Device/backend | Detection | UU integration | Advertised limit |
|---|---:|---:|---|
| CPU / OpenH264 | Yes | Supported | Upstream path: 1080p/60 fps |
| NVIDIA NVDEC | Per-GPU driver query | Experimental | UU menu up to 4K/144 fps; measure on the actual stream |
| Intel/AMD VA-API | PCI discovery | Not implemented | Reported as unavailable, never silently selected |

The NVIDIA bridge performs NVDEC in GPU memory, copies each decoded frame to
CPU memory, and uploads it to a DXVK D3D11 texture. It fixes functionality but
is not zero-copy. Driver APIs do not expose one universal “maximum fps” value
for every codec, bit depth, chroma format, bitrate, and concurrent-stream
combination, so the selector distinguishes driver limits from UU menu limits.

## Safe automatic updates

The upstream `Upgrade.exe` can overwrite injected compatibility DLLs. This
project replaces its active entry point with a no-op placeholder while keeping
the original updater archived, then performs updates on the Linux side.

Only versions present in `update-compatibility.tsv` with matching installer and
upstream file SHA-256 values are allowed. Before installing, the bridge
snapshots the client, registry, and settings. It then reapplies and validates
the Event Log shim, updater protection, decoder bridge, and Wake-on-LAN patch.
Any failure rolls the transaction back. Unknown releases are deferred instead
of modifying the working installation.

## Data and privacy

```text
Wine prefix:    ${XDG_DATA_HOME:-$HOME/.local/share}/uuyc-linux-controller/wineprefix
Installer cache:${XDG_CACHE_HOME:-$HOME/.cache}/uuyc-linux-controller/
Wrapper logs:   ${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/
```

Uninstalling the Debian package does not delete the Wine prefix, account state,
or cookies. Wrapper logs record commands and errors but do not intentionally
collect passwords, verification codes, or device lists. The official client
retains data according to its own behavior and policy.

Never post remote-assistance codes, device IDs, account cookies, or complete
client logs in a public issue. See [SECURITY.md](SECURITY.md).

## Build and test

```bash
make test
make deb
```

The generated package is written to `dist/` and does not contain the official
NetEase installer. Rebuilding the Windows Event Log shim additionally requires
`binutils-mingw-w64-x86-64` and `mingw-w64-x86-64-dev`:

```bash
make shim
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and
[CHANGELOG.md](CHANGELOG.md) for release history.

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
their respective owner.
