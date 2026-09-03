<h1 align="center">UU Remote for Linux</h1>

<p align="center">
  Run the official UU Remote client on Ubuntu with native Linux input, display, tray, updates, and hardware video integration.
</p>

<p align="center">
  <a href="https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest"><img src="https://img.shields.io/github/v/release/GuoWQ222/uu-remote-for-linux?style=flat-square&label=release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Ubuntu-24.04-E95420?style=flat-square&logo=ubuntu&logoColor=white" alt="Ubuntu 24.04">
  <img src="https://img.shields.io/badge/Desktop-X11%20%7C%20Wayland-4A90E2?style=flat-square" alt="X11 and Wayland">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-0BSD-blue?style=flat-square" alt="0BSD license"></a>
</p>

<p align="center"><strong>English</strong> · <a href="README.zh-CN.md">简体中文</a></p>

> [!IMPORTANT]
> This is an unofficial compatibility wrapper. It does not distribute NetEase's proprietary installer or client binaries.

## 🔜 Coming Soon

Remote control of the login screen after logout is coming soon.

## 🗺️ Contents

- [✨ Highlights](#highlights)
- [🚀 Install](#install)
- [🧭 Daily use](#daily-use)
- [🎛️ Compatibility](#compatibility)
- [🧰 Commands](#commands)
- [📄 License](#license)

<a id="highlights"></a>
## ✨ Highlights

| | Capability | What you get |
|---|---|---|
| 🖥️ | Official client | Wine-based environment, graphical interface, and automatic updates |
| 🖱️ | Native control | Supports X11/Wayland, dual monitors, and primary-display switching |
| 🎬 | Hardware video | Supports CPU/OpenH264, NVIDIA NVDEC, and NVENC H.264/HEVC |
| 🧭 | Linux desktop | Native tray and all UU Remote settings |

<a id="install"></a>
## 🚀 Install

[Download the latest Debian package](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest), then install it on X11 or GNOME Wayland:

```bash
sudo apt install ./uu-remote-for-linux_1.1.42_amd64.deb
uu-remote-for-linux
```

No repository or archive key is required. On first launch, accept NetEase's [official agreement](https://uuyc.163.com/contact/20240402/40294_1146065.html) to install the client and WebView2.

> [!TIP]
> Terminal-only setup: `uu-remote-for-linux --accept-eula --setup-only`

<details>
<summary><strong>🧱 Install from source</strong></summary>

```bash
sudo apt install ca-certificates curl osslsigncode python3-gi zenity libpipewire-0.3-0 libegl1 libgles2
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

</details>

<a id="daily-use"></a>
## 🧭 Daily use

Right-click the native tray icon:

| Action | Purpose |
|---|---|
| **Show main window** | Restore the hidden or minimized UU window |
| **Controlled display** | Select the Ubuntu monitor matching the Windows video track |
| **Viewer layout** | Leave windows under manual control (default), keep one on the primary display, or arrange two existing viewers |
| **Select decoder** | Choose CPU/NVIDIA decoding and restart UU |
| **Exit** | Stop the dedicated Wine client |

Controlled-display choices follow hot-plug, rotation, and layout changes automatically. Viewer windows move automatically only after selecting a managed layout.

<a id="compatibility"></a>
## 🎛️ Compatibility

| Area | Backend | Status | Notes |
|---|---|---:|---|
| System | Ubuntu 24.04 | ✅ | Supported distribution |
| CPU | amd64 / x86_64 | ✅ | Architecture of the published Debian package |
| Runtime | Wine 11.1+ | ✅ | Required Wine version |
| Desktop | X11 + XTest | ✅ | Direct input and existing UU capture path |
| Desktop | GNOME Wayland Portal | ✅ | XWayland UI and Portal input; native three-buffer PipeWire DMA-BUF capture, with explicit failure instead of a hidden transport fallback |
| Decode | CPU / OpenH264 | ✅ | Safe fallback, up to 1080p/60 fps |
| Decode | NVIDIA NVDEC | 🧪 | Experimental; UU menu up to 4K/144 fps |
| Encode | NVIDIA NVENC | ✅ | H.264/HEVC, enabled only after validation |
| Intel/AMD hardware | — | ❌ | CPU fallback remains available |

> [!WARNING]
> Wayland requires Portal permission. On a multi-monitor desktop, select all displays in the sharing dialog; the permission is then restored for later sessions. Login-manager and lock-screen capture are outside the logged-in Portal session; use X11 for unattended access before login.

The exact version boundaries, semantic profilers, and update rollback
transaction are documented in
[COMPATIBILITY.md](COMPATIBILITY.md).

<a id="commands"></a>
## 🧰 Commands

<details>
<summary><strong>Show common commands</strong></summary>

```bash
# Start and maintenance
uu-remote-for-linux
uu-remote-for-linux --diagnose
uu-remote-for-linux --repair
uu-remote-for-linux --stop
uu-remote-for-linux --check-update

# Decoder
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders
uu-remote-for-linux --decoder auto   # cpu / nvidia:0

# Controlled-host encoder
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode
```

</details>

<a id="license"></a>
## 📄 License

Original project code uses the [Zero-Clause BSD license](LICENSE). Optional components retain their own licenses; revisions, corresponding source, build notes, and checksums are listed in [NOTICE.md](NOTICE.md) and [third_party/HWDECODE.md](third_party/HWDECODE.md).
