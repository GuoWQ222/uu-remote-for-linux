# Direct GitHub Release package

Public releases contain one user-facing amd64 download:

- `uu-remote-for-linux_VERSION_amd64.deb` contains the X11 XTest path and
  GNOME Wayland Portal/native three-buffer DMA-BUF path.

```bash
sudo apt install ./uu-remote-for-linux_VERSION_amd64.deb
```

The launcher selects the backend from the live user session. The package has
no custom Mutter dependency, nested system DEBs, Mutter source archive or
compositor repair manager. Neither installation nor first launch installs,
pins or rolls back desktop packages. Installing UU itself requires no logout.
Existing independent system repairs are left unchanged.

Build the package with `make release-assets`. CI produces the universal DEB,
`SHA256SUMS` and `CONTENTS.txt` for verification. Publish only the DEB as the
release asset and place its SHA-256 in the release notes; do not publish the
CI manifests or platform-specific system packages as extra release downloads.

[Mutter root-cause evidence](../../contrib/mutter-46-lock-state/README.md)
remains a source reference separate from UU distribution. The native lock-state
observer does not repair compositor-internal state divergence. Use a supported
system update containing the full fix; do not describe this universal package
as fixing all affected GNOME builds.
