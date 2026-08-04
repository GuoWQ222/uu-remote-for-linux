[English](README.md) | **简体中文**

# UU远程（Linux测试版）

> [!IMPORTANT]
> 网易UU远程软件的非官方实现。

## 功能特性

- 图形化 GUI 界面，支持 Ubuntu 24.04、X11/Wayland。
- 可随网易UU Windows版本同步更新。
- 在 Ubuntu 24.04 中使用 UU 远程官方客户端登录并进行远程控制。
- CPU/OpenH264 解码，以及实验性的 NVIDIA NVDEC 解码。
- 原生 Linux 托盘菜单，支持选择解码器和自动重启。
- Linux 作为被控端时支持原生鼠标、键盘和远端光标：X11 使用 XTest，
  Wayland 使用 RemoteDesktop Portal。
- 支持自启动、防休眠、安全更新、Linux 系统代理、文件传输与远程开机兼容桥。

## 系统要求

- Ubuntu 24.04（已验证）
- Wine 11.1 或更高版本
- X11，或带 XWayland 与 XDG Desktop Portal 的 GNOME Wayland。启动器会
  自动识别当前会话。Wayland 首次作为被控端启动时，系统会要求用户授权
  屏幕共享和远程交互。

## 从 Release 安装（推荐）

从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest)
下载最新的 `.deb`，然后运行：

```bash
sudo apt install ./uu-remote-for-linux_1.1.14_amd64.deb
```

安装完成后，从 Ubuntu 应用菜单点击“UU 远程（Linux 版本）”，或运行
`uu-remote-for-linux`。首次启动会显示网易官方《UU 远程软件许可及服务协议》；
选择“接受并继续”后，程序会自动安装独立的 Windows 客户端和 WebView2，随后
直接打开 UU 远程。选择“拒绝并退出”不会写入接受状态，也不会安装或启动客户端。

纯终端环境下，请先阅读[官方协议](https://uuyc.163.com/contact/20240402/40294_1146065.html)，
再显式运行 `uu-remote-for-linux --accept-eula --setup-only`。

### 从源码安装（可选）

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

## 使用方法（命令行）

```bash
# 启动 UU 远程
uu-remote-for-linux

# 安装或修复独立客户端
uu-remote-for-linux --setup-only
uu-remote-for-linux --repair

# 在不做任何更改的情况下检查安装状态
uu-remote-for-linux --diagnose

# 执行一次受兼容性限制的安全更新检查
uu-remote-for-linux --check-update

# 交互式选择解码器，或列出检测到的设备
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders

# 明确选择解码策略
uu-remote-for-linux --decoder auto
uu-remote-for-linux --decoder cpu
uu-remote-for-linux --decoder nvidia:0

# 仅停止本项目的独立 Wine 前缀
uu-remote-for-linux --stop

# 主控焦点处理不兼容时的应急回退
UU_REMOTE_DISABLE_FOCUS_STABILIZER=1 uu-remote-for-linux
```

## 解码器支持

| 设备/后端 | UU 集成状态 | 标称上限 |
|---|---:|---|
| CPU / OpenH264 | 已支持 | 上游路径：1080p/60 fps |
| NVIDIA NVDEC | 实验性支持 | UU 菜单最高 4K/144 fps；须以实际串流测试为准 |
| Intel/AMD VA-API | 尚未实现 | 不可用 |

## 桌面后端

| 会话 | Wine 界面 | 被控端输入 | 原生桌面采集 |
|---|---|---|---|
| X11 | Wine X11 驱动 | XTest | 现有 UU 路径已支持 |
| Wayland | XWayland | XDG RemoteDesktop Portal | ScreenCast/PipeWire → 共享帧 → 进程内 Win64 GDI 钩子 |

在 Wayland 下，完成系统授权后，`uu-remote-for-linux --diagnose` 会显示
`wayland-xwayland`、`wayland-portal`，并显示 `Wayland 画面桥` 已生效。
Portal 授权是 Wayland 的安全边界，应用不能绕过。画面桥请求不内嵌鼠标的
显示器画面，指针仍由 UU 的同步远端光标绘制。登录管理器和锁屏画面不属于
已登录用户的 Portal 会话，因此需要登录前无人值守控制时，X11 仍更稳妥。

## 主控端中文输入

远控窗口只传输物理键盘事件，不会把 Linux IBus/Fcitx 的本地候选词提交
当作远端按键。连接其他设备后，请保持远控窗口聚焦，使用
`Super+Space`（键盘上的 Win+Space）切换**远端设备**的中文输入法，再
输入拼音。1.1.3 起，启动器还会为 `gameviewer.exe` 设置 Wine 官方的
`UseXIM=N` 应用级选项，彻底阻止 UU 主控窗口连接本机 IBus；键盘桥则在
聚焦期间固定 XKB 并处理 Wine 辅助窗口的焦点抖动。切出远控窗口约
0.75 秒后，Linux 原输入源会自动恢复。

## 许可证与署名

本项目的原创代码采用零条款 BSD 许可证
（[LICENSE](LICENSE)）。部分兼容思路和 Event Log 兼容层改编自
`ParticleG/uuyc-wine`；随附的可选解码组件继续使用各自的 zlib 和 LGPL
许可证。准确的修订版本、对应源代码和校验和记录在
[NOTICE.md](NOTICE.md) 与
[third_party/HWDECODE.md](third_party/HWDECODE.md) 中。

本仓库不包含网易专有的安装程序或客户端二进制文件。官方应用程序图标
仅用于产品识别；其美术作品以及所有网易/UU 标志仍属于网易所有。
