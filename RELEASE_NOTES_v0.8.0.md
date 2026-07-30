# UU Remote for Linux v0.8.0

This is the first public GitHub release of the independent, unofficial Linux
controller compatibility project.

Highlights:

- controller-side UU Remote on Ubuntu/Debian through an isolated Wine prefix;
- CPU/OpenH264 and experimental NVIDIA NVDEC decoding;
- native decoder selection, tray integration, XDG autostart, sleep inhibition,
  Wake-on-LAN support, and safe automatic updates;
- X11 `Super+Space` forwarding for the focused remote-control window;
- Chinese and English documentation.

Install:

```bash
sudo apt install ./uuyc-linux-controller_0.8.0_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
uuyc-linux-controller --diagnose
uuyc-linux-controller
```

The package does not contain the proprietary NetEase installer. First-time
setup downloads the official client only after explicit EULA acceptance.

This project is not affiliated with, endorsed by, or supported by NetEase.
