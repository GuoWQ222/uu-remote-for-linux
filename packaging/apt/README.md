# Signed APT packaging

The repository exposes two mutually exclusive installation entry packages:

- `uu-remote-for-linux-x11` depends only on the shared
  `uu-remote-for-linux` runtime and never changes Mutter.
- `uu-remote-for-linux-wayland` is restricted to Ubuntu 24.04 Noble amd64 with
  GNOME Shell 46 and requires at least the four verified Mutter package
  versions. APT installs and configures the runtime and compositor packages in
  one normal transaction; later Ubuntu revisions can supersede the repair and
  no maintainer script invokes APT or `dpkg`.

The two names distinguish desktop session backends, not CPU architectures.
Both currently contain amd64 software because the bundled Wine and native
bridges are built for x86-64.

`uu-remote-for-linux-archive-keyring` installs only the repository public key
and its deb822 source definition. The production private signing key must stay
outside Git and is imported into an isolated `GNUPGHOME` only for publishing.

Build the entry and keyring packages with:

```bash
./packaging/apt/build-packages.sh --archive-keyring /path/to/public-key.gpg
```

Build and sign a complete repository with:

```bash
./packaging/apt/build-repository.sh \
  --output /empty/output/directory \
  --gnupg-home /private/gnupg-home \
  --signing-key FULL_FINGERPRINT \
  --passphrase-file /private/passphrase-file \
  --expected-public-key ./packaging/apt/uu-remote-for-linux-archive-keyring.gpg
```

The passphrase file must be a non-symlink regular file owned by the current user
with mode `0600`. It is passed directly to local GnuPG and is never placed in a
process environment. The exported public key must match the fixed repository
copy byte for byte before packaging or signing begins.

For GitHub Pages, build the complete static site locally:

```bash
./packaging/apt/build-site.sh \
  --output /empty/site-directory \
  --gnupg-home /private/gnupg-home \
  --signing-key FULL_FINGERPRINT \
  --passphrase-file /private/passphrase-file
```

Publish only that already-signed site to the `gh-pages` branch and configure
Pages to serve the branch root. No GitHub workflow or Secret receives the
private key. A compromise of hosted files cannot create a signature accepted
by clients that already trust the fixed archive public key.

The output includes `Packages`, `Sources`, compressed indexes, `Release`,
`InRelease`, `Release.gpg`, the public key, the shared runtime, both entry
packages, the keyring package, four Mutter binaries, and the exact corresponding
Mutter source package.
