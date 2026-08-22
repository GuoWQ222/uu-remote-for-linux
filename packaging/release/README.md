# Direct GitHub Release packages

Public releases contain two user-facing downloads and do not require an APT
repository or archive key:

- `uu-remote-for-linux-x11_VERSION_amd64.deb` is a complete standalone X11
  package. It contains the shared runtime and does not replace Mutter.
- `uu-remote-for-linux-wayland_VERSION_amd64.zip` is an offline bundle for
  Ubuntu 24.04 Noble amd64 with GNOME Shell 46. It contains the shared runtime,
  the Wayland profile, and the four verified Mutter repair packages.

The Wayland archive is installed after extraction with one normal transaction:

```bash
sudo apt install ./*.deb
```

The archive contains `SHA256SUMS`; the builder verifies its exact inventory and
reproducibility. No package maintainer script invokes APT or dpkg. Installing
the Wayland bundle changes Mutter, so the user must log out and back in before
the patched GNOME Shell library is active.

Build both release assets with:

```bash
make release-assets
```

The generated shared runtime `.deb` remains an internal member of the Wayland
ZIP. Only the standalone X11 `.deb` and Wayland `.zip` are intended as public
GitHub Release downloads.
