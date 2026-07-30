[English](README.md) | **简体中文**

# UU 远程（Linux 版本）

> [!IMPORTANT]
> 本项目是社区维护的非官方兼容层，与网易及网易 UU 远程没有隶属、认可或
> 合作关系。“网易”“UU”及相关图标和商标属于其各自权利人。

这是一个面向 Ubuntu/Debian x86_64 的非官方兼容层项目。它在独立 Wine
前缀中安装并启动网易官方 Windows 客户端，第一阶段只验证以下链路：

- Linux 登录 UU 远程并显示设备列表；
- Linux 主控 Windows/macOS；
- 远程画面、声音、键鼠、剪贴板和文件传输。

它不是网易发布的原生 Linux 客户端，也不承诺 Linux 被控、无人值守、
登录界面控制、隐私屏、虚拟显示器、HDR 或 4K/144Hz。

0.8.1 将项目与应用公开展示名称统一为中文“UU 远程（Linux 版本）”、
英文“UU Remote for Linux”，并让无桌面的 GitHub Runner 也能完整验证
托盘、ShellCheck 和 Debian 包构建。

0.8.0 修复 X11 下远控窗口收不到 `Win+Space` 的问题。GNOME 和 IBus
会在 Wine/UU 的全局键盘钩子之前占用该组合键；Linux 键盘桥现在识别
真正获得焦点的 `GameViewer.exe` 远控画面，只在聚焦期间临时移除
`Super+Space`、反向切换组合键和 Mutter 的单独 Super 覆盖键。失去焦点、
正常退出或下次从异常中恢复时，会按照恢复日志精确写回用户原有设置，
不会永久关闭 Linux 输入法切换，也不会影响其他 Wine 应用。

0.7.2 在 Linux 原生托盘菜单中加入“选择解码器…”：菜单会复用完整的
CPU、核显和独显能力探测窗口；用户取消时保持 UU 原运行状态，保存有效
选择后才停止当前独立 Wine 前缀，部署对应解码桥并自动重新启动 UU。
选择流程运行期间菜单项会暂时禁用，避免重复打开或触发并发重启。

0.7.1 将 Linux 桌面与原生托盘图标替换为官方 GameViewer.exe 中的
网易 UU 远程透明图标，保证顶栏小尺寸显示与官方客户端一致。

0.7.0 增加 Linux 原生托盘代理，从根本上绕开 Wine 传统 XEmbed
托盘的字体和首次弹出菜单焦点问题。代理通过 AppIndicator/
StatusNotifierItem 提供中文标题以及“显示主界面”“退出”菜单，并只
移除当前 UU 独立 Wine 前缀所属的 `explorer.exe` XEmbed 图标，不会
全局关闭 GNOME 的旧托盘支持，也不会处理其他 Wine 前缀。作为代理尚未
启动时的降级保护，Wine 的 `MS Shell Dlg` 字体回退也会改为
`Noto Sans CJK SC`，避免悬停提示出现中文缺字方框。

0.6.0 修复了“自动更新”与 Linux 解码/WOL 兼容桥的冲突。官方
`Upgrade.exe` 会直接覆盖安装目录，无法保证项目注入的 `d3d11.dll`、
`dxgi.dll`、`streamer.dll` 补丁和 WOL 服务端补丁仍然有效。本版本保存
原版更新器并放入无副作用的 Win64 占位程序，让 UU 设置页里的开关继续
作为用户意图来源；实际检查、下载和安装由 Linux 侧兼容桥执行。

自动更新采用版本兼容白名单和事务式回滚：只有官方安装包 SHA-256、
原版 `streamer.dll` 和服务端文件哈希都已收录的版本才会安装。更新前
完整快照当前客户端、注册表和设置；更新后重新部署所有已启用兼容桥并
逐项校验。任一安装或校验失败都会自动恢复旧版本。遇到尚未适配的官方
新版时只记录“已暂缓”，不会覆盖当前客户端或任何兼容 DLL。

0.5.1 进一步把 `GameViewerServer.exe` 和 `GameViewerHealthd.exe`
放进用户级 systemd 临时服务中托管。在 Wine 下，官方 Windows 服务
无法通过 Winlogon 会话的 `CreateProcessAsUser` 自动恢复这两个进程；
早期包装器的后台 Wine 进程也可能在启动器退出后丢失。现在服务端和
健康监视器拥有独立生命周期、失败自动重启，并由 `--stop` 或维护操作
显式停止。

0.5.0 增加了“允许远程开机”的 Linux WOL 识别桥。官方 Windows
服务端原本通过 PowerShell/WMI 查询 `S5WakeOnLan`、
`WakeOnMagicPacket` 和设备唤醒权限；Wine 无法枚举 Linux 物理网卡，
会把真实支持 WOL 的网卡误报为不支持。本版本先用 `ethtool`、
NetworkManager 和 PCI `power/wakeup` 核验并配置 Magic Packet，再对
UU 4.34.0.8979 的服务端能力判断应用 SHA-256 与原始字节双重锁定的
3 字节兼容补丁。原文件会保存在同目录，可用 `--disable-wol` 完整恢复。

这个桥只修复本机网卡配置和 UU 的能力识别。真正从公网唤醒还需要同一
局域网内受 UU 支持且始终在线的路由器或辅助设备；它不会绕过企业网络
策略，也不能代替 BIOS/UEFI 的 WOL 设置。项目仍以 Linux 主控端为范围：
冷启动 Linux 后，UU 只能在用户进入图形会话后由 XDG 自启动项运行；
因此“关机后唤醒并直接进入无人值守远控”仍不在当前支持范围。

0.4.0 修复了“防止电脑休眠”。Wine 目前不会把 Windows
`SetThreadExecutionState` 自动映射到 Linux 电源管理；本版本监视 UU
客户端为该调用记录的真实状态值，并在开关开启时通过
`systemd-logind` 持有 `sleep:idle` 阻止型 inhibitor。开关关闭、UU Wine
前缀停止或兼容桥退出时会释放 inhibitor。它会阻止系统自动休眠和
logind 的空闲动作，但不会永久修改 GNOME 的全局电源设置，也不需要
root 权限。

0.3.3 增加了 Linux 登录自启动兼容桥。UU 原界面的“开机自动启动”
仍然写入独立 Wine 前缀的 Windows `Run` 注册表；启动器会监视这一项，
并在开关开启时原子创建 XDG 自启动文件，关闭时只删除带有本项目管理
标记的文件。兼容桥在注册表文件发生变化时读取 Wine 的实时注册表状态，
不必等待 Wine 最长数十秒的延迟落盘；实时查询只在该 Wine 前缀的
服务器套接字存在时进行，因此执行 `--stop` 后不会重新拉起 Wine。同步
不需要 root 权限，通常在切换开关后 2 秒内完成。

0.3.0 增加了解码设备自动探测和图形选择窗口。选择器会列出 CPU、每块
NVIDIA CUDA/NVDEC 设备，以及 PCI 总线上检测到的 Intel/AMD/其他显示
设备，并显示接入后端、编码格式、最高画质、UU 可选最高帧率和能力状态。
NVIDIA 能力由当前驱动的 `cuvidGetDecoderCaps` 逐卡查询，不按显卡名称
猜测。选择 `auto` 时，每次启动优先使用可用的第一块 NVDEC 设备，否则
回退到 UU 的 CPU/OpenH264 路径。多 NVIDIA GPU 可通过 CUDA 序号显式
绑定；无效或已移除的设备会在启动前报错，不会静默改用另一块显卡。

当前可实际接入 UU 的后端是 CPU/OpenH264 和 NVIDIA NVDEC。Intel/AMD
核显或独显会被列出，但由于 Wine 版 UU 尚无 VA-API 桥，这些行会明确
显示“尚未实现”并拒绝保存，避免把“系统检测到”误报为“UU 可以使用”。
驱动能力查询能给出编码格式和最大分辨率，但硬件 API 不提供一个适用于
所有码流的绝对“最高 fps/解码率”；窗口因此区分驱动上限、UU 菜单上限
和需要实际码流基准验证的吞吐量。

0.2.0 增加了 NVIDIA 硬件解码桥。它让 UU 远程的 Windows
NVDEC 解码器调用 Linux NVIDIA 驱动，并通过 CPU 回拷把解码帧上传到
DXVK D3D11 纹理。它解决的是 Wine 缺少 CUDA/D3D11 互操作这一根因，
不是伪造硬件能力；同时对 UU 4.34.0.8979 的 `streamer.dll` 应用经过
SHA-256 和原始字节双重校验的 6 字节选择补丁，使父进程实际请求并记录
NVDEC（实现 33），而不是 Wine 下不可用的 DXVA11（实现 32）。当前仍
属于 NVIDIA 专用实验实现，不是零拷贝。

## 安全与边界

- 官方安装包只在首次设置时从网易官方发布接口下载，不包含在本仓库或
  生成的 `.deb` 中。
- 使用独立前缀：
  `${XDG_DATA_HOME:-$HOME/.local/share}/uuyc-linux-controller/wineprefix`。
- `--stop` 只停止这个独立前缀的 Wine 进程。
- `--enable-hwdecode` 不覆盖来源不明的 D3D11/CUDA DLL；
  `--disable-hwdecode` 只删除本项目记录并校验过的注入文件。
- UU 原生更新器会保存为 `Upgrade.uuyc-original.exe`，运行位置由本项目
  的无副作用占位程序保护；只有通过兼容档案验证的版本才进入更新事务。
- 自动更新前会创建回滚快照，安装完成后必须通过客户端版本、Event Log
  DLL、更新器保护、解码桥和 WOL 补丁校验，否则恢复旧版本。
- 启用时原版 `streamer.dll` 会改名保存在同目录，原解码能力缓存会保存
  在项目数据目录；关闭时二者原样恢复。遇到未知版本或字节不匹配会拒绝
  修改，官方更新产生的旧原件会先按 SHA-256 归档。
- 卸载软件包不会删除账号状态、Cookie 或 Wine 前缀。
- 首次安装前必须显式接受
  [网易 UU 远程软件许可及服务协议](https://uuyc.163.com/contact/20240402/40294_1146065.html)。

本项目使用了由
[ParticleG/uuyc-wine](https://github.com/ParticleG/uuyc-wine)
验证过的关键兼容设置，并针对 Ubuntu/Debian、动态下载和诊断流程进行了
重新封装。上游兼容方案与本项目均不受网易官方支持。

## 系统要求

- Ubuntu 24.04 或兼容的 Debian 系发行版；
- x86_64/amd64；
- X11，或 Wayland 下可用的 XWayland；
- Wine 11.1 或更高版本；
- `curl`、`ca-certificates`、`flock`、`sha256sum`。
- Python 3、PyGObject、GTK 3、AppIndicator3、`gsettings` 和 `libX11`
  （用于原生托盘及 X11 远控键盘桥）。
- `systemd-logind`、`systemd-inhibit` 和 `setsid`（用于防休眠桥）。
- `iproute2`、NetworkManager、`ethtool` 和 `pkexec`（用于 WOL 桥）。
- 图形选择窗口需要 `zenity`；PCI 显卡枚举建议安装 `pciutils`。

启用硬件解码桥还需要：

- NVIDIA 显卡和可工作的专有驱动；
- 主机可加载 `libcuda.so.1` 与 `libnvcuvid.so.1`；
- Vulkan 1.3 驱动（供内置 DXVK 3.0.2 使用）。

Ubuntu 24.04 自带仓库的 Wine 9.0 版本过低。请按照
[WineHQ 官方 Ubuntu 安装说明](https://gitlab.winehq.org/wine/wine/-/wikis/Debian-Ubuntu)
安装 Wine 11.1+。程序会拒绝已知过旧版本；仅用于排错时可设置
`UUYC_ALLOW_UNSUPPORTED_WINE=1` 绕过检查。

## 从源码运行

```bash
cd uu-remote-for-linux
./bin/uuyc-linux-controller --diagnose
./bin/uuyc-linux-controller --accept-eula --setup-only
./bin/uuyc-linux-controller
```

首次设置会下载约 90 MB 的官方安装包，并在 Wine 前缀中安装 WebView2。
整个过程可能持续数分钟。

常用命令：

```bash
# 启动客户端
uuyc-linux-controller

# 只执行安装/修复，不启动界面
uuyc-linux-controller --setup-only
uuyc-linux-controller --repair

# 下载官方最新版并据此更新前缀
uuyc-linux-controller --refresh-installer --setup-only

# 立即执行一次安全更新检查
uuyc-linux-controller --check-update

# 查看只读诊断信息
uuyc-linux-controller --diagnose

# 打开图形选择窗口，或在终端查看全部设备能力
uuyc-linux-controller --select-decoder
uuyc-linux-controller --list-decoders

# 直接选择自动策略、CPU，或第 N 块 NVIDIA CUDA 设备
uuyc-linux-controller --decoder auto
uuyc-linux-controller --decoder cpu
uuyc-linux-controller --decoder nvidia:0

# 旧版兼容命令：等价于选择 nvidia:0 / cpu
uuyc-linux-controller --enable-hwdecode
uuyc-linux-controller --disable-hwdecode

# 停止该独立 Wine 前缀
uuyc-linux-controller --stop
```

## 安全自动更新

正常启动 0.6.0 或更高版本后，继续使用 UU 设置页中的“自动更新”开关。
开关开启时，Linux 兼容桥会在启动后立即检查一次，随后默认每 6 小时
检查官方发布接口；关闭后不会发起自动检查。也可随时手动运行：

```bash
uuyc-linux-controller --check-update
uuyc-linux-controller --diagnose
```

诊断命令会显示“UU 自动更新开关”、“安全更新状态”、“自动更新兼容桥”
和“官方更新器保护”。日志与状态分别保存在：

```text
${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/update-bridge.log
${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/update-status
```

安全更新不是对任意未来版本的盲目兼容。发现新版本后，只有当前 Linux
软件包包含该版本的兼容档案且下载文件哈希完全匹配时才会安装；否则保留
现有版本并显示“已暂缓”。这是避免官方更新覆盖解码兼容 DLL、导致桥接
失败的核心安全边界。安装新版本的 Linux 软件包后可再次执行
`--check-update`。

如需打开 `uuremote:` 链接：

```bash
uuyc-linux-controller --uri 'uuremote:...'
```

## Linux 登录自启动

正常启动一次 0.3.3 或更高版本后，直接使用 UU 设置页里的
“开机自动启动”开关。兼容桥会同步这两个状态：

```text
Wine HKCU\Software\Microsoft\Windows\CurrentVersion\Run
  ↕
${XDG_CONFIG_HOME:-$HOME/.config}/autostart/uuyc-linux-controller.desktop
```

自启动项调用同一个 `uuyc-linux-controller --autostart` 入口，继续使用
独立 Wine 前缀和已选择的解码设备。诊断命令会分别显示“UU 自启动开关”、
“Linux 自启动项”和“自启动兼容桥”；同步日志保存在：

```text
${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/autostart-bridge.log
```

关闭 UU 界面开关后，兼容桥只删除包含
`X-UUYC-Autostart-Bridge=true` 标记的文件，不会删除用户自行创建的同名
自启动项。如果同名文件不受本项目管理，兼容桥会保留它并在日志中报告。

## 防止电脑休眠

正常启动 0.4.0 或更高版本后，继续使用 UU 设置页里的“防止电脑休眠”
开关。该开关开启时，诊断命令应显示：

```text
UU 防休眠开关          开启
Linux 休眠抑制         生效中
防休眠兼容桥           就绪
```

Linux 原生状态也可用以下命令核验：

```bash
systemd-inhibit --list --no-pager | grep uuyc-linux-controller
```

兼容桥只解析 UU 4.34.0.8979 自身写入客户端日志的
`HomePageMainWindow::preventSleep state:` 数值；最低位对应
`ES_SYSTEM_REQUIRED`。未发现状态、开关关闭或独立 Wine 前缀已停止时，
桥会采取保守策略并释放 inhibitor。同步日志保存在：

```text
${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/sleep-bridge.log
```

## 允许远程开机

先启用 Linux 原生 Magic Packet 和 UU 识别桥：

```bash
uuyc-linux-controller --enable-wol
uuyc-linux-controller --diagnose
uuyc-linux-controller
```

启用时会通过 `pkexec` 请求一次管理员权限，只用于核验网卡能力、把当前
NetworkManager 有线连接设为 `wake-on-lan=magic`、设置驱动 `wol g`，
以及启用 PCI 设备唤醒。它不会断开当前网络连接。诊断中应显示：

```text
Linux WOL 状态         Magic Packet 已配置
UU 后台进程托管       Server/Healthd 生效中
UU WOL 兼容桥          已启用
UU WOL 识别补丁        完整
```

重新启动 UU 后，再在原设置页打开“允许通过远程开机启动”，按网易引导
选择同一局域网中的受支持路由器或在线辅助设备。只有当网易云端返回
`support_wol:true` 后，诊断中的 `UU 远程开机开关` 才会显示“已开启”。
默认网关存在并不等于该路由器受 UU 支持。

需要撤销 UU 文件兼容补丁时：

```bash
uuyc-linux-controller --disable-wol
```

该命令恢复原版 `GameViewerServer.exe`，但保留 Linux Magic Packet
配置，避免破坏系统管理员原有的 WOL 设置。

## 用户级安装

```bash
./scripts/install-user.sh
~/.local/bin/uuyc-linux-controller --accept-eula --setup-only
```

安装位置：

```text
~/.local/bin/uuyc-linux-controller
~/.local/lib/uuyc-linux-controller/
~/.local/share/applications/uuyc-linux-controller.desktop
```

## 解码设备选择

先完成普通客户端设置，然后打开选择器：

```bash
uuyc-linux-controller --setup-only
uuyc-linux-controller --select-decoder
```

也可以右键桌面应用图标，选择“选择解码设备”。保存后关闭并重新启动
UU 远程，启动器会复查所选设备；选择 NVIDIA 时自动部署硬解桥并设置
`UUYC_CUDA_DEVICE`，选择 CPU 时自动完整撤销桥接文件和版本锁定补丁。
选择文件保存在：

```text
${XDG_DATA_HOME:-$HOME/.local/share}/uuyc-linux-controller/decoder-selection
```

当前 CPU 行的 1080p/60fps 是 UU 4.34.0.8979 的 OpenH264 路径限制；
NVIDIA 行显示当前驱动逐卡返回的编码与最大分辨率，同时将 UU 菜单的
4K/144fps 标为“需实测”。分辨率、位深、色度、码率和并发流会共同影响
实际帧率，因此选择器不会伪造一个驱动未提供的绝对吞吐数字。

## NVIDIA 硬件解码桥

先完成普通客户端设置，再启用桥：

```bash
uuyc-linux-controller --setup-only
uuyc-linux-controller --enable-hwdecode
uuyc-linux-controller --diagnose
uuyc-linux-controller
```

诊断输出中的 `NVIDIA 解码库`、`硬解桥运行库`、`硬解桥开关` 和
`硬解桥注入` 均应显示为就绪、完整或已启用。出现兼容问题时可以完整
回退：

```bash
uuyc-linux-controller --disable-hwdecode
```

桥接数据路径是：

```text
UU streamer 选择 NVDEC(33) → NVDEC → CUDA 显存
                                    → CPU 缓冲 → DXVK D3D11 纹理
```

因此视频解码本身由 NVDEC 完成，但每帧有一次显存到内存和一次内存到
显存的传输。这个版本优先解决功能正确性；真正的零拷贝仍需实现 Vulkan
外部内存与 CUDA external memory 的跨 API 互操作。

选择补丁仅支持原版 `streamer.dll` SHA-256
`2144cd9c199ee21ef55da984ef38d719e337a8202b26522458e56bff648ffb60`。
它把文件偏移 `0x94dc03` 的 `89 55 d0` 改为 `b2 21 90`，并把
`0x94e4ee` 的 `8b 45 d0` 改为 `6a 21 58`；补丁后 SHA-256 必须为
`abf2e482c65397138aa69430e3413a10f80d942a28817c22f8f8ff78e3c5ca70`。
项目不分发网易的 DLL，只在用户已经安装的受支持版本上原地生成补丁副本。

## 构建 Debian 包

生成的包不包含网易安装程序：

```bash
./packaging/build-deb.sh
sudo apt install ./dist/uuyc-linux-controller_0.8.1_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
```

## 验收清单

运行 `tests/manual-controller-checklist.md` 中的步骤，并记录：

1. 登录是否成功；
2. 是否能发现 Windows/macOS 设备；
3. 首帧、1080p 和音频是否正常；
4. 键盘、鼠标、滚轮、快捷键是否正常；
5. 剪贴板和文件传输是否正常；
6. 断线重连和多次启动是否稳定。

不要使用远程协助码、设备 ID、账号 Cookie 或完整客户端日志提交公开问题。

## 数据与日志

```text
Wine 前缀：${XDG_DATA_HOME:-$HOME/.local/share}/uuyc-linux-controller/wineprefix
安装包缓存：${XDG_CACHE_HOME:-$HOME/.cache}/uuyc-linux-controller/
包装层日志：${XDG_STATE_HOME:-$HOME/.local/state}/uuyc-linux-controller/
```

包装层日志会记录执行的命令和错误，不主动收集密码、验证码或设备列表。
网易客户端自身仍会按照其产品逻辑保存账号与运行数据。

## 开发

```bash
make test
make deb
```

如果安装了 `binutils-mingw-w64-x86-64` 和 `mingw-w64-x86-64-dev`，可复现
构建 Event Log 兼容 DLL：

```bash
make shim
```
