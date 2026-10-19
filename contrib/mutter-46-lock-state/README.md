# Bundled Wayland keyboard repair / 内置 Wayland 键盘根因修复

**v1.1.43 needs one DEB download.** The universal UU package includes the
Mutter repair, original rollback packages and complete corresponding source.
On the first interactive UU launch, a matching GNOME Wayland system is offered
the repair. It is opt-in and requires administrator authentication. Installing
the UU DEB itself does not replace the running desktop or invoke nested APT.

**1.1.43 只需下载一个 DEB。**修复包、原版回退包和完整源码均已内置。
首次交互启动 UU 时检测当前系统，匹配后提示是否应用；用户选择后进行管理员
认证。UU 安装时只放入组件，不直接覆盖合成器，也不自动重启桌面。

## Supported build / 适用范围

- Ubuntu 24.04, amd64, GNOME Shell 46 and Mutter ABI 14.
- Original four packages: `46.2-1ubuntu0.24.04.18`.
- Patched version: `46.2-1ubuntu0.24.04.18+pjlablock3`.
- Managed packages: `libmutter-14-0`, `gir1.2-mutter-14`, `mutter-common`,
  `mutter-common-bin`.

Already patched systems are not reinstalled. Other versions, architectures,
locally modified packages and incompatible dependency plans are left unchanged.
The tool does not downgrade newer security updates. Source-only user installs
can inspect eligibility but cannot elevate user-owned repair code; install the
system DEB to activate the repair.

已安装匹配补丁的 PJLAB 不重复处理。版本不同、文件有本地修改，或依赖计划
要求更改其他包时会停止，不降级较新的系统包。其他 X11/Wayland 环境仍可使用
UU，内置系统修复不会自动应用。无图形环境和登录自启动不会弹出修复提示。

## Use and rollback / 使用与回退

Install the one DEB normally and start UU. If you accept the repair, the
helper checks all eight bundled package hashes and metadata, verifies the
installed baseline, simulates an offline four-package APT transaction and
rejects unrelated changes/removals. It retains the original packages and
previous hold settings under `/var/lib/uu-remote-for-linux/wayland-lock-fix/`.

Accepting the dialog installs all four patched packages together and holds
them against silent replacement. Save work and log out/in when convenient to
load the repaired compositor. The tool never automatically logs out or restarts
GDM, GNOME Shell, UU or Sunlogin. A failed installation attempts to restore all
four original packages and previous package holds; an unsuccessful recovery
is reported explicitly with its retained state and backup.

接受提示后，工具离线安装四个配套包并设置版本锁定；请在方便时保存工作、注销
并重新登录。当前桌面、UU 和向日葵不会自动重启。安装失败会尝试恢复原版包和
原有锁定；回退失败会明确报错并保留记录，不宣称修复成功。

```bash
# Read-only status / 只读检查
uu-remote-for-linux --wayland-keyboard-status

# Retry after choosing "not now" / 暂不处理后重新显示提示
uu-remote-for-linux --repair-wayland-keyboard

# Restore this tool's original packages and holds / 恢复原版和原有锁定
uu-remote-for-linux --rollback-wayland-keyboard
```

Rollback also requires a later logout/login. It refuses to overwrite packages
changed by another upgrade or local modification. A previously external repair
with no record from this helper must use its original recovery procedure.
Removing UU does not automatically roll back the separately managed Mutter
packages; recovery data is retained. After removal, inspect `state.json` and
`ROLLBACK.txt` in the path above for the original package transaction and holds.

Version holds are protection for this tested backport, not a permanent substitute
for security updates. Review new official updates, confirm the complete fix or
rebase and retest, then restore the normal update path.

## Root cause and evidence / 根因与证据

Old Mutter can independently calculate native-seat and Wayland keyboard
modifiers. Filtering a Caps event in an IME can leave them persistently out of
phase: native Caps off, Chrome's received Caps on. Chinese composition can
still work; Shift switches to direct English and exposes uppercase output.
Another Caps press flips both contradictory states rather than fixing them.

The backport uses the native authoritative modifier tuple, removes duplicate
Wayland accounting, delivers per-event state in order, refreshes on focus and
notifies clients even if the IME consumes the key. It retains the original
Mutter 46 ABI and does not use periodic Caps resets or a mirror daemon.

上游依据：

- [7d0d128 / MR 4546](https://github.com/GNOME/mutter/commit/7d0d128e005fb737df7a8eea5e472d6c229898e1)
- [0828fd6 / MR 4686 follow-up](https://github.com/GNOME/mutter/commit/0828fd6169c17ca54633f76936f03ac496f6b01e)
- Dependencies `aeac0f3` and `1940c6e`; Mutter 46 IME notification/ABI integration.

Real-desktop acceptance included two animated 4K displays, eight CPU workers,
63,806 key events, 7,360 Caps toggles, 23,680 characters, 2,560 text checks,
4,196 state checks, 12 input/capture reconnects, two complete desktop restarts
and four Mutter regression suites. UU and Sunlogin manual tests passed in
Wayland/XWayland fields. A subsequent isolated Chrome/Rime test reproduced
Chinese -> Shift -> uppercase on original Mutter in 5/5 controlled cases;
patched Mutter passed the matching 5/5 cases, including Rime's original
`Caps_Lock: clear` and customized `noop` bindings. The specific historical
event that first triggered the user's mismatch was not captured.

## Corresponding source / 对应源码与许可证

The DEB installs the full patched source, Debian packaging, licenses and
real-desktop test scripts here:

`/usr/share/doc/uu-remote-for-linux/source/mutter-46.2-pjlablock3-source.tar.xz`

The patch, `SOURCE.json`, acceptance summary and this guide are in
`/usr/share/doc/uu-remote-for-linux/mutter-lock-fix/`. The full license is in
the source archive and `/usr/share/common-licenses/GPL-2`. Exact file hashes are in
the runtime payload's `manifest.json`; the outer DEB's SHA-256 is published
in the Release notes. No separate archive or checksum download is required.

The source archive is already patched. To rebuild, extract it into a path
without spaces, install Ubuntu's normal Mutter source build dependencies and
run `dpkg-buildpackage -b -uc -us`. To reconstruct the original rollback
packages' corresponding source, reverse `mutter-46-lock-state.patch` in the
extracted source tree before building. The extra `pjlab-tests/` files do not
affect the original Debian build.

Mutter-derived code, packages and source retain **GPL-2.0-or-later**, upstream
per-file notices and Debian copyright. They are not relicensed under this
wrapper's 0BSD license. The original/modified source and build scripts are
provided in the same DEB as the optional binaries.
