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
> 这是非官方 Linux 兼容包装层，不分发网易专有安装程序或客户端二进制文件。

## 🗺️ 目录

- [✨ 功能亮点](#highlights)
- [🚀 安装](#install)
- [🧭 日常使用](#daily-use)
- [🎛️ 兼容性](#compatibility)
- [🧰 常用命令](#commands)
- [📄 许可证](#license)

<a id="highlights"></a>
## ✨ 功能亮点

| | 能力 | 提供的功能 |
|---|---|---|
| 🖥️ | 官方客户端 | 基于 Wine 环境搭建、图形化界面、自动更新 |
| 🖱️ | 原生远控 | 支持 X11/Wayland，支持双显示器和主控显示器切换 |
| 🎬 | 硬件编解码 | 支持 CPU/OpenH264、NVIDIA NVDEC 与 NVENC H.264/HEVC |
| 🧭 | Linux 桌面 | 原生托盘以及 UU 远程的所有设置功能 |

> [!NOTE]
> 版本相关的编解码、竖屏原画与 WOL 改动仅在唯一语义指纹、哈希和运行验证通过后部署。对高于已验证 4.38 基线的新版本，若任一已启用的兼容功能无法自动重建，更新会被拒绝并回滚到上一个完整版本；旧版本仍保留文档中的安全回退。

<a id="install"></a>
## 🚀 安装

**已验证环境：** Ubuntu 24.04 x86_64 · Wine 11.1+ · X11 或 GNOME Wayland

1. 从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest) 下载最新 `.deb`。
2. 安装并启动：

   ```bash
   sudo apt install ./uu-remote-for-linux_1.1.29_amd64.deb
   uu-remote-for-linux
   ```

首次启动时，接受网易[官方协议](https://uuyc.163.com/contact/20240402/40294_1146065.html)即可安装独立客户端与 WebView2；拒绝后不会安装客户端。

> [!TIP]
> 纯终端安装：`uu-remote-for-linux --accept-eula --setup-only`

<details>
<summary><strong>🧱 从源码安装</strong></summary>

```bash
sudo apt install python3-gi zenity gir1.2-gstreamer-1.0 \
  gstreamer1.0-pipewire gstreamer1.0-plugins-base
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux
```

</details>

<a id="daily-use"></a>
## 🧭 日常使用

右键点击原生托盘图标：

| 操作 | 用途 |
|---|---|
| **显示主界面** | 恢复隐藏或最小化的 UU 窗口 |
| **被控屏幕** | 选择与 Windows 视频轨道对应的 Ubuntu 显示器 |
| **远控窗口布局** | 默认由用户手动移动；也可在主屏保留单窗口，或排列两个已有窗口 |
| **选择解码器** | 选择 CPU/NVIDIA 解码并重启 UU |
| **退出** | 停止独立 Wine 客户端 |

被控屏幕选择会自动同步热插拔、旋转和布局变化；只有主动选择自动布局后，远控窗口才会被自动移动。

<a id="compatibility"></a>
## 🎛️ 兼容性

| 范围 | 后端 | 状态 | 说明 |
|---|---|---:|---|
| 桌面 | X11 + XTest | ✅ | 原生输入与现有 UU 采集路径 |
| 桌面 | GNOME Wayland Portal | ✅ | XWayland 界面、Portal 输入与 PipeWire 采集 |
| 解码 | CPU / OpenH264 | ✅ | 安全回退，最高 1080p/60 fps |
| 解码 | NVIDIA NVDEC | 🧪 | 实验性；UU 菜单最高 4K/144 fps |
| 编码 | NVIDIA NVENC | ✅ | H.264/HEVC，验证通过后启用 |
| Intel/AMD 硬件 | — | ❌ | 仍可使用 CPU 回退 |

> [!WARNING]
> Wayland 必须授权 Portal。多屏环境首次弹出共享对话框时请选择全部显示器，后续会复用该授权。登录管理器和锁屏不属于已登录用户的 Portal 会话；登录前无人值守控制请使用 X11。

<a id="commands"></a>
## 🧰 常用命令

<details>
<summary><strong>展开命令列表</strong></summary>

```bash
# 启动与维护
uu-remote-for-linux
uu-remote-for-linux --diagnose
uu-remote-for-linux --repair
uu-remote-for-linux --stop
uu-remote-for-linux --check-update

# 解码器
uu-remote-for-linux --select-decoder
uu-remote-for-linux --list-decoders
uu-remote-for-linux --decoder auto   # cpu / nvidia:0

# Linux 被控端编码
uu-remote-for-linux --enable-hwencode
uu-remote-for-linux --disable-hwencode
```

</details>

<a id="license"></a>
## 📄 许可证

本项目原创代码采用[零条款 BSD 许可证](LICENSE)。可选组件保留各自许可证；修订版本、源码链接与校验和见 [NOTICE.md](NOTICE.md) 和 [third_party/HWDECODE.md](third_party/HWDECODE.md)。
