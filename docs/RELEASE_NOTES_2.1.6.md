# Veyra 2.1.6 更新说明

本次汇总已发布 **2.1.5 之后** 的功能和修复：即时回放、麦克风录制、全效果剔除，以及自动去重、60→30 和实时补帧调度改进。

## 新增：即时回放与麦克风

- **保留刚才那一段画面。** 即时回放默认关闭，开启后滚动缓存最近 **1 分钟**，可选 **15 秒至 10 分钟**。在录制按钮旁直接开关，或到「设置 → 播放」调节长度；按 **F9** 或顶部回放按钮保存到录制目录。快捷键可修改，游戏模式也支持；**F8 普通录制可以同时运行**。
- 缓存保存的是实际处理、呈现的画面和声音。保存从关键帧开始，可能比所选长度多一个 GOP；刚开启、切源/尺寸/HDR变化或触及内存上限时，可保存长度会缩短，不跨不同尺寸拼接。
- **录制麦克风。** 支持选择设备、与游戏声音混音，或保存为独立 AAC 音轨；只录麦克风也可用。双轨录制需在播放器/剪辑软件选择音轨，普通播放器通常默认播放第一轨。麦克风断开或权限不足会提示，画面和游戏声音继续录制。
- 即时回放开启后持续占用额外一路硬件编码和内存，并非按 F9 时才开始工作。缩短缓存主要节省内存；降低录制帧率可减轻编码负担。不保证满载机器开启后仍不掉帧。
- 修复回放音频在画面停止更新时突破内存上限、单个过大 GOP 无法裁剪、保存过程中片段结束导致保存截止越界；限制快速切源积压的编码线程，并验证并发保存拒绝、保存失败恢复及退出中保存。

## 画面保护与补帧

- **全效果剔除区域。** 支持最多四个矩形/椭圆区域及羽化；区域内恢复原画面、按原始画面节奏刷新，补帧也不参与。列表和节点设置可保存。修正 ANR 生成后覆盖区域合成的顺序，并对 SDR/HDR、缩放、NR＋SR 和多倍率补帧做了像素回归。
- **恢复独立 NR 保护区域和输出稳定器节点**，保留与全效果剔除不同的作用范围。修复列表中美颜层删除，删除失败可回滚。
- **精修光流扩展到图内 DLSS、FSR、ANR**，继续复用已有边缘修复；VFG 使用其内部光流。合成平移场景中 ANR/FSR 边缘误差下降，但静止 HUD 模糊没有因此解决，且存在额外 GPU 开销。
- **ANR FG 适配源码已接入**共享图、2×–6×、设置/预设、HDR、导出及失败回退。**公开包不携带 ANR DLL、模型或程序包**：上游协作者于 2026-11-13 明确禁止当前 Beta 第三方再分发。没有外部运行组件时该选项不可用；本机实验数据不代表 AMD/Intel 或所有显卡通过。[上游声明](https://github.com/apex-compute/ANR/issues/9#issuecomment-6857895225)
- 补帧选项增加 VFG 金色皇冠、ANR 红色及当前界面提示。这些标签不是本次跨显卡客观画质排名。

## 自动去重、60→30 与间歇卡顿

- 「去除重复帧」统一为独立开关，下拉保留「自动去除」与「60→30」；旧的多余模式读取时迁移到自动。
- 自动去重不再只接受稳定周期：使用独立滚动变化窗口识别变帧率，静止画面不伪造新帧。软件漏读不再重置整个内容时钟，恢复后不会沿用此前停顿跨度作为每对画面的等待时间。
- 60→30 只用相邻「新图 → 重复图」建立相位证据，优先每对保留第一份，修复静止恢复或漏读后连续追取。59–61 Hz 范围外不强制减半。**它是固定半速采样，40/60 FPS 内容会舍去部分新图，不能替代自动去重。**
- 优先送出已到期帧，在图处理前/补帧提交后增加非阻塞检查点；物理采集无补帧路径使用 GPU fence 交接，减少 CPU 观察等待。采集补帧期限余量在 **0–1 ms** 内自适应；XeSS 保留提供方内部节奏。
- 对能在一个源间隔内完成的重档补帧放宽相位上限；补帧持续被预算拒绝满一秒后，真实帧就绪即送，避免继续按不可达的补帧相位扣留。重负载探测仍有代价，见下方数据和限制。
- 采集回调发现明显新画面后提前结束比较，参考帧复制放到发布后。实时应用日志改为有界异步写入，播放/采集线程不执行文件/stdout写入和 flush；队列压力丢弃会计数报告。修正 Waiting/EOF 后尾帧进度快照遗漏。

## 默认值、界面与兼容

- **默认 GPU 优先级改为「高」；VFG 自动调控默认关闭。** 已明确保存的优先级和开关保持，不覆盖用户选择。VFG 自动调控仍受倍率、质量档和实时输入条件限制。
- NVIDIA 低延迟仅保留「关/开」，移除 Boost；旧增强值迁移为普通开。驱动 Sleep 反复异常长等待时，本会话回退，避免继续阻塞。
- **HDR 曲线预设、亮度曲线与元数据默认折叠**，展开后完整调节，收起不重置参数；HDR 输出格式仍直接可见。
- PS5 Account ID 获取改用 PSNTools 网页后手动配对，移除应用内旧 PSN OAuth 登录；已有主机/凭据数据不删除。
- 赞助入口改为 Patreon 主页和咖啡支持，交流群更新为 10 群，保留微信赞助、Discord 与知识库。
- 打包递归核验 **88 个着色器**，包括 GPU DIS 子目录；避免只复制顶层而漏掉某个效果。

## 本轮数据：条件和代价一起列出

以下是**本轮各修复阶段的同场景 A/B**，并非所有项目都使用公开 2.1.5 与最终 2.1.6 两包直接对比。后续整合做了功能回归，没有重跑全部性能对照。主要设备为 RTX 5070、Ryzen 7 9700X、1440p/320 Hz 显示器、T911 4K60 NV12；采集测试使用一层 1080p NR。FPS 和延迟均为软件侧计时，**不是物理屏幕刷新率或手柄到光子的端到端延迟**。

| 同条件采集对照 | 修复前 | 修复后 |
|---|---:|---:|
| DLSS 2×：提交间隔折算 1% low，窗口 / 全屏 | 105.39 / 106.66 FPS | 115.19 / 115.76 FPS |
| VFG 2×：同上 | 107.08 / 109.10 FPS | 114.67 / 115.36 FPS |
| 无补帧：回调到 Present 返回中位数，窗口 / 全屏 | 1.548 / 1.564 ms | 1.067 / 1.081 ms |
| 60→30＋VFG 高＋低 4×：1% low | 97.5 FPS | 111–114 FPS |
| 同场景每段 GPU 迟到帧数 | 300 | 2–9 |
| VFG 高 3×持续被拒：真实帧软件等待 | 38–46 ms | 约 6.5 ms |

代价：精度/检查点增加本机约 0.4–0.8 个 CPU 总占用百分点；上述 60→30 重档相位调整让真实帧晚约 0.3–2.2 ms。高 3×过载探测仍会产生长间隔，1% low 在该过载场景由 29.8 变为 26.5–29.4 FPS，不能把等待缩短宣传成满负载流畅问题已根治。

| 1080p 合成平移、精修光流关闭 → 开启 | 关闭 | 开启 |
|---|---:|---:|
| ANR 边缘带平均绝对误差（0–255，越低越好；本机实验） | 4.93 | 0.74 |
| FSR 3.1 边缘带平均绝对误差 | 2.62 | 0.27 |
| DLSS 光流阶段 GPU 时间中位数 | 1.360 ms | 2.443 ms |
| ANR 光流阶段 GPU 时间中位数 | 1.261 ms | 2.269 ms |
| FSR 光流阶段 GPU 时间中位数 | 1.261 ms | 2.314 ms |

上述误差改善约 85%/90%，只针对该合成场景的边缘带；DLSS 边缘误差基本不变。每组十个完成时间戳样本，测量包含反向光流和精修，不能换算成所有游戏的整体画质/帧率收益。

验证覆盖：Qt Quick **199/0/0**；回放缓冲 **37/0**；VFG 设置683、预设629、修复合同308项通过；19组GPU回归、12组实际程序、28个输出文件独立解码/轨道与时间轴检查。去重参考片检查完整60秒/3600采样，全部1898次已观察画面变化保留；真实30帧游戏验证了动态识别及约3帧锁定60→30第一份。录制/回放双音轨无串轨；标记片回放音画偏差0帧、普通录制1帧（30 FPS），实际 T911-MIC 48kHz 双声道采集开关正常。这些计数来自不同模块/阶段，**不相加当作独立用例总数**。[详细条件和证据](RELEASE_VALIDATION_2.1.6.md)

## 已知限制与升级

- 窗口模式 VFG 高档仍可能受到 DWM 上屏合成限制；提交60 FPS不代表面板显示了60个不同画面。高倍率超预算不会凭空补足硬件性能。
- 静止 UI/HUD 模糊、所有设备的间歇卡顿、报告人 RTX4070Ti SUPER/160Hz 的驱动全屏异常，尚不能统一宣称根治；HDR动态实卡、RTX30/40及AMD/Intel实机仍需各自验证。旧5080日志中的两层NR负载也不能靠去重修复消除。
- 选择与 Veyra 实际使用显卡匹配的 NVIDIA/AMD 完整包。退出旧程序后解压到**新目录**，迁移旧目录的 `runtime_local/user-data-2.0.0` 和自定义 `runtime_local/luts`；自定义数据目录以「设置 → 关于」为准。即时回放/麦克风默认关闭；已保存的效果和设置保留。公开包不含用户配置、日志或测试媒体，已安装的 VB-CABLE 继续使用。
- 实验运行组件不代表 NVIDIA/Intel/AMD 官方合作或认证，身份/许可/卸载信息见包内清单。ANR 上游当前禁止第三方再分发，公开包明确不含其运行资产。

## English

**Everything since 2.1.5:**

- Added instant replay: off by default, a rolling 60-second buffer with 15-second to 10-minute choices. Toggle beside Record or in Settings; F9/the replay button saves the past segment. Rebindable in game mode; F8 regular recording can run concurrently. It consumes an additional hardware encoding session and RAM. Keyframe alignment may add a GOP; memory limits or source/size/HDR changes can shorten the clip.
- Added selectable microphone capture, mixing or a separate AAC track, including mic-only recording. Missing/denied devices report an error while picture/game audio continue. Fixed replay capacity enforcement, oversized GOPs, save cutoff across segment retirement, and bounded encoder retirement; tested save concurrency, recovery and exit.
- Added feathered rectangular/elliptical all-effect exclusion regions, including generated frames; restored independent NR protection/stabilizer nodes and fixed beauty-layer deletion. Refined flow now serves graph-based DLSS/FSR/ANR. The independently written ANR host supports 2×–6×/HDR/export, but **no ANR runtime/models are distributed** following the upstream collaborator's explicit beta redistribution prohibition linked above.
- Automatic dedup now handles variable content cadence; 60→30 retains the first sample without extra catch-up after static/missed frames. Expired-frame-first scheduling, bounded checkpoints, 0–1 ms adaptive capture margins, overload real-frame release, earlier capture publishing and bounded asynchronous logging address several software stall mechanisms. XeSS retains provider pacing.
- GPU priority now defaults to High, VFG automatic regulation to Off; explicit saved choices remain. Removed Reflex Boost with legacy migration and added a long-Sleep fallback. HDR curve/preset/metadata controls start collapsed; PS5 Account ID lookup uses PSNTools/manual pairing. Updated Patreon/community links and recursively audited all 88 shaders.

**Measured, not universal:** stage-by-stage A/B on an RTX5070/9700X/T911 4K60 with one 1080p NR layer improved DLSS 2× submission-derived 1% low from 105.39/106.66 to 115.19/115.76 FPS (window/fullscreen), and VFG from 107.08/109.10 to 114.67/115.36. No-FG callback-to-Present-return medians fell from 1.548/1.564 to 1.067/1.081 ms. These are software timings, not optical latency, and the old program was an intermediate test build rather than the public 2.1.5 ZIP. CPU cost increased about 0.4–0.8 total-utilization percentage points on this machine.

In a 1080p synthetic translation test, ANR/FSR edge-band error fell from 4.93/2.62 to 0.74/0.27, with about 1 ms additional measured flow work; this does not establish HUD or overall game-quality improvement. A heavy 60→30 VFG4× case improved 1% low from 97.5 to 111–114 at a 0.3–2.2 ms real-frame delay cost. Releasing real frames during rejected VFG3× reduced software waiting from 38–46 to 6.5 ms, but overload probes worsened that case's 1% low from 29.8 to 26.5–29.4; overload is not solved.

Qt Quick 199/0/0, replay buffer 37/0, GPU/content/audio/lifecycle checks are recorded in the linked validation note. Windowed high-quality VFG/DWM behavior, static HUD blur and other-GPU/device validation remain limited. Exit Veyra, extract the matching GPU package into a new folder, and migrate `runtime_local/user-data-2.0.0` and custom LUTs (or use your existing custom data directory). Existing VB-CABLE installations are reused.

[完整提交记录 / Full changes](https://github.com/Likely7/Veyra-NRVideo/compare/v2.1.5...v2.1.6) · [对应源码 / Corresponding source](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.6-sources)
