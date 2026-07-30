# Contributing

Thank you for improving UU Remote for Linux.

## Development workflow

1. Work on a focused branch.
2. Keep changes limited to the Linux wrapper and compatibility components.
3. Run the offline test suite:

   ```bash
   make test
   ```

4. Build and inspect the Debian package:

   ```bash
   make deb
   dpkg-deb --info dist/uuyc-linux-controller_*_amd64.deb
   dpkg-deb --contents dist/uuyc-linux-controller_*_amd64.deb
   ```

5. Describe manual controller-side testing and the exact UU/Wine versions in
   the pull request.

## Repository hygiene

- Never commit the proprietary NetEase installer, client binaries, account
  data, Wine prefixes, cookies, device IDs, or complete client logs.
- Do not commit generated `.deb` packages; releases carry those artifacts.
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
