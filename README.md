<h1 align="center">UU Remote for Linux</h1>

<p align="center">
  Run the official UU Remote client on Ubuntu with native Linux input, display, tray, update, and hardware-codec integration.
</p>

<p align="center">
  <a href="https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest"><img src="https://img.shields.io/github/v/release/GuoWQ222/uu-remote-for-linux?style=flat-square&label=release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Ubuntu-24.04-E95420?style=flat-square&logo=ubuntu&logoColor=white" alt="Ubuntu 24.04">
  <img src="https://img.shields.io/badge/Desktop-X11%20%7C%20Wayland-4A90E2?style=flat-square" alt="X11 and Wayland">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-0BSD-blue?style=flat-square" alt="0BSD license"></a>
</p>

<p align="center"><strong>English</strong> · <a href="README.zh-CN.md">简体中文</a></p>

> [!IMPORTANT]
> This is an unofficial compatibility wrapper for NetEase UU Remote. The repository does not distribute the proprietary installer or client binaries.
> UU and wrapper-managed downloads use direct connections by default; desktop proxy settings are not copied into the Wine client.

## ✨ Highlights

| | Capability | What it provides |
|---|---|---|
| 🖥️ | Ubuntu desktop | Isolated Wine client, graphical first-run setup, and transactional official-client updates |
| 🖱️ | Native remote control | X11/XTest or Wayland Portal input, controlled-display selection, and aligned local/remote cursors |
| 🎬 | Hardware video | CPU/OpenH264 fallback, experimental NVIDIA NVDEC decoding, and validated NVENC H.264/HEVC encoding |
| 🧭 | Native tray | Show the main window, choose the controlled display or decoder, and arrange one or two viewer windows |
| 🛡️ | Linux integration | Autostart, sleep inhibition, resilient encrypted DNS, file transfer, safe updates, and Wake-on-LAN bridges |

> [!NOTE]
> After a UU update, the Wake-on-LAN layer derives a local profile from verified function boundaries and WOL semantics in the official `GameViewerServer.exe`, then deploys only after checking the original bytes and SHA-256 hashes. Ambiguous binaries are left untouched and safely fall back to Win32 adapter mapping.

## 🚀 Quick start

1. Download the latest `.deb` from [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest).
2. Install it:

   ```bash
   sudo apt install ./uu-remote-for-linux_1.1.26_amd64.deb
   ```

3. Open **UU Remote for Linux** from the Ubuntu application menu, or run:

   ```bash
   uu-remote-for-linux
   ```

On first launch, accept NetEase's official UU Remote agreement to install the isolated Windows client and WebView2 automatically. Rejecting the agreement leaves the client uninstalled.

> [!TIP]
> For terminal-only setup, read the [official agreement](https://uuyc.163.com/contact/20240402/40294_1146065.html), then run `uu-remote-for-linux --accept-eula --setup-only`.

<details>
<summary><strong>Install from source</strong></summary>

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

</details>

## ✅ Requirements

| Item | Requirement |
|---|---|
| Operating system | Ubuntu 24.04 (tested) |
| Wine | 11.1 or newer |
| Desktop | X11, or GNOME Wayland with XWayland and XDG Desktop Portal |
| NVIDIA acceleration | Proprietary driver with usable `libcuda.so.1` and `libnvidia-encode.so.1` |

The launcher detects the session and probes optional NVIDIA libraries automatically. Wayland asks for screen-sharing and remote-interaction permission the first time this machine is controlled.

## 🧭 Tray at a glance

Right-click the native UU icon in the Ubuntu top bar or system tray:

| Action | Purpose |
|---|---|
| **显示主界面 · Show main window** | Restore the hidden or minimized UU window |
| **被控屏幕 · Controlled display** | Choose the Ubuntu monitor that matches the video track selected on Windows |
| **远控窗口布局 · Viewer layout** | Keep one viewer on the primary display or place existing viewers across two displays |
| **选择解码器… · Select decoder…** | Choose CPU/NVIDIA decoding and restart UU automatically |
| **退出 · Exit** | Stop this project's dedicated Wine client and remove the tray icon |

The display menu follows hot-plug, rotation, and layout changes. The dual-view option arranges existing windows; it does not create another remote session.

## 🎛️ Compatibility

### Video

| Role | Backend | Status | Notes |
|---|---|---:|---|
| Decode | CPU / OpenH264 | ✅ Supported | Upstream path, up to 1080p/60 fps |
| Decode | NVIDIA NVDEC | 🧪 Experimental | UU menu up to 4K/144 fps; verify on the actual stream |
| Encode | NVIDIA NVENC | ✅ Auto-probed | H.264 and HEVC; preferred only after validation |
| Encode | CPU / OpenH264 | ✅ Safe fallback | H.264 |
| Decode/encode | Intel/AMD hardware | ❌ Not implemented | CPU fallback remains available |

### Desktop

| Session | UI | Controlled-host input | Capture |
|---|---|---|---|
| X11 | Wine X11 driver | XTest | Existing UU capture path |
| Wayland | XWayland | XDG RemoteDesktop Portal | ScreenCast/PipeWire shared frames → Win64 GDI hook |

> [!NOTE]
> Portal permission is mandatory on Wayland. Login-manager and lock-screen capture remain outside the logged-in Portal session, so X11 is safer for unattended control before login.

<details>
<summary><strong>Useful commands</strong></summary>

```bash
# Start, inspect, repair, or stop
uu-remote-for-linux
uu-remote-for-linux --diagnose
uu-remote-for-linux --repair
uu-remote-for-linux --stop

# Update the official client
uu-remote-for-linux --check-update

# Decoder selection
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders
uu-remote-for-linux --decoder auto   # or cpu / nvidia:0

# Controlled-host NVENC policy
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode

# Emergency focus fallback
UU_REMOTE_DISABLE_FOCUS_STABILIZER=1 uu-remote-for-linux
```

</details>

## ⚠️ Known issues

The following issues are confirmed and will be fixed in later releases:

- When Windows controls a dual-monitor Ubuntu host, switching to a portrait monitor may remain visibly blurry even at Original quality, making text difficult to read; the landscape monitor is usually unaffected.
- The Ubuntu cursor changes to an I-beam or resize cursor over text fields, window edges, and corners, but the Windows controller currently keeps the default arrow instead of receiving the remote cursor shape.

## 📄 License and attribution

Original project code uses the [Zero-Clause BSD license](LICENSE). Optional hardware-codec components retain their zlib or LGPL licenses; exact revisions, source links, and checksums are listed in [NOTICE.md](NOTICE.md) and [third_party/HWDECODE.md](third_party/HWDECODE.md).
