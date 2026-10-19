# Mutter 46 keyboard-state backport / 键盘状态根因修复

This optional component belongs to release **v1.1.43**. It fixes a compositor
state divergence that the UU native lock-state observer alone cannot repair.
The universal UU `.deb` continues to support X11 and GNOME Wayland without
replacing the system compositor. Install the separate Mutter bundle only on
the exact supported platform below; installing UU alone does not install it.

本目录及 1.1.43 的独立 Mutter 附件用于修复合成器内部的大小写状态错位。
此前 UU 的原生状态读取修复不能单独消除此根因。UU 通用 DEB 不自动替换系统
合成器；需要此修复时，另行安装与系统版本严格匹配的 Mutter 附件。

## Supported build / 适用范围

- Ubuntu **24.04**, **amd64**, GNOME Wayland, Mutter ABI 14.
- Original Ubuntu source/package version **46.2-1ubuntu0.24.04.18**.
- Backport package version **46.2-1ubuntu0.24.04.18+pjlablock3**.
- Four runtime packages: `libmutter-14-0`, `gir1.2-mutter-14`,
  `mutter-common`, `mutter-common-bin`.

Other distributions, Mutter versions and architectures need their own build
and acceptance testing. Do not downgrade a newer Ubuntu security update to
install this bundle. 如果系统版本不同或已有更新的安全更新，应重新移植和验收，
不要强行降级。PJLAB 已部署并通过验收，无需为本次发布再次安装或重启。

## Root cause / 根因

Mutter 46 could maintain different modifier states in its native seat and
Wayland keyboard. An IME consuming Caps Lock events could make those states
remain out of phase. A separate temporary IBus engine reproduced the same
failure, so this is not evidence of a Rime-only defect. A native state query
can be correct while an application still receives the wrong state.

The patch removes duplicate Wayland accounting, preserves the native
modifier tuple, delivers the captured state in the correct order around
each key event, refreshes state on focus entry, and notifies Wayland even
when the IME consumes the key. It preserves the exported Mutter 46 ABI.
There is no polling daemon or periodic Caps Lock reset.

原生座席与 Wayland 键盘重复维护状态，输入法过滤锁键时可能产生持久偏差；
还必须修正逐事件的状态发送顺序，才能保证 XWayland 同样正确。补丁统一权威
状态来源，补齐事件顺序、焦点刷新和被输入法过滤后的通知，同时保留 ABI。

Upstream basis, including the required follow-up:

- [aeac0f3](https://github.com/GNOME/mutter/commit/aeac0f33c2d06ac356af253ed52c175b48999f92)
- [1940c6e](https://github.com/GNOME/mutter/commit/1940c6eba938a9413b93694573501153788d1f0f)
- [7d0d128 / MR 4546](https://github.com/GNOME/mutter/commit/7d0d128e005fb737df7a8eea5e472d6c229898e1)
- [0828fd6 / MR 4686](https://github.com/GNOME/mutter/commit/0828fd6169c17ca54633f76936f03ac496f6b01e)

## Download and install / 下载与安装

Download from the [v1.1.43 release](https://github.com/GuoWQ222/uu-remote-for-linux/releases/tag/v1.1.43):

- `mutter-46.2-pjlablock3-ubuntu24.04-amd64.tar.xz`: four tested Debian
  packages, their checksums, source provenance and this guide.
- `mutter-46.2-pjlablock3-source.tar.xz`: complete corresponding patched
  source, Debian packaging, upstream licenses and real-desktop test scripts.

Verify the outer archive against the release `SHA256SUMS`, extract it, and
enter `mutter-46.2-pjlablock3-ubuntu24.04-amd64/`. Save open work and keep a
working SSH management channel before changing the compositor. The following
transaction checks the exact platform, downloads rollback packages first,
then installs all four packages together:

先核验 Release 校验文件，再解压并进入目录。保存工作并保留 SSH 管理通道。
以下命令只适用于当前安装版本恰为原始版本的机器，备份下载失败即停止：

```bash
(
set -eu
. /etc/os-release
test "$ID:$VERSION_ID:$(dpkg --print-architecture)" = 'ubuntu:24.04:amd64'
base='46.2-1ubuntu0.24.04.18'
test "$(dpkg-query -W -f='${Version}' libmutter-14-0)" = "$base"
backup="$PWD/mutter-before-pjlablock3"
mkdir -p "$backup"
(
cd "$backup"
apt download "libmutter-14-0=$base" "gir1.2-mutter-14=$base" \
  "mutter-common=$base" "mutter-common-bin=$base"
)
sha256sum --strict --check SHA256SUMS
sudo apt install ./libmutter-14-0_*.deb ./gir1.2-mutter-14_*.deb \
  ./mutter-common_*.deb ./mutter-common-bin_*.deb
sudo apt-mark hold libmutter-14-0 gir1.2-mutter-14 mutter-common mutter-common-bin
)
```

Log out and back in to load the new compositor; if the host uses a custom
headless launcher with a library checksum guard, update that guard to the
installed library after verifying package integrity. Do not run the acceptance
scripts automatically during installation: they occupy the keyboard and some
restart the graphical desktop.

注销并重新登录后补丁生效。自定义无头桌面的库校验保护须同步更新。
`apt-mark hold` prevents a normal unattended update from silently replacing
the tested backport. Review security updates and rebase or confirm the full
upstream fix before removing the hold; it is not a permanent substitute for
security updates.

## Rollback / 回退

From the extracted bundle directory, with the four original packages saved
above, restore all four in one transaction, then log out/in:

```bash
sudo apt-mark unhold libmutter-14-0 gir1.2-mutter-14 mutter-common mutter-common-bin
sudo apt install --allow-downgrades ./mutter-before-pjlablock3/*.deb
```

These generic commands do not modify a custom headless launcher. Restore its
previous checksum guard as well if you changed one. PJLAB has its own retained
original packages and launcher recovery procedure; its private host paths are
not required by this public bundle.

## Source and validation / 源码与验收

`mutter-46-lock-state.patch` is the complete source/packaging diff against the
Ubuntu source version above. `SOURCE.json` records exact revisions and hash.
The complete source archive is already patched: do not apply the patch twice.
To inspect the patch on an unmodified matching Ubuntu source tree:

```bash
git apply --check /path/to/mutter-46-lock-state.patch
git apply /path/to/mutter-46-lock-state.patch
```

Install normal Ubuntu Mutter build dependencies, extract the source archive
in a path without spaces, and run `dpkg-buildpackage -b -uc -us` inside it.
The source includes Debian packaging and `pjlab-tests/`. Those tests require
the documented live desktop and installed UU input bridge; see their README
before running them.

Acceptance on 2026-10-19 used two animated 4K landscape displays and eight CPU
workers. Across native/UU input and Wayland/XWayland clients: **63,806 key
events, 7,360 Caps toggles, 23,680 actual characters, 2,560 text checks and
4,196 state checks**, with zero text/state errors. It covered 640 IME filtering
cycles, 320 bursts and 32 cross-monitor moves. Four Mutter regression suites,
12 real input/capture reconnects and two complete desktop restarts passed.
The user then confirmed UU and Sunlogin reconnect, Caps off/on/off and left
Shift input-method switching in both application backends.

验收同时核对实际字符与原生/应用状态，不以接口一致代替输入结果。完整私人
日志保留在本地；公开的 `acceptance.json` 不包含认证材料、账户数据或原始
人工输入。夹具准备中止不计为通过。结论针对当前补丁及已覆盖场景，后续系统
升级仍须复核。

The Mutter-derived patch and source retain **GPL-2.0-or-later**, upstream
per-file notices, Debian copyright and `COPYING`; they are not relicensed
under this wrapper project's 0BSD license.
