# Third-party notices

NetEase UU Remote is proprietary software. This repository does not include or
redistribute the NetEase installer, application binaries, or account data. The
desktop/tray identification icon is the icon resource from the official
`GameViewer.exe`; the artwork and all NetEase/UU names and trademarks remain
the property of their respective owner. At runtime, users may explicitly
request a download from the official NetEase release endpoint and remain
subject to the upstream license:

https://uuyc.163.com/contact/20240402/40294_1146065.html

Wine and Microsoft Edge WebView2 are separate third-party projects with their
own licenses and distribution terms.

The optional NVIDIA hardware codec bridge includes:

- DXVK 3.0.2 `d3d11.dll` and `dxgi.dll`, licensed under the zlib license;
- Wine CUDA and CUVID relay code from `SveSop/nvcuda` and `SveSop/nvenc`,
  licensed under LGPL-2.1-or-later;
- a modified `nvcuda.dll.so` that adds the UU Remote CUDA-to-D3D11 CPU-bounce
  bridge, cross-device upload submission, and explicit CUDA-device selection.
  The complete corresponding source is shipped in
  `third_party/sources/nvcuda-uu-remote-v0.8.tar.xz`.
- a modified `nvencodeapi64.dll.so` relay that converts UU's D3D11 input into
  CUDA-backed NVENC input and forwards it to Linux `libnvidia-encode.so.1`.
  Its corresponding source is `third_party/sources/nvenc-nvcuvid-v0.5.tar.xz`;
  the applied patch and bridge overlay are under `third_party/nvenc/`.

Exact revisions, checksums, build details, licenses, and the corresponding
CUVID/NVENC source archive are documented in `third_party/HWDECODE.md`.

The optional Mutter 46 keyboard-state backport under
`contrib/mutter-46-lock-state` derives from GNOME Mutter and Ubuntu's Debian
packaging. It retains GPL-2.0-or-later, upstream per-file notices and the
included `COPYING`; it is not covered by the wrapper's 0BSD license. The
universal DEB carries four patched and four original rollback packages as
inert, version-checked payloads under its private runtime directory. Complete
corresponding source, including Debian build scripts and licenses, is shipped
in `/usr/share/doc/uu-remote-for-linux/source/mutter-46.2-pjlablock3-source.tar.xz`.
The complete source for the original rollback packages can be reconstructed
by reversing the included source/packaging patch on that archive. See
`contrib/mutter-46-lock-state/SOURCE.json` and README for exact revisions,
licenses and build instructions. The outer DEB never directly overwrites
system compositor files; activation is a separate, opt-in APT transaction.
