# UU Remote Mutter public build report

## Result

- Source/version: `mutter 46.2-1ubuntu0.24.04.16+0uuremote3`
- Maintainer: `Wenqi Guo <129141646+GuoWQ222@users.noreply.github.com>`
- Preserved field: `Original-Maintainer: Debian GNOME Maintainers <pkg-gnome-maintainers@lists.alioth.debian.org>`
- Base source: exact Ubuntu Noble `.16` source package.
- Embedded patches are byte-for-byte identical to the two repository patches.
- Canonical installable output: `runtime/` (the four packages needed by the UU Remote workaround).
- Complete binary set: `binary/` (8 debs) and `debug/` (5 ddebs).
- Complete source set: `source/` (orig tarball, Debian tarball, dsc, and source changes).
- The dsc, changes, and buildinfo files are intentionally unsigned; no metadata was fabricated.

## Runtime SHA-256

```text
3d49023f7269e3df001b66988edca267e38d5bc344b47634b383d6f85684b2e6  gir1.2-mutter-14_46.2-1ubuntu0.24.04.16+0uuremote3_amd64.deb
f1b1ca936c9df84bc6f7612c99ab605471e289ea07ffcf6cdb2bb3d8ead28c16  libmutter-14-0_46.2-1ubuntu0.24.04.16+0uuremote3_amd64.deb
2348a36c6e4114abb6f2def6ed1c84d4dc3942e5033ee6a33b8af467bae907fc  mutter-common-bin_46.2-1ubuntu0.24.04.16+0uuremote3_amd64.deb
e0af63698758d3b77d0cd8d4bb284d2b8ad8b499da713d163bc9b36123928baf  mutter-common_46.2-1ubuntu0.24.04.16+0uuremote3_all.deb
```

## Verification

- Two independently extracted source trees completed clean `dpkg-buildpackage -b` builds in an isolated Ubuntu 24.04.4 amd64 rootfs.
- All 8 installable debs and all 5 ddebs are byte-for-byte identical between the two clean builds.
- The dsc, Debian tarball, orig tarball, and source changes are byte-for-byte identical between the two source builds.
- ABI against Ubuntu `.16`: `libmutter-14.so.0` SONAME unchanged; all 928 exported dynamic symbol names match; packaged `symbols`, `shlibs`, and ELF `DT_NEEDED` lists match exactly.
- Runtime control metadata for the four release packages matches Ubuntu `.16` after normalizing only the intentional package version.
- `apt-get -s` resolved and configured the four runtime packages without unmet dependencies.
- Ubuntu buildd-style `pkgbinarymangler` was enabled in the temporary rootfs: `mutter-common` has the same 28-entry package shape as Ubuntu and contains no duplicate `.mo` files.
- Every SHA-256 entry referenced by the canonical binary and source changes files validates against a real artifact.
- Generated dsc extraction succeeds and applies both UU Remote patches.

The two `.buildinfo` files differ by their real `Build-Date`, as expected. The buildd-only raw translations tar also records staging mtimes and is not byte-reproducible; it is not an installable package. Neither exception affects the byte-identical deb/ddeb/source results. The canonical build2 translations tar is included because the canonical binary changes file references it.

## Build environment and limits

- All work was performed under `/tmp`; no sudo/pkexec call, system install, or repository edit was made.
- The dpkg metadata overlay was reconstructed from 595 real Ubuntu deb archives; `dpkg-checkbuilddeps`, `dpkg-shlibdeps`, and the complete debhelper binary sequence passed.
- `SOURCE_DATE_EPOCH=1787396857`, fixed build path `/build/mutter-46.2`, and `DEB_BUILD_OPTIONS="parallel=8 nocheck"` were used.
- Hardware/session-dependent upstream tests were not executed because of `nocheck`; all test code and test packages were compiled. Packaging, archive integrity, ABI, dependency, source round-trip, and reproducibility checks were executed.

## Version ordering

`46.2-1ubuntu0.24.04.16 < 46.2-1ubuntu0.24.04.16+0uuremote3 < 46.2-1ubuntu0.24.04.17`.

The earlier private version `...+uuremote3` sorts above the public `...+0uuremote3`. A machine already running the private build therefore needs an explicit allowed downgrade for this one migration; users on Ubuntu `.16` receive a normal upgrade, and a future Ubuntu `.17` supersedes this package normally.
