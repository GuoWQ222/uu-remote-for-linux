# Mutter keyboard-state evidence / 键盘状态根因证据

This directory preserves a source backport and reproduction/test evidence.
**It is not included in the universal UU DEB and is not a separate release
download.** UU installation and launch do not install, pin, roll back or
prompt to replace system compositor packages. Release v1.1.43 provides only
the universal UU DEB.

本目录保留源码补丁、复现及验收证据，不作为通用 UU DEB 的组件，也不额外
发布 Mutter 安装包。安装或启动 UU 不安装、锁定、回退系统合成器，也不弹出
系统补丁应用提示。已独立维护的 PJLAB 修复不会因更新 UU 而被撤销。

## Root cause / 根因

Affected Mutter builds maintain native-seat and Wayland keyboard modifier
states separately. Filtering lock-key events through an IME can leave those
states out of phase. Applications then receive an incorrect Caps state even
when the native query is correct. Switching from Chinese composition to direct
English exposes the uppercase output; Shift need not have enabled Caps Lock.

The source backport removes duplicate accounting, uses the native modifier
tuple, orders modifier delivery with each key event, refreshes focus and
notifies clients even when the IME consumes a key. Upstream basis:

- [7d0d128 / MR 4546](https://github.com/GNOME/mutter/commit/7d0d128e005fb737df7a8eea5e472d6c229898e1)
- [0828fd6 / MR 4686 follow-up](https://github.com/GNOME/mutter/commit/0828fd6169c17ca54633f76936f03ac496f6b01e)
- Dependencies `aeac0f3` and `1940c6e`, plus Mutter 46 IME notification/ABI integration.

输入法过滤事件是触发条件，合成器保留两份相反状态是已复现的缺陷。独立 IBus
引擎也能触发，因此不能归结为 Rime 独有问题。UU 改读原生状态只修正自己的
观察来源，无法直接改写合成器向 Chrome 或其他应用发送的状态。

## Distribution boundary / 通用包边界

Use a supported, compatible system update containing the complete fix when
available. A version number alone is insufficient: distributions backport
different changes, so verify the actual patch and reproduce the input cases.
Keep GNOME Shell/Mutter ABI and distribution support aligned rather than
replacing a major Mutter library in isolation.

Changing input-method lock-key handling or the UU injection path could avoid
some triggers. Such a workaround must first pass the original-Mutter IME,
Wayland/XWayland, reconnect and multi-client cases. No universally reliable
UU-only workaround has been demonstrated. Changing Chrome's backend alone
does not establish a system-wide fix. An X11 desktop avoids this specific
Wayland path but changes the user's requested display environment.

优先使用包含完整修复、与桌面配套的系统更新。UU 通用包只负责自己的兼容层，
不承诺修复系统合成器内部缺陷。修改 Rime 或输入注入策略可能避开部分触发，
但尚未证明能在原版 Mutter 下覆盖所有应用和两个远程客户端，不标记为根治。

## Source and validation / 源码及验收

`mutter-46-lock-state.patch` and `SOURCE.json` record the complete backport
against Ubuntu `46.2-1ubuntu0.24.04.18`, ABI 14, amd64. The tested build is
`46.2-1ubuntu0.24.04.18+pjlablock3`. This exact baseline is research provenance,
not a dependency or supported universal payload. Other builds require their
own integration and acceptance. Do not downgrade newer security updates.

The source patch includes the independent IBus reproducer under
`pjlab-tests/`; the full source archive retained in commit `60e2057` also
contains the later real Chrome/Rime test scripts. With controlled lock-event
filtering, original
Mutter reproduced Chinese -> Shift -> uppercase in 5/5 paired cases; patched
Mutter passed the matching 5/5 cases. Both Rime `Caps_Lock: clear` and `noop`
were covered. The user's original historical triggering event was not captured.

Earlier real-desktop acceptance used two animated 4K displays and eight CPU
workers: 63,806 key events, 7,360 Caps toggles, 23,680 characters, 2,560 text
checks and 4,196 state checks, with zero text/state errors. Four Mutter
regression suites, 12 bridge reconnects and two desktop restarts passed.
UU/Sunlogin manual acceptance passed in Wayland/XWayland fields. These results
validate that tested backport, not the absence of bugs in other system builds.

完整对应源码曾内置于提交
[`60e2057`](https://github.com/GuoWQ222/uu-remote-for-linux/tree/60e205788c6190760889fd26cea3c0a8d9e9d9e8/third_party/mutter-46)
的归档中，保留在仓库历史供此前二进制使用者获取；当前通用 DEB 不携带该归档。
源码补丁及相关文件保留 **GPL-2.0-or-later**、上游逐文件声明和本目录 `COPYING`，
不改用包装项目的 0BSD 许可证。
