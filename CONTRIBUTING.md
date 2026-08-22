# Contributing

Thank you for improving UU Remote for Linux.

## Development workflow

1. Work on a focused branch.
2. Keep changes limited to the Linux wrapper and compatibility components.
3. Run the offline test suite:

   ```bash
   make test
   ```

4. Build and inspect the direct-download release assets:

   ```bash
   make release-assets
   dpkg-deb --info dist/uu-remote-for-linux-x11_*_amd64.deb
   unzip -l dist/uu-remote-for-linux-wayland_*_amd64.zip
   (cd dist && sha256sum --strict -c SHA256SUMS)
   ```

5. Describe manual controller-side testing and the exact UU/Wine versions in
   the pull request.

## Repository hygiene

- Never commit the proprietary NetEase installer, client binaries, account
  data, Wine prefixes, cookies, device IDs, or complete client logs.
- Do not commit generated release `.deb` files under `dist/`. A narrowly
  scoped, version-locked third-party compatibility payload may be vendored
  only under `third_party/` when its provenance, license, complete
  corresponding source, fixed checksums, metadata verifier, and fail-closed
  runtime gate are included in the same change.
- Preserve license notices and corresponding source for bundled third-party
  components.
- New binary compatibility patches must be version-locked by upstream hashes
  and must fail closed on an unknown file.
- Avoid global Wine, desktop, or power-management changes. Scope behavior to
  the project's isolated prefix and restore user settings exactly.

## Reproducible compatibility components

The Windows Event Log shim can be rebuilt with:

```bash
sudo apt install binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev
make shim
```

Hardware-decoding source, revisions, checksums, and build details are recorded
in `third_party/HWDECODE.md`.

The Ubuntu 24.04 Mutter capture repair, clean-source build requirements,
payload verification, and rollback procedure are documented in
`packaging/mutter/README.md`. Public Mutter binaries must come from the exact
source package in a clean Noble build environment; local incremental hotfix
packages are suitable for diagnosis only and must not be published. A release
candidate must also pass the documented real APT install, legacy-normalization,
and rollback gate in a disposable Noble environment; mock tests and
`apt-get -s` alone are not release acceptance.

New releases expose two direct downloads: a complete standalone X11 `.deb` and
a GNOME Wayland ZIP containing the shared runtime, Wayland profile, and four
verified Mutter packages. The extracted Wayland directory must install through
one outer `apt install ./*.deb` transaction; no maintainer script may invoke
APT or dpkg. Release checksums must cover both public assets, while the ZIP's
internal `SHA256SUMS` must cover all six Debian packages.
