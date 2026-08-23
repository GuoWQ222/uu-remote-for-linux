# Direct GitHub Release package

Public releases contain one user-facing amd64 download and do not require an
APT repository or archive key:

- `uu-remote-for-linux_VERSION_amd64.deb` contains both the X11 XTest path and
  GNOME Wayland Portal/native three-buffer DMA-BUF path.

Install it directly with one normal transaction:

```bash
sudo apt install ./uu-remote-for-linux_VERSION_amd64.deb
```

The launcher selects the backend from the live user session, where DISPLAY,
XDG_SESSION_TYPE, and Portal availability are reliable. The Debian maintainer
scripts do not guess the session or install another package. The package does
not replace or patch the desktop compositor, and no logout is required.

Build the release asset with:

```bash
make release-assets
```

Only the universal `.deb` and its checksum manifest are intended as public
GitHub Release downloads.
