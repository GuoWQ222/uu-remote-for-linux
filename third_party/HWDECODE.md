# Hardware-decoding bridge provenance

The optional bridge is an NVIDIA-only, correctness-first compatibility path:

1. UU Remote submits H.264/H.265 bitstreams to Linux NVDEC through the Wine
   CUDA/CUVID relays.
2. The modified CUDA relay copies each decoded plane from CUDA device memory
   to a CPU buffer.
3. `ID3D11DeviceContext::UpdateSubresource` uploads the planes to the DXVK
   D3D11 textures expected by UU Remote.

This removes the missing CUDA/D3D11 interop blocker. It is hardware decoding
with CPU copyback, not a zero-copy implementation. The UU-specific v0.6 relay
also accepts `UUYC_CUDA_DEVICE=<ordinal>` and binds the D3D11 CUDA-context
entry points to that enumerated CUDA device. Invalid ordinals fail explicitly.

UU Remote 4.34.0.8979 currently auto-selects implementation 32 (DXVA11)
under Wine, while implementation 33 is NVDEC. Because DXVK does not implement
the D3D11 video-decoder interfaces used by this probe, the parent process must
select and record implementation 33 for the capability result to be usable.

The launcher applies two version-locked three-byte edits to the user's
installed `streamer.dll`:

```text
offset 0x94dc03: 89 55 d0 -> b2 21 90  (select implementation 33)
offset 0x94e4ee: 8b 45 d0 -> 6a 21 58  (record implementation 33)
```

The original file must have SHA-256
`2144cd9c199ee21ef55da984ef38d719e337a8202b26522458e56bff648ffb60`;
the generated patched copy must have SHA-256
`abf2e482c65397138aa69430e3413a10f80d942a28817c22f8f8ff78e3c5ca70`.
Both the hashes and original bytes are checked before replacement. The
original proprietary DLL remains in the user's prefix, is restored by
`--disable-hwdecode`, and is not distributed by this project. The decoder
capability cache is invalidated on enable and restored on disable.

## Pinned components

- DXVK 3.0.2 release:
  `https://github.com/doitsujin/dxvk/releases/tag/v3.0.2`
- `SveSop/nvidia-libs` 1.0.2 release:
  `https://github.com/SveSop/nvidia-libs/releases/tag/v1.0.2`
- nvcuda source revision:
  `772b344a3594fca916999c5c5288d875332de482`
- nvenc/nvcuvid source revision:
  `09ec9e1c8b25a351e415a6d0361bfacad0fd710c`

The complete modified nvcuda source and the corresponding nvcuvid source are
included under `third_party/sources/`. Their archive SHA-256 values are:

```text
1ba879e8cce534a3d7d6b48b0ad5900aac0ce2e4ca7ebd1ac16f8f4b94185d16  nvcuda-uuyc-v0.6.tar.xz
e2b5e99ef3a849a8ed779ce8c786505578d82169b25b08e2a3905702550751be  nvenc-nvcuvid-v0.5.tar.xz
```

Runtime binary checksums are in
`lib/uuyc-linux-controller/hwdecode/MANIFEST.sha256`.

## Rebuilding the modified relay

The source uses Meson and Wine's `winegcc`. With Wine development headers,
Meson, and Ninja available:

```bash
tar -xf third_party/sources/nvcuda-uuyc-v0.6.tar.xz
meson setup \
  --cross-file nvcuda-uuyc-v0.6/build-wine64.txt \
  --buildtype release \
  -Dfakedll=false \
  build-nvcuda \
  nvcuda-uuyc-v0.6
ninja -C build-nvcuda
```

The resulting Unix relay is
`build-nvcuda/dlls/nvcuda/nvcuda.dll.so`.

## Licenses

The Wine CUDA/CUVID relays and this modification are
LGPL-2.1-or-later. DXVK is under the zlib license. License texts are installed
beside the runtime in `hwdecode/licenses/`.
