# X11 display synchronization companion

This directory preserves the opt-in compatibility component tested on PJLAB
with Ubuntu 24.04, GNOME X11, Wine 11.19 Staging, NVIDIA 580.173.02 and UU
4.43.1.3614. It is source-only and is not enabled by the Debian package.
The historical `pjlab-uu-` filenames are retained so existing installations
can be compared with these sources without changing their startup paths.
No host-specific virtual-monitor configuration is included.

## Problem and behavior

When Wine's default desktop owner was no longer alive, native resolution
changes could leave Wine's monitor metrics and UU's GDI capture buffers at
the previous size. Native X11 screenshots were stable while GDI alternated
between stale desktop frames. A single cache refresh restored the current
picture but did not prevent recurrence after another resolution change.

The companion reads the live X11 root size and active RandR monitor layout,
waits for a stable layout, and refreshes the matching UU Wine prefix. It
also detects wineserver restarts. Changes in width, height, monitor position
and primary status are handled from the reported geometry, without a
resolution whitelist. It does not submit native display modes or retry
GNOME's Apply operation. Supported modes still depend on the display driver
and desktop environment.

The short-lived helper uses Wine's internal display-change notification and
broadcasts `WM_DISPLAYCHANGE` so applications can recreate capture buffers.
The internal ABI is explicitly pinned to Wine **11.19** and rejects other
versions with exit code 78. Do not change the version macro to bypass that
guard: a Wine upgrade requires checking the corresponding `win32u` ABI and
retesting first. This component supports the standard project prefix at
`~/.local/share/uu-remote-for-linux/wineprefix` and `/usr/bin/wine` on X11.

## NVIDIA query workaround

The forced Wine cache refresh also queries Vulkan output mappings. On the
tested NVIDIA/virtual-output combination, an independent native reproducer
showed that `vkGetRandROutputDisplayEXT` returned `VK_ERROR_UNKNOWN` and made
the next native low-to-high resolution change fail. Enumerating the GPU
without that output query did not reproduce the failure.

Only the notification helper receives `VK_DRIVER_FILES=/dev/null` and
`VK_ICD_FILENAMES=/dev/null`. The helper does not render with Vulkan; it can
still refresh X11 display data. These variables are not exported to the
running UU UI/Server, other applications or the user's session. This does
not globally disable Vulkan, CUDA, NVENC or NVDEC. It is a workaround for the
tested query path, not a claim about NVIDIA's closed-source internal cause
or every NVIDIA driver version.

## Build and enable

Use an Ubuntu X11 graphical session with Wine 11.19 and the project's normal
Wine prefix. Requirements are Python 3, libX11, libXrandr, a systemd user
session and `x86_64-w64-mingw32-gcc` (Ubuntu package
`gcc-mingw-w64-x86-64`). From this directory:

```bash
mkdir -p build
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra \
  '-DPJLAB_WINE_VERSION="11.19"' -Wl,--no-insert-timestamp \
  pjlab-uu-display-refresh.c -o build/pjlab-uu-display-refresh.exe -luser32

sudo install -Dm0755 build/pjlab-uu-display-refresh.exe \
  /usr/local/libexec/pjlab-uu-display-refresh.exe
sudo install -Dm0755 pjlab-uu-display-sync.py \
  /usr/local/libexec/pjlab-uu-display-sync.py
sudo install -Dm0755 pjlab-uu-display-sync-start \
  /usr/local/libexec/pjlab-uu-display-sync-start
sudo install -Dm0755 pjlab-uu-display-sync-disable \
  /usr/local/libexec/pjlab-uu-display-sync-disable
install -Dm0644 pjlab-uu-display-sync.desktop \
  "$HOME/.config/autostart/pjlab-uu-display-sync.desktop"

/usr/local/libexec/pjlab-uu-display-sync-start
```

The executable contains only this project's helper code. Do not commit a
Wine prefix, proprietary UU binaries, account data or complete client logs.

The watcher does not start Wine when no matching wineserver is running. It
uses a per-user lock, bounded helper timeouts and bounded retries. Status
and logs are in `~/.local/state/uu-remote-for-linux/display-sync-status.json`
and `display-sync.log`. The user service is tied to the graphical session.

For a read-only size check while UU is running:

```bash
WINEPREFIX="$HOME/.local/share/uu-remote-for-linux/wineprefix" \
  WINEDEBUG=-all VK_DRIVER_FILES=/dev/null VK_ICD_FILENAMES=/dev/null \
  wine /usr/local/libexec/pjlab-uu-display-refresh.exe --query
```

Stop the watcher and remove its autostart entry without stopping UU:

```bash
/usr/local/libexec/pjlab-uu-display-sync-disable
```

## Recorded validation

- Native Vulkan query A/B checks reproduced first-apply failure 4/4 with
  the problematic query and success 4/4 when it was skipped.
- After restricting driver discovery to the helper child, 29 single native
  resolution submissions passed across the tested low/high transitions.
  The user confirmed a single Apply from 800×600 to 1920×1080 worked and
  the UU picture remained stable.
- The user subsequently tested dual 4K displays, a portrait second display,
  and the final single 4K display successfully.
- Final single-screen checks validated 33 GNOME-selectable resolutions and
  nine actual transitions spanning 800×600 through 3840×2160. Each used one
  native submission and independently checked Wine's resulting size.
- Final 3840×2160 GDI capture returned 12 identical static frames. The
  synchronization service also started automatically after graphical
  logout/login during the display setup.

These checks cover the reported stale-geometry and first-apply failures on
the stated environment. They do not establish compatibility with arbitrary
Wine ABIs, other drivers or every possible source of remote-video flicker.
