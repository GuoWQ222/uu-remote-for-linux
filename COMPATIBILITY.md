# UU 版本升级与补丁兼容规范

本文说明本项目对官方 UU Windows 客户端、Wine 和 GNOME Wayland
所做改动的版本边界。这里的“自动兼容”不是在未知二进制中盲目搜索并改写若干
字节，而是：从新版本重新识别唯一语义、验证完整文件哈希、执行真实探测，任一
证明缺失就按功能风险处理：输入/焦点/画面等核心契约失败会拒绝更新并恢复完整
快照；NVDEC、NVENC 与 WOL 二进制增强失败则保留 CPU/OpenH264/通用 Win32
网卡映射，不把可选加速失败扩大成客户端不可用。

## 兼容等级

| 等级 | 定义 | 升级行为 |
|---|---|---|
| A：协议/导入语义 | 只依赖稳定 API、配置格式或唯一导入名称 | 自动发现并生成新档案；运行时仍验证 |
| B：二进制语义 | 需要识别编译后函数、调用关系或机器指令字段 | 只接受唯一候选、完整边界和深层探测同时通过 |

## 当前补丁矩阵

| 功能 | 等级 | 当前自适应机制 | 新 UU 版本的处理 |
|---|---:|---|---|
| 输入、锁键、焦点、画面与光标 Hook | A+B | `uu-remote-runtime-profiler` 解析 PE32+ 导入表，按 API 名验证 USER32、GDI、DXGI、IPHLPAPI，并自动发现唯一 `Qt*Core.dll`；运行时 Hook 再按唯一 IAT/vtable/语义候选安装 | 导入契约和运行时唯一候选都成立才启用；否则更新事务失败并回滚 |
| NVDEC 探测兼容 | B | `uu-remote-hwdecode-profiler` 结合 `.pdata` 与链式展开边界、日志语义、RIP 引用和局部数据流生成哈希绑定档案；旧布局派生两处补丁，新布局必须证明已有唯一的 NVDEC 专用调用链并保持官方 DLL 原样；DXVK 对 UU 暴露真实 NVIDIA 标识 | 不修改新版共用的编码/解码探测函数；候选不唯一、越界或真实解码失败时保留 CPU 解码 |
| NVENC、HEVC 与竖屏原画 | B | `uu-remote-encoder-policy` 验证实际编码器编号、vtable 和队列调用语义；区分旧整数 LUID 与新 GPUDevice 指针接口，新接口保持官方指针方法不变；四处竖屏操作独立验证，在暂停其他线程并核对 RIP 后修改字段；D3D11→CUDA 真实纹理上传作为最终门禁 | 禁止把旧整数补丁写入新指针接口；语义和真实 GPU 探测通过才部署，不能证明时保留 OpenH264 |
| WOL 能力与网卡映射 | A+B | `uu-remote-wol-profiler` 用源码路径、状态字符串或内联 Enabled/Disabled 比较、网卡能力日志、函数边界与布尔返回语义定位唯一谓词；物理网卡信息由 Linux 侧动态回填 | 不把带输出参数的电源配置查询误认为能力谓词；唯一匹配并核验 SHA256 后生成，失败时保留通用 Win32 映射 |
| 官方更新器接管 | A | 项目管理的更新入口以原始文件哈希和清单核验；下载只允许 HTTPS 重定向，并验证系统 CA 链、Authenticode 和网易发布者身份；Linux 侧拥有快照与回滚事务 | 不依赖版本哈希白名单；新安装后从实际文件重建语义档案与清单，核心契约或完整性不符即回滚 |
| Event Log、系统代理、自启动、防休眠、键盘快捷键 | A | Wine API shim、注册表/配置文件和 Linux 桌面 API 桥；不依赖官方代码 RVA | 格式仍可解析时自动工作；未知或超时均失败闭合并恢复桌面状态 |
| Wayland Portal 输入与 PipeWire 画面 | A | Portal、PipeWire、XKB 和共享帧协议；原生 helper 只协商三槽 DMA-BUF、EGL 读入独立 staging 后立即归还 producer；GNOME 的 Caps/Num 从 Mutter 原生状态读取，Scroll 保留 XKB 来源 | 不依赖官方 UU 或合成器内部版本号；原生锁键查询失败时停止发布权威锁键状态并重试；DMA-BUF/EGL 不可用时明确失败，不切换到语义不同的隐藏回退 |

<a id="wayland-lock-state-and-rime"></a>
## Wayland lock state and Rime / Wayland 锁键状态与 Rime

On GNOME Wayland, XWayland indicators can remain stale while a native
Wayland window has keyboard focus. The input bridge reads Caps Lock and
Num Lock from Mutter's `RemoteDesktop.Session` properties instead. Its
private, unstarted observer session injects no input and requests no screen
capture; closing its bus connection releases it without closing the Portal
session. Native query failures publish an unknown lock state and retry,
rather than using contradictory XWayland state. Scroll Lock retains XKB;
when Mutter is absent, other compositors retain the existing XKB path.

GNOME Wayland 中，原生 Wayland 窗口聚焦时，XWayland 指示器可能保留旧状态。
输入桥因此从 Mutter 会话属性读取 Caps Lock 和 Num Lock；查询失败时将状态
标记为未知并重试，避免用旧值触发额外校正按键。这项修改不自动更改输入法配置。

The remaining inversion was reproduced with a separate temporary IBus
engine, even with the prior Rime fix installed. Mutter 46 could maintain
different native-seat and Wayland keyboard states when an IME consumed lock
events; correct per-event modifier ordering is required for XWayland too.
This is not resolved by reading the native state or changing Rime bindings.
The [optional Mutter 46 backport](https://github.com/GuoWQ222/uu-remote-for-linux/blob/v1.1.43/contrib/mutter-46-lock-state/README.md)
contains the complete patch, exact supported Ubuntu version, independent
reproducer, pressure tests, package/source downloads and rollback procedure.
The universal UU package does not install a replacement compositor.

复发问题已用独立 IBus 测试引擎复现，不能归结为 Rime 单独的缺陷。
Mutter 46 的重复状态维护和事件顺序才是本次根因；原生状态读取与 Rime 按键
配置不能代替该修复。1.1.43 提供严格限定 Ubuntu/Mutter 版本的独立系统包及
完整源码。既有 [ibus-rime #171](https://github.com/rime/ibus-rime/issues/171)
和 [librime #1011](https://github.com/rime/librime/pull/1011) 不能单独证明
合成器已修复；发行版也可能回移补丁，不能只看主版本号判断。

若希望左 Shift 切换中英文、Caps Lock 只控制大小写，可把以下项目合并到
`~/.config/ibus/rime/default.custom.yaml` 的已有 `patch:` 中，然后通过 Rime
菜单重新部署。请保留已有自定义项，不要覆盖整个文件：

```yaml
patch:
  "ascii_composer/switch_key/Caps_Lock": noop
  "ascii_composer/switch_key/Shift_L": commit_code
```

For left Shift to switch Chinese/English and Caps Lock to control letter
case only, merge the overrides above into the existing Rime customization
and redeploy. This is an explicit user configuration, not an installation
default. Removing these two overrides and redeploying restores the prior
bindings. Logging out/in may clear an existing mismatch temporarily, but
does not repair the compositor bug; use the matching root-cause backport
when the documented platform and reproduction conditions apply.

验收时必须看实际输入：远端英文模式下依次关闭、开启、关闭 Caps Lock，应得到
`abc`、`ABC`、`abc`，并核对控制端指示状态；再测试左 Shift 切换中文候选与直接
英文输入，以及断开后重新连接。接口状态一致不能代替应用实际输出的验证。

## 自动升级事务

官方 UU 发现新版本时，启动器按以下顺序执行：

1. 在下载阶段验证 HTTPS 重定向、Authenticode 证书链和网易发布者身份，然后对
   客户端目录、注册表和所有生成档案建立原子回滚快照。
2. 停止专用 Wine 前缀，恢复必须从官方原件分析的补丁文件。
3. 安装官方新版本，但尚不把它认定为可运行版本。
4. 重新派生 NVDEC、NVENC/竖屏、WOL 和运行时导入契约；所有候选必须唯一且在
   PE 节、函数和文件边界内。
5. 应用策略后再次按最终文件字节生成 SHA256 绑定档案，避免接受补丁前的旧哈希。
6. 运行解码、编码纹理、Wine Hook 和文件完整性探测。核心 Hook/导入契约失败则
   回滚；可选编解码增强失败则移除项目管理的注入文件并提交安全回退。

档案写入采用同目录临时文件、`fsync` 和原子替换。运行时档案同时绑定 UU 版本、
文件大小和 SHA256；手工替换任一二进制都会使档案失效并触发重新验证。

项目不再分发任何“UU 版本号 → RVA/原始字节/安装包哈希”的 TSV 表。每个本地档案
都必须由当前官方二进制生成。升级自旧包时，如果客户端正处于历史补丁态，启动器
只会从哈希可验证的原版备份重新推导档案，再执行恢复或迁移；不会继续引用旧 RVA。

## 为什么不自动生成任意机器码补丁

闭源程序升级可能删除功能、改变对象布局、内联函数或重新定义调用约定。此时任何
“在附近找到相似字节就修改”的实现都可能破坏栈、vtable 或其他线程正在执行的
指令。因此项目只自动完成可证明的语义迁移；无法证明时自动回滚才是后向兼容的
安全行为。需要新增语义规则时，应把它加入 profiler、唯一性断言和真实探测，而
不是新增版本号分支或裸 RVA。

## Installer publisher verification

Official UU installer downloads, refreshes and update transactions use the
same Authenticode publisher gate. Compatibility follows the verifier output
format rather than an Ubuntu release, osslsigncode version or UU installer
version. Legacy slash-delimited subjects and RFC2253 subjects are accepted,
including escaped comma/plus/slash/backslash, hexadecimal UTF-8 values and
multi-valued RDNs. Organization attributes must match the expected NetEase
publisher exactly and occur once; a name in CN, OU or a certificate-chain
entry is insufficient. Malformed, ambiguous and unknown formats fail closed.
The verifier must still return success with the system CA bundle and its
normal certificate, signature and revocation checks. Verification explicitly
selects primary signature index 0, binding the verdict to the same signer
whose subject is inspected rather than an unrelated nested signature. No ignore flags,
certificate fingerprint pin, tool downgrade or global TLS exception is used.

This fixes the wrapper's publisher parser, not unrelated failures in other
Linux packages. The embedded WebView2 setup is taken from the verified UU
installer and does not independently use this subject parser. Future issuer
or signing-key changes retain compatibility while the official organization
identity and supported output format remain the same; a changed publisher
identity or a new output grammar requires review instead of automatic trust.


## 新版本验收要求

- profiler 的恶意/截断 PE 边界测试和新版本样本推导测试；
- 更新事务的成功、语义缺失、深层探测失败和完整回滚测试；
- Wine 中输入、Caps/Num/Scroll、焦点、光标、横竖屏和编码的真实 Hook 测试；
- Wayland 上 Portal 重连、DMA-BUF 三缓冲、多屏、拼音提交与无旧新帧 ABA 测试。

没有以上证据时，新版本只能使用不依赖该补丁的安全回退路径，不能标记为兼容。

`tests/native-nvdec-profile.sh` 验证原生 NVDEC 档案的无修改部署、关闭桥接、
篡改拒绝以及展开链边界。`tests/current-streamer.sh` 可通过
`UU_REMOTE_TEST_OFFICIAL_INSTALL_DIR` 指向本机官方安装目录，在独立 Wine
前缀内验证官方 H.264/HEVC 编解码、纹理上传和真实 DLL 的八个竖屏字段；
同时执行实际缩放指令的横竖屏回归，确认 GPUDevice 方法未被整数覆盖。
官方样本只复制到临时目录，不纳入仓库。此测试不执行关机、休眠或网卡唤醒，
也不替代另一台设备发起的完整远控会话验收。

## Optional X11 display synchronization

On the tested Wine 11.19 / NVIDIA virtual-output setup, a missing Wine
desktop owner left capture geometry stale after native display changes.
The opt-in companion in [contrib/x11-display-sync](contrib/x11-display-sync/README.md)
refreshes the isolated prefix from live X11 geometry and monitor layouts.
It does not set display modes or restart UU. The Wine internal notification
ABI is version-guarded, and its NVIDIA Vulkan-query workaround affects only
the short-lived helper child. See that directory for exact prerequisites,
installation, rollback, reproduction evidence and validation boundaries.
This component is not enabled or installed by the Debian package.
