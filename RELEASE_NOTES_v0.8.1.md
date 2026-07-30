# UU Remote for Linux v0.8.1

This maintenance release applies the project's new independent public name
throughout the repository and downloadable package:

- English name: **UU Remote for Linux**
- Chinese name: **UU 远程（Linux 版本）**
- Repository: `GuoWQ222/uu-remote-for-linux`

The command, Debian package name, and existing data directories remain
`uuyc-linux-controller` for backward-compatible upgrades.

The GitHub Actions workflow now validates the native tray under a virtual X11
display, runs ShellCheck, builds the Debian package, and uploads the resulting
artifact.

Install:

```bash
sudo apt install ./uu-remote-for-linux_0.8.1_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
uuyc-linux-controller --diagnose
uuyc-linux-controller
```

The package does not contain the proprietary NetEase installer. First-time
setup downloads the official client only after explicit EULA acceptance.

This project is not affiliated with, endorsed by, or supported by NetEase.
