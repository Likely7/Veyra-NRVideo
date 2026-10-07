# Blackmagic / DeckLink 采集兼容测试说明

状态：本机接口、像素和普通 USB 卡回归通过；Blackmagic 实卡待测试。此包是本地候选 `2.0.5-capture-test`，保留此前已经验收的 VFG 优化。

## 两个设备名称

截图中的 `Blackmagic WDM Capture` 与 `Decklink Video Capture` 是两个驱动入口。它们可能来自同一张物理卡，现有日志和截图不足以确认数量，程序继续分别列出，不合并设备身份。

用户原日志为 2.0.4 / RTX5070Ti / 驱动576.88，五次连接均收到连续帧并送显。收到帧不等于画面非黑，旧日志没有源像素统计。日志还包含社区 NR 初始化异常 `BAD00002 / SEH C0000005`，这是另一项故障，实卡验收应先关闭全部增强。

## 本次改动

- 按 OBS libdshowcapture 的成熟实现，补齐 WDM 上游输入选择器的发现与连接。使用设备 medium 的 GUID 和实例 ID 精确匹配，保留已有连接；不自动修改 HDMI/SDI 路由。找到但连接失败时显示错误并记录 HRESULT。
- HDYC 直接进入现有 UYVY GPU 解包路径，按其格式提供 BT.709 缺省，明确颜色元数据和手动覆盖仍优先。v210 仍使用既有系统转换，此次没有增加原生 10bit v210 解包。
- 采集面板增加“设备属性”；Blackmagic 增加“输入选择”。连接、采集中及断开尚未完成时，接口拒绝争抢设备；断开后可以配置。没有属性页的驱动给出明确提示，可用 Desktop Video Setup 配置。
- Blackmagic 优先列出驱动当前配置，明确这不是实时信号检测；保存的用户选择仍优先。格式标明逐行/隔行，保存键包含扫描及场序标志，避免 p30/i60 碰撞。旧键仅唯一匹配时迁移，歧义要求重选。
- Blackmagic 原始 CPU 样本每秒最多检查 32×18 个点，不增加 GPU 回读。连续三次黑采样后提示检查输入制式，像素恢复后清除提示。黑场内容也会触发此观察，不能据此断言物理无信号，也不会丢弃或改动该帧。

## 实际验证

最终构建 `capture-build-v4` 成功，EXE SHA256：
`0e95e46d0419fd2dbb04e33a55e6c47ecdc6ff93dad14c51a286552f30848214`。

| 检查 | 结果与边界 |
| --- | --- |
| 采集颜色、原生 sink、WDM medium/方向/重复连接/失败回传、黑采样 | 213 项通过，0 失败；上游接口为合成 COM 测试，不是真卡 |
| 格式身份和旧键迁移 | 0 失败，覆盖 p/i 歧义拒绝及唯一匹配迁移 |
| 真实 GPU 像素 | 124 个输出检查通过；HDYC 全/限范围灰阶误差为 0；带 padding 彩色图正/反向输出均与 UYVY+明确 BT.709 逐像素一致，colorSpread=1 |
| 普通 USB 采集 | 本机 KUHAIMI 27P、1080p60 YUY2，连接和重连两段回调为 60.084 / 59.961fps，PTS 递增；无上游 crossbar、无路由修改 |
| 产品 GUI | 连接、收帧、驱动设置在连接中/采集中被拒绝、断开后按钮恢复、旧 VideoInfo2 键重启恢复均通过 |
| 驱动属性缺失 | 普通 USB 实际创建 COM 图，GetFormat 成功，无输入属性页时返回 E_NOINTERFACE，按钮及所选格式保持可用 |
| 可见像素 | 产品截图完整显示采集卡内置“无信号”提示图；证明该卡的非黑像素能走到输出，不代表外部 HDMI 视频已验收 |

没有 Blackmagic 实卡，没有验证其 HDMI/SDI 输入切换、特定型号的属性页、隔行画面质量或社区 NR 异常恢复。此次没有增加去隔行算法，也没有把 USB/合成测试外推为 Blackmagic 已解决。其它补帧实现、NR 模型、SDK、运行库和着色器保持原字节。

首轮构建 `capture-build-v1` 失败：Windows SDK 将 IKsPin 声明置于 `__STREAMS__` 条件下，且初版引用了不存在的 HRESULT 名称。改为局部暴露 SDK 声明及真实错误常量后 v2 通过；v3 修正单场/双场样本的场率标签；最终 v4 补齐连接/断开期间的驱动争用保护。原失败日志保留，没有伪报通过。

验收日志统一位于 `E:/项目/Veyra/logs/blackmagic-capture-20261007/`：`capture-build-v4.log/json`、`contracts-v4/`、`usb-rate-v4/`、`gui-v4/`、`driver-page-v4/`。实图位于 `E:/项目/Veyra/tests/blackmagic-capture-20261007/gui-v4/screenshots/`。源码与 EXE 的逐文件对应关系、完整包清单和 CRC 核对结果见同任务 `verify/` 及包内 `package-manifest.json`。

## 请对方这样测试

1. 解压到新目录，先关闭 madVR、OBS 等正在占用此卡的软件。启动后关闭 NR、超分、补帧和其它增强。
2. 用 Blackmagic Desktop Video Setup 或采集面板的驱动设置，确认实际接的是 HDMI 还是 SDI。某个入口没有设置页并不等于设备不支持，可从官方配置工具设置。
3. 选择与真实信号一致的分辨率、59.94/60、逐行/隔行。设备 FPS 先保持 0；“驱动默认”仅是配置值。驱动列出的 HDYC/UYVY 可以直接试，避免同时改动多个变量。
4. 两个入口分别连接测试，先确认基础画面。如果没有出画，断开后再换入口或制式。不要同时打开两条接口争抢可能相同的硬件。
5. 基础画面正常后再逐项打开增强。若仍黑屏，发本次日志及所选格式；新的 `capture-upstream` 和 `capture-source-pixels` 会区分上游连接失败、驱动原始黑采样和后续处理问题。若社区 NR 继续初始化失败，再单独排查 NR。

## 来源

- OBS libdshowcapture 固定提交 [c13d4b7b0c66979396ba0a9060c9aafc15bb7b22](https://github.com/obsproject/libdshowcapture/tree/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source)，LGPL-2.1-or-later；文件、函数、修改说明及许可证见 THIRD_PARTY_NOTICES.md 和新头文件。
- [Blackmagic DeckLink SDK 概览](https://sdk-doc.blackmagicdesign.com/decklink-sdk/section1.html)、[Desktop Video 使用手册](https://documents.blackmagicdesign.com/UserManuals/DesktopVideoManual.pdf)。本次沿用 DirectShow，不宣称接入 DeckLink 原生 SDK。

构建、日志、临时、测试、归档和候选包都在 E:/项目/Veyra 对应任务目录。隔离分支 `codex/blackmagic-capture-20261007`，其它工作树和正式包保留。
