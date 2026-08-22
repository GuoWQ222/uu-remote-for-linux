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

## ⚠️ Known Issues

Ubuntu Wayland still has several stability issues. We will accelerate fixes in future releases.

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

> [!NOTE]
> Version-specific codec, portrait-quality, and WOL changes are applied only after unique semantic fingerprint, hash, and runtime validation. For releases newer than the verified 4.38 baseline, an update that cannot rebuild every enabled adaptation is rejected and rolled back to the last complete client; older releases retain the documented safe fallbacks.

<a id="install"></a>
## 🚀 Install

**Tested environment:** Ubuntu 24.04 x86_64 · Wine 11.1+ · X11 or GNOME Wayland

1. Download the latest `.deb` from [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest).
2. Install and launch:

   ```bash
   sudo apt install ./uu-remote-for-linux_1.1.32_amd64.deb
   uu-remote-for-linux
   ```

3. On Ubuntu 24.04 GNOME Wayland, explicitly enable the bundled low-latency
   capture repair:

   ```bash
   /usr/bin/uu-remote-for-linux --mutter-fix-status
   /usr/bin/uu-remote-for-linux --install-mutter-fix
   ```

   The manager accepts only the exact Noble amd64 Mutter 46.2 baseline and
   rechecks all four packages, SHA256 digests, and the APT transaction plan.
   Success means APT has both unpacked and configured all four packages; an
   unpack-only result is an interrupted transaction that must be recovered
   before logout. Save your work and log out/in after success. Installing or
   removing UU itself never replaces, pins, or rolls back Mutter automatically.
   Before removing UU, run `--rollback-mutter-fix` if desired. Verified
   recovery packages remain under
   `/var/lib/uu-remote-for-linux/mutter-fix/rollback/` so uninstalling the
   wrapper cannot remove the rescue material.
   If GNOME Wayland cannot start, recover from a TTY or SSH session with:

   ```bash
   sudo /usr/libexec/uu-remote-for-linux/uu-remote-mutter-fix-root rollback
   ```

   The direct command below is an emergency path only when the manager reports
   `interrupted-known-partial` and every one of the four installed versions is
   one of the exact `.16`, `+0uuremote3`, or diagnostic `+uuremote3` versions.
   This also covers that known state after UU itself has been removed. If any
   package is `.17` or another unknown/newer version, do **not** run this
   downgrade; finish the Ubuntu repository upgrade instead. The paths are
   intentionally written out without a user-shell glob:

   ```bash
   sudo apt-get --allow-downgrades --reinstall --no-remove install \
     /var/lib/uu-remote-for-linux/mutter-fix/rollback/mutter-common_46.2-1ubuntu0.24.04.16_all.deb \
     /var/lib/uu-remote-for-linux/mutter-fix/rollback/mutter-common-bin_46.2-1ubuntu0.24.04.16_amd64.deb \
     /var/lib/uu-remote-for-linux/mutter-fix/rollback/libmutter-14-0_46.2-1ubuntu0.24.04.16_amd64.deb \
     /var/lib/uu-remote-for-linux/mutter-fix/rollback/gir1.2-mutter-14_46.2-1ubuntu0.24.04.16_amd64.deb
   ```

   Log out and back in after recovery. If those cached files do not exist,
   reinstall the matching UU `.deb` first; its maintainer scripts still do not
   alter Mutter, but they restore the fixed-path recovery helper and payload.

On first launch, accept NetEase's [official agreement](https://uuyc.163.com/contact/20240402/40294_1146065.html) to install the isolated client and WebView2. Rejecting it leaves the client uninstalled.

> [!TIP]
> Terminal-only setup: `uu-remote-for-linux --accept-eula --setup-only`

<details>
<summary><strong>🧱 Install from source</strong></summary>

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

A user-writable source installation cannot safely provide a privileged Mutter
manager. That feature is available only from the root-owned system `.deb`.

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
| Desktop | X11 + XTest | ✅ | Direct input and existing UU capture path |
| Desktop | GNOME Wayland Portal | ✅ | XWayland UI; Portal input and PipeWire capture; Noble latency repair is explicit |
| Decode | CPU / OpenH264 | ✅ | Safe fallback, up to 1080p/60 fps |
| Decode | NVIDIA NVDEC | 🧪 | Experimental; UU menu up to 4K/144 fps |
| Encode | NVIDIA NVENC | ✅ | H.264/HEVC, enabled only after validation |
| Intel/AMD hardware | — | ❌ | CPU fallback remains available |

> [!WARNING]
> Wayland requires Portal permission. On a multi-monitor desktop, select all displays in the sharing dialog; the permission is then restored for later sessions. Login-manager and lock-screen capture are outside the logged-in Portal session; use X11 for unattended access before login.

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
/usr/bin/uu-remote-for-linux --mutter-fix-status
/usr/bin/uu-remote-for-linux --install-mutter-fix
/usr/bin/uu-remote-for-linux --rollback-mutter-fix

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

Original project code uses the [Zero-Clause BSD license](LICENSE). Optional components retain their own licenses; revisions, corresponding source, build notes, and checksums are listed in [NOTICE.md](NOTICE.md), [third_party/HWDECODE.md](third_party/HWDECODE.md), and [packaging/mutter/README.md](packaging/mutter/README.md).
