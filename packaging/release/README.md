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

The standard build produces the universal `.deb` and its checksum manifest.

Release v1.1.43 additionally publishes the optional Ubuntu 24.04 / Mutter 46
keyboard-state backport as a separate four-package archive and complete
corresponding source archive. See
[contrib/mutter-46-lock-state](../../contrib/mutter-46-lock-state/README.md)
for the exact version, source provenance, tests, installation and rollback.
The final release `SHA256SUMS` covers the UU package and both archives. The
standard CI checksum file covers only its own UU artifact; release assembly
adds the two independently validated archives before publishing.

The universal package still has no custom Mutter dependency or payload.
Do not bundle or automatically install these platform-specific system
packages inside it, or describe installation of UU alone as deploying the
Mutter root fix.
