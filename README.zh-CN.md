[English](README.md) | **简体中文**

# UU远程（Linux测试版）

> [!IMPORTANT]
> 网易UU远程软件的非官方实现。

## 功能特性

- 图形化 GUI 界面。
- 可随网易UU Windows版本同步更新。
- 在 Ubuntu 24.04 中使用 UU 远程官方客户端登录并进行远程控制。
- CPU/OpenH264 解码，以及实验性的 NVIDIA NVDEC 解码。
- 原生 Linux 托盘菜单，支持选择解码器和自动重启。
- X11 会话下支持 Linux 作为被控端的原生鼠标、键盘和远端光标。
- 支持自启动、防休眠、安全更新、文件传输与远程开机兼容桥。

## 系统要求

- Ubuntu 24.04（已验证）
- Wine 11.1 或更高版本
- X11 会话（Linux 作为被控端时必需；Wayland 原生输入尚未支持）

## 从 Release 安装（推荐）

从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest)
下载最新的 `.deb`，然后运行：

```bash
sudo apt install ./uu-remote-for-linux_1.0.0_amd64.deb
uu-remote-for-linux --accept-eula --setup-only
```

安装 1.0.0 时会替换旧的软件包标识。首次启动将把现有 Wine 前缀、登录
状态、设置、缓存、日志和自启动项迁移到新的 `uu-remote-for-linux`
路径，不需要重新登录。

### 从源码安装（可选）

```bash
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uu-remote-for-linux --accept-eula --setup-only
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
```

## Linux 被控端输入

0.9.0 起，启动器会在 `GameViewerServer.exe` 的 `SendInput` 回退路径上
加载项目自建的 Win64 钩子，将已认证的键鼠事件交给本地 X11/XTest
守护进程。它不尝试加载网易的 Windows 内核 HID 驱动，也不需要
`/dev/uinput` 或 root 权限。

运行 `uu-remote-for-linux --diagnose`，确认：

- `被控端原生输入` 为“生效中”；
- 移动或点击时，`被控端输入事件` 的计数持续增加；
- `Win64 输入钩子` 为“完整”。

0.9.1 还会把 X11/XTest 实际采用的鼠标坐标反向同步给 Wine 的
`GetCursorInfo` 和 `GetCursorPos`。因此 UU 的 GDI 捕获路径上报给主控端
的光标元数据，会与 Linux 桌面上实际移动的指针保持一致。

0.9.2 进一步把钩子安装到 UU 单独加载的 `streamer.dll` 捕获模块，并
提供可由 `GetIconInfo` 正常读取位图的稳定 Win32 箭头光标。这可以避免
Windows 主控端隐藏本地鼠标后，却收不到可用的远程光标形状。

该功能当前仅支持 X11。Wayland、登录管理器/锁屏界面、游戏手柄和多点
触控尚未支持。

## 解码器支持

| 设备/后端 | 检测方式 | UU 集成状态 | 标称上限 |
|---|---:|---:|---|
| CPU / OpenH264 | 是 | 已支持 | 上游路径：1080p/60 fps |
| NVIDIA NVDEC | 逐 GPU 查询驱动 | 实验性支持 | UU 菜单最高 4K/144 fps；须以实际串流测试为准 |
| Intel/AMD VA-API | PCI 设备发现 | 尚未实现 | 不可用 |

## 许可证与署名

本项目的原创代码采用零条款 BSD 许可证
（[LICENSE](LICENSE)）。部分兼容思路和 Event Log 兼容层改编自
`ParticleG/uuyc-wine`；随附的可选解码组件继续使用各自的 zlib 和 LGPL
许可证。准确的修订版本、对应源代码和校验和记录在
[NOTICE.md](NOTICE.md) 与
[third_party/HWDECODE.md](third_party/HWDECODE.md) 中。

本仓库不包含网易专有的安装程序或客户端二进制文件。官方应用程序图标
仅用于产品识别；其美术作品以及所有网易/UU 标志仍属于网易所有。
