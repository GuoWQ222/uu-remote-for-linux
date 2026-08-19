# Third-party notices

NetEase UU Remote is proprietary software. This repository does not include or
redistribute the NetEase installer, application binaries, or account data. At
runtime, users may explicitly request a download from the official NetEase
release endpoint and remain subject to the upstream license:

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
