<h1 align="center">UU 远程 Linux 版</h1>

<p align="center">
  在 Ubuntu 上运行官方 UU 远程客户端，并提供原生输入、显示器、托盘、安全更新与硬件编解码集成。
</p>

<p align="center">
  <a href="https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest"><img src="https://img.shields.io/github/v/release/GuoWQ222/uu-remote-for-linux?style=flat-square&label=release" alt="最新版本"></a>
  <img src="https://img.shields.io/badge/Ubuntu-24.04-E95420?style=flat-square&logo=ubuntu&logoColor=white" alt="Ubuntu 24.04">
  <img src="https://img.shields.io/badge/Desktop-X11%20%7C%20Wayland-4A90E2?style=flat-square" alt="支持 X11 和 Wayland">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-0BSD-blue?style=flat-square" alt="0BSD 许可证"></a>
</p>

<p align="center"><a href="README.md">English</a> · <strong>简体中文</strong></p>

> [!IMPORTANT]
> 这是网易 UU 远程的非官方 Linux 兼容包装层。本仓库不分发网易专有的安装程序或客户端二进制文件。
> UU 与包装层管理的下载默认直连，不会把桌面代理设置复制到 Wine 客户端。

## ✨ 功能亮点

| | 能力 | 提供的功能 |
|---|---|---|
| 🖥️ | Ubuntu 桌面 | 独立 Wine 客户端、图形化首次设置，以及事务式官方客户端更新 |
| 🖱️ | 原生远控 | X11/XTest 或 Wayland Portal 输入、被控屏幕选择，以及本地与远端光标对齐 |
| 🎬 | 硬件编解码 | CPU/OpenH264 回退、实验性 NVIDIA NVDEC 解码和经验证的 NVENC H.264/HEVC 编码 |
| 🧭 | 原生托盘 | 显示主界面、选择被控屏幕或解码器，并排列单个或两个远控窗口 |
| 🛡️ | Linux 集成 | 自启动、防休眠、加密 DNS 容错、文件传输、安全更新与远程开机兼容桥 |

> [!NOTE]
> 远程开机兼容层会在 UU 更新后从官方 `GameViewerServer.exe` 的函数边界与 WOL 语义标记自动生成本机版本档案，再经原始字节和 SHA-256 双重校验后部署；无法唯一识别时不会修改二进制，并继续使用 Win32 网卡映射安全回退。

## 🚀 快速开始

1. 从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest) 下载最新 `.deb`。
2. 安装软件包：

   ```bash
   sudo apt install ./uu-remote-for-linux_1.1.26_amd64.deb
   ```

3. 从 Ubuntu 应用菜单打开 **UU 远程 Linux 版**，或运行：

   ```bash
   uu-remote-for-linux
   ```

首次启动时，接受网易官方《UU 远程软件许可及服务协议》即可自动安装独立的 Windows 客户端与 WebView2；拒绝协议不会安装客户端。

> [!TIP]
> 纯终端环境请先阅读[官方协议](https://uuyc.163.com/contact/20240402/40294_1146065.html)，再运行 `uu-remote-for-linux --accept-eula --setup-only`。

<details>
<summary><strong>从源码安装</strong></summary>

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

</details>

## ✅ 系统要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Ubuntu 24.04（已验证） |
| Wine | 11.1 或更高版本 |
| 桌面 | X11，或带 XWayland 与 XDG Desktop Portal 的 GNOME Wayland |
| NVIDIA 加速 | 专有驱动以及可用的 `libcuda.so.1`、`libnvidia-encode.so.1` |

启动器会自动识别桌面会话并探测可选的 NVIDIA 运行库。Wayland 首次作为被控端运行时，系统会请求屏幕共享与远程交互授权。

## 🧭 托盘速览

右键点击 Ubuntu 顶栏或系统托盘中的原生 UU 图标：

| 操作 | 用途 |
|---|---|
| **显示主界面** | 恢复已隐藏或最小化的 UU 窗口 |
| **被控屏幕** | 选择与 Windows 端当前视频轨道对应的 Ubuntu 显示器 |
| **远控窗口布局** | 在主屏保留单个窗口，或把已有远控窗口分布到两个显示器 |
| **选择解码器…** | 选择 CPU/NVIDIA 解码，并自动重启 UU 使其生效 |
| **退出** | 停止本项目独立运行的 Wine 客户端并移除托盘图标 |

显示器菜单会跟随热插拔、旋转与布局变化。双窗口选项只排列已有窗口，不会额外创建远控会话。

## 🎛️ 兼容性

### 视频

| 角色 | 后端 | 状态 | 说明 |
|---|---|---:|---|
| 解码 | CPU / OpenH264 | ✅ 已支持 | 上游路径最高 1080p/60 fps |
| 解码 | NVIDIA NVDEC | 🧪 实验性 | UU 菜单最高 4K/144 fps；须以实际串流为准 |
| 编码 | NVIDIA NVENC | ✅ 自动探测 | H.264、HEVC；验证通过后才会优先启用 |
| 编码 | CPU / OpenH264 | ✅ 安全回退 | H.264 |
| 编解码 | Intel/AMD 硬件 | ❌ 尚未实现 | 仍可使用 CPU 回退 |

### 桌面

| 会话 | 界面 | 被控端输入 | 画面采集 |
|---|---|---|---|
| X11 | Wine X11 驱动 | XTest | 现有 UU 采集路径 |
| Wayland | XWayland | XDG RemoteDesktop Portal | ScreenCast/PipeWire 共享帧 → Win64 GDI 钩子 |

> [!NOTE]
> Wayland 必须经过 Portal 授权。登录管理器与锁屏画面不属于已登录用户的 Portal 会话，因此需要登录前无人值守控制时，X11 更稳妥。

<details>
<summary><strong>常用命令</strong></summary>

```bash
# 启动、诊断、修复或停止
uu-remote-for-linux
uu-remote-for-linux --diagnose
uu-remote-for-linux --repair
uu-remote-for-linux --stop

# 更新官方客户端
uu-remote-for-linux --check-update

# 选择解码器
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders
uu-remote-for-linux --decoder auto   # 也可使用 cpu / nvidia:0

# Linux 被控端 NVENC 策略
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode

# 主控焦点处理不兼容时的应急回退
UU_REMOTE_DISABLE_FOCUS_STABILIZER=1 uu-remote-for-linux
```

</details>

## 📄 许可证与署名

本项目原创代码采用[零条款 BSD 许可证](LICENSE)。可选硬件编解码组件继续使用各自的 zlib 或 LGPL 许可证；准确的修订版本、源代码链接与校验和记录在 [NOTICE.md](NOTICE.md) 和 [third_party/HWDECODE.md](third_party/HWDECODE.md) 中。
