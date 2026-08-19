[English](README.md) | **简体中文**

# UU远程（Linux测试版）

> [!IMPORTANT]
> 网易UU远程软件的非官方实现。

## 目录

- [功能特性](#功能特性)
- [系统要求](#系统要求)
- [从 Release 安装（推荐）](#从-release-安装推荐)
  - [从源码安装（可选）](#从源码安装可选)
- [托盘图标（右键菜单）](#托盘图标右键菜单)
  - [使用方法（命令行）](#使用方法命令行)
- [解码器支持](#解码器支持)
- [被控端编码器支持](#被控端编码器支持)
- [桌面后端](#桌面后端)
- [许可证与署名](#许可证与署名)

## 功能特性

- 图形化 GUI 界面，支持 Ubuntu 24.04、X11/Wayland。
- 每次启动及运行期间定期检查网易官方更新，并以事务方式安装最新版本。
- 在 Ubuntu 24.04 中使用 UU 远程官方客户端登录并进行远程控制。
- CPU/OpenH264 解码、实验性的 NVIDIA NVDEC 解码，以及 Linux 被控端的
  NVIDIA NVENC H.264/HEVC 硬件编码。
- 原生 Linux 托盘菜单，支持选择被控屏幕、选择解码器、自动重启，以及远控
  窗口单主屏或双屏分布布局。
- Linux 作为被控端时支持原生鼠标、键盘和远端光标：X11 使用 XTest，
  Wayland 使用 RemoteDesktop Portal。
- 支持自启动、防休眠、安全更新、Linux 系统代理、文件传输与远程开机兼容桥。

## 系统要求

- Ubuntu 24.04（已验证）
- Wine 11.1 或更高版本
- 使用 NVENC/NVDEC 时，需要 NVIDIA 专有驱动以及可用的
  `libcuda.so.1`、`libnvidia-encode.so.1`；启动器会自动探测和验证。
- X11，或带 XWayland 与 XDG Desktop Portal 的 GNOME Wayland。启动器会
  自动识别当前会话。Wayland 首次作为被控端启动时，系统会要求用户授权
  屏幕共享和远程交互。

## 从 Release 安装（推荐）

从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest)
下载最新的 `.deb`，然后运行：

```bash
sudo apt install ./uu-remote-for-linux_1.1.26_amd64.deb
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

## 托盘图标（右键菜单）

UU 远程运行时，右键点击 Ubuntu 顶栏或系统托盘中的原生 UU 图标，可以使用：

- **显示主界面**：重新显示已隐藏或最小化的 UU 主界面。
- **被控屏幕**：选择本机作为被控端时接收绝对鼠标坐标的 Ubuntu 显示器。
  默认使用主显示器；菜单会按连接器与分辨率列出任意数量的显示器，并跟随
  热插拔、旋转与布局变化。Windows 端切换 UU 视频屏幕后，应在这里选择同一
  块显示器。
- **远控窗口布局**：选择并记住现有远控画面窗口的排列方式：
  - **单窗口（主屏）**：把第一个远控窗口放入主显示器工作区，也是默认布局。
  - **双窗口（每屏一个）**：把已有的多个远控窗口按每个显示器最多一个进行
    排列；该选项不会额外创建远控会话或远控窗口。
- **选择解码器…**：打开解码器与设备选择窗口；保存新选择后会自动重启 UU，
  使设置生效。
- **退出**：停止本项目独立运行的 UU Wine 客户端，并移除原生托盘图标。

### 使用方法（命令行）

```bash
# 启动 UU 远程
uu-remote-for-linux

# 安装或修复独立客户端
uu-remote-for-linux --setup-only
uu-remote-for-linux --repair

# 在不做任何更改的情况下检查安装状态
uu-remote-for-linux --diagnose

# 立即检查并安装网易官方最新版本
uu-remote-for-linux --check-update

# 交互式选择解码器，或列出检测到的设备
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders

# 明确选择解码策略
uu-remote-for-linux --decoder auto
uu-remote-for-linux --decoder cpu
uu-remote-for-linux --decoder nvidia:0

# 手动启用或关闭 Linux 被控端的 NVENC 自动策略
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode

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

## 被控端编码器支持

| 设备/后端 | UU 集成状态 | 编码格式 |
|---|---:|---|
| NVIDIA NVENC | 自动探测并优先启用 | H.264、HEVC |
| CPU / OpenH264 | NVENC 验证失败时的安全回退 | H.264 |
| Intel/AMD 硬件编码 | 尚未实现 | 不可用 |

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

## 许可证与署名

本项目的原创代码采用零条款 BSD 许可证
（[LICENSE](LICENSE)）。随附的可选硬件编解码组件继续使用各自的 zlib 和 LGPL
许可证。准确的修订版本、对应源代码和校验和记录在
[NOTICE.md](NOTICE.md) 与
[third_party/HWDECODE.md](third_party/HWDECODE.md) 中。

本仓库不包含网易专有的安装程序或客户端二进制文件。官方应用程序图标
仅用于产品识别；其美术作品以及所有网易/UU 标志仍属于网易所有。
