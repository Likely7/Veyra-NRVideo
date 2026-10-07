# Veyra 2.0.6

本次重点：VFG 补帧吞吐优化、AMD NR 开启后结果被后续 FSR 覆盖的修复，以及 Blackmagic/DeckLink 采集兼容。提供 NVIDIA / AMD 两个完整便携包；应用版本为正式版，NR 与社区/解锁运行组件继续保留各自实验边界。

## VFG：修复“有余量却上不去”的一类瓶颈

原先 VFG 的 CUDA/SDK 提交工作会占住呈现线程，GPU 性能球有余量也可能错过呈现时机。现在提交工作进入有上限的独立 worker，每个输出槽显式区分等待、成功与失败；只有有效完成的结果参与呈现，失败仍保留源帧。GPU 同步、停机回收和离线导出完成判定保持完整。

只调整 NVIDIA VFG，不改 DLSS、XeSS 或 FSR 补帧算法，也不通过提高默认 GPU 优先级获得这次数据。队列最多 14 个任务，覆盖两组 8X 工作；这不是无界缓存。运行库缺失和生成失败仍有回退，不把未完成纹理当成功帧。

### 匹配性能对照

RTX5070 12GB / 驱动616.56；GTA VI 4K30 视频输入，原版单层 NR、内部1080p、强度1、风格0、零运动，自动调控/抗闪烁/SR/HDR/调色关闭；VFG仍处理4K。窗口1280×800、自动显示同步、输出不限帧，实际普通GPU优先级。每轮预热5秒、统计20秒，无并发GPU任务。下表是同次优化验收的相邻匹配对照中位数，计量为软件呈现/提交 FPS。

| VFG 档位 | 优化前 | 优化后 | 提升 | CPU 提交滚动 P95 的中位数 |
|---|---:|---:|---:|---:|
| 中等 4X | 80 FPS | 120 FPS | +50.0% | 23.450 → 1.198 ms |
| 高 2X | 35.5 FPS | 60 FPS | +69.0% | 30.940 → 1.079 ms |

同一候选程序把中等4X强制切回同步提交后为76.5 FPS，正常异步提交为120 FPS，支持本次瓶颈定位。更完整矩阵中：低档2–8X达到对应60–240 FPS目标，中等2–5X达到60–150 FPS，高档仅2X达到60 FPS。更高倍率仍受计算/带宽等预算限制；不能推断“高档8X也能跑满”。CPU提交减少不等于推理耗时、屏幕延迟或显存占用同比减少。

既有优化验收覆盖3组GPU测试各913检查、168项同步/异步输出hash一致、设置331检查、21种720p导出及4K NR/VFG导出，另有切换/暂停/seek/全屏/取消/失败回退检查。2.0.6另进行新构建的针对性回归，结果见[发布验收](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md)。详细配置和原始数据见[VFG报告](VFG_OPTIMIZATION_REPORT_2026-10-07.md)。

## AMD NR：修复开启执行但画面没有变化

在“NR先处理 → FSR超分”的链路中，NR已有正常执行记录，后续FSR却可能再次读入原图，覆盖NR结果；普通缩放的首帧/reset路径也存在同类输入选择问题。现在统一读取当前已完成的处理阶段输出，覆盖文件播放、串流共用图及相关导出路径。此修复不修改NR模型、强度参数、HIP运行库或FSR算法。

此前定向复现6个用例失败且最大像素码值偏差156；修复后20个图用例全部通过，最大偏差0、D3D12验证错误0，包含NR先行、FSR、普通缩放、首帧/reset、多层、运动/时间历史及4K导出图。该证据来自RTX5070上真实FSR3.1.5与仅用于测试的确定性NR provider；**AMD HIP、FSR4.1 ML与用户Xbox实机仍需复测**，不以替代测试冒充实卡效果验收。详见[AMD修复报告](AMD_NR_FSR_REPORT_2026-10-07.md)。

## Blackmagic / DeckLink 兼容

- 补齐Blackmagic WDM可能需要的设备medium/上游crossbar连接，不替用户改变输入路由。
- 支持HDYC按UYVY解包，并在无明确元数据/手工覆盖时采用BT.709；保留显式颜色设置优先级。
- 按设备返回的精确格式协商，保留VideoInfo2扫描/交错字段，减少格式看似相同但无法连接的问题。
- 采集断开时可打开设备/输入驱动属性，选择正确HDMI等输入；增加有限频率的黑场采样诊断，帮助区分已收到黑画面与完全没帧。

来源、固定提交与LGPL声明已记录在THIRD_PARTY_NOTICES。现有回归包括213项采集/COM/颜色检查、124项GPU颜色检查和本机USB卡约60FPS回调/重连检查。**本机没有Blackmagic卡，尚不能确认反馈者真实HDMI信号已恢复；列表中的两个名称也不一定是两张卡。** 详见[采集报告](BLACKMAGIC_CAPTURE_REPORT_2026-10-07.md)。

## 下载、更新与反馈

1. 按显卡选择NVIDIA或AMD完整便携包，在新目录完整解压；保留2.0.5方便回退，不把两个厂商包覆盖混装。
2. 本次不升级运行组件/模型；patched FFmpeg、许可证和对应依赖源码继续随包提供。校验下载请使用SHA256SUMS.txt。应用及依赖源码见Release独立资产；重编译/静态依赖重链接见[构建说明](BUILD_2.0.6.md)。
3. 4群已满，README和本Release更新为用户提供的**交流群5**二维码，图片标注**2026-10-14前有效**；微信赞助二维码、Discord与Ko-fi继续保留。
4. 反馈请附显卡/驱动、输入来源、NR/补帧运行版本、处理顺序与日志。公开日志前请隐藏账号/会话信息。

已知：NVIDIA持续显存增长尚未定位/修复；部分高倍率VFG仍达不到目标帧率；AMD实卡HIP/FSR4.1、Blackmagic真实输入、所有设备的长期稳定性未全部验收。XeSS补帧/节点离线导出仍不支持。Xbox日志中的独立硬解回退问题未在本次修复范围内。

---

## English release notes

2.0.6 improves NVIDIA VFG scheduling, fixes the NR-first output handoff to FSR/scaling, and adds Blackmagic/DeckLink capture compatibility. Separate complete NVIDIA and AMD portable packages are available. The application release is stable; experimental NR/community/unlock components retain their existing status.

**VFG:** bounded worker submission removes CUDA/SDK CPU work from the presentation thread. Output slots explicitly track pending/success/failure, with source-frame fallback and complete teardown synchronization. Other frame-generation algorithms and runtime binaries are unchanged. Matched adjacent RTX5070 tests with GTA 4K30, one original NR layer at internal1080p and normal GPU priority improved Medium4X from80 to120 software-present FPS (+50.0%) and High2X from35.5 to60 (+69.0%). Median rolling CPU-submission P95 changed23.450→1.198ms and30.940→1.079ms respectively. Each run used5s warmup+20s observation, VFG at4K, no competing GPU workload. These are conditional submission measurements, not screen refresh, end-to-end latency or VRAM savings. Higher multipliers remain budget-limited. Full settings/evidence and fresh release checks are linked above.

**NR → FSR/scaling:** the later stage could read the original image despite NR executing successfully, making the NR toggle appear ineffective. Both FSR and relevant scaling/reset paths now consume the completed stage output. Six reproduced failures became20 passing graph cases with zero marker error/D3D12 validation errors. This uses real FSR3.1.5 and a deterministic test NR provider on RTX5070; AMD HIP/FSR4.1 ML and Xbox hardware confirmation remain pending. The test provider is never shipped.

**Capture:** Blackmagic WDM upstream connections, HDYC/UYVY with a BT.709 fallback, exact driver format negotiation including scan/interlace metadata, disconnected driver-property dialogs, and bounded black-frame sampling diagnostics. No local Blackmagic hardware was available; this is compatibility implementation plus regression evidence, not confirmed physical HDMI acceptance.

Extract the appropriate vendor package into a new folder. Runtime/model/codec identities and patched FFmpeg are preserved from2.0.5; application and dependency source archives, notices and checksums accompany the release. Group4 is full; the supplied Group5 QR replaces the current community QR and is valid before2026-10-14. Existing donation QR, Discord and Ko-fi remain. NVIDIA sustained VRAM growth is unresolved; higher VFG multipliers and long-running cross-device stability are not universally validated. XeSS FG/node offline export remains unsupported.
