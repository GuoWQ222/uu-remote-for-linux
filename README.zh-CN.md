[English](README.md) | **简体中文**

# UU远程（Linux测试版）

> [!IMPORTANT]
> 网易UU远程软件的非官方实现。

## 功能特性

- 图形化 GUI 界面。
- 在 Ubuntu 24.04 中使用 UU 远程官方客户端登录并进行远程控制。
- CPU/OpenH264 解码，以及实验性的 NVIDIA NVDEC 解码。
- 原生 Linux 托盘菜单，支持选择解码器和自动重启。
- 基本支持 Windows UU 远程软件的所有功能。

## 系统要求

- Ubuntu 24.04（已验证）
- Wine 11.1 或更高版本

## 从 Release 安装（推荐）

从 [GitHub Releases](https://github.com/GuoWQ222/uu-remote-for-linux/releases/latest)
下载最新的 `.deb`，然后运行：

```bash
sudo apt install ./uuyc-linux-controller_0.8.1_amd64.deb
uuyc-linux-controller --accept-eula --setup-only
```

### 从源码安装（可选）

```bash
git clone https://github.com/GuoWQ222/uu-remote-for-linux.git
cd uu-remote-for-linux
./scripts/install-user.sh
~/.local/bin/uuyc-linux-controller --accept-eula --setup-only
```

## 使用方法（命令行）

```bash
# 启动 UU 远程
uuyc-linux-controller

# 安装或修复独立客户端
uuyc-linux-controller --setup-only
uuyc-linux-controller --repair

# 在不做任何更改的情况下检查安装状态
uuyc-linux-controller --diagnose

# 执行一次受兼容性限制的安全更新检查
uuyc-linux-controller --check-update

# 交互式选择解码器，或列出检测到的设备
uuyc-linux-controller --select-decoder
uuyc-linux-controller --list-decoders

# 明确选择解码策略
uuyc-linux-controller --decoder auto
uuyc-linux-controller --decoder cpu
uuyc-linux-controller --decoder nvidia:0

# 仅停止本项目的独立 Wine 前缀
uuyc-linux-controller --stop
```

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
