# Veyra 2.0.5 · 稳定性、跨厂商导出与性能回归修复

2026-10-07 · Windows 11 x64 · 正式应用版本。增强运行组件仍保留各自实验/社区性质，不代表NVIDIA、AMD或Intel认证，也不是完整官方DLSS 5集成。

本次包含公开2.0.4之后的OBS/AMD NR/多层导出修复、PR #19/#20适配，以及补帧选择、FSR导出、显示同步和统计界面性能回归修复。NR强度5、自动/手动画面调控和统一列表/节点预设等2.0.4功能继续保留。

## 下载与升级

- NVIDIA：Veyra-2.0.5-NVIDIA-win64-portable.zip。
- AMD：Veyra-2.0.5-AMD-win64-portable.zip；lmxxf NR目前限定RX9000，需驱动提供HIP 7。
- 运行只需适合当前显卡的一个完整包。在可写新目录完整解压后启动veyra_qml_ui.exe，先确认基础播放，再逐项开启效果。保留旧版便于回退，勿混装旧DLL或整个runtime目录。
- 重编译使用本Release的应用源码和依赖源码两个资产；SHA256SUMS及包内manifest用于核对发布者文件，不限制用户自行替换兼容运行库。[源码/构建](BUILD_2.0.5.md)。

## 已修复的问题

**统计界面性能回退。** 高频读数不断重启数值动画，在本机320Hz环境产生约320次/秒UI提交，与NR竞争GPU。改为有界33ms更新和像素精度变化判断，保持读数推进、交互与动画；隐藏、关闭动效和零时长时停止无效更新。未减少NR工作、切换软件界面或提高GPU优先级掩盖差距。已确认2.0.3默认是普通优先级，不是高。

**AMD/Intel补帧默认和旧配置。** 新配置优先选择可用FSR，再回退XeSS；旧配置中不可用DLSS/VFG也按此迁移，保留启用状态及独立列表/节点设置。已可用的手动选择不被覆盖。通用“添加补帧”按所有后端实际能力判断，避免FSR/XeSS可用却因DLSS不可用而禁用入口。

**FSR补帧视频导出。** 冻结、规划和工作进程保留所选后端与倍率，不再偷偷替换为DLSS而产生错误限制。FSR使用共享图生成可编码帧；FSR4硬件/provider检查保持。XeSS当前接口不提供离线可编码补帧纹理，界面和工作进程会明确拒绝并提示替代选项，本版未实现XeSS补帧导出。

**AMD NR黑屏及视频分辨率限制。** 修正lmxxf输出线性颜色契约和共享残差/时域合成。RX9000预览已获用户实卡可用反馈。视频导出最高内部1080p推理，再合成到原工作/输出尺寸，避免把4K输入直接当4K模型推理拒绝；这不等于原生4K NR。图片原尺寸模型预算限制保持，AMD真实HIP高分辨率导出/硬编码仍待实卡复测。

**多层NR导出。** 冻结每层/全局设置和执行计划，修正离线native尺寸及旧汇总字段不一致，避免预览正常、导出失败或参数遗漏。原版双层NR和SR→NR→FSR→编码链已验证。

**OBS兼容模式UI拖影。** 修正软件QML重绘/脏区域更新导致的滚动残影，保留现有兼容模式和视频GPU处理。完整UI采集仍建议Windows窗口采集；不宣称GPU界面游戏采集限制已消失。

**窗口/全屏显示同步。** Auto明确使用Present(1,0)，手动允许撕裂保留Present(0,ALLOW_TEARING)，同步低排队深度1保持，增加实际参数日志。12组窗口/全屏、队列和同步组合通过API/像素验证；未直接复现用户物理屏幕扫描/VRR的所有撕裂现象，不能保证全部来源/驱动设置消除撕裂。

**GPU优先级。** 请求档位已生效时避免重复写入调度API，保留用户保存选择。该项未单独测得提速，不计入性能收益。

## PR #19 / #20 适配

- PR #19：HDR显示输出曲线、调优预设、力度、显示峰值及HDR10 metadata生命周期，适配列表/节点、预设/会话和QML接口。调优默认关闭，关闭/力度0保留原像素；属于显示输出处理，不烘焙进离线导出，不宣称新增自动HDR导出算法。
- PR #20：MSVC UTF-8日志转换与RemotePlay条件编译；关闭PS5 RemotePlay时保留Moonlight/Xbox所需C编译链。标准和RemotePlay-off构建均通过。
- HDR验证为GPU像素/软件契约及metadata调用结果，未测实体显示器亮度/色差，未证明Windows/显示器实际采用metadata。暂缓PR #13/#14不在本版。

## 优化数据与条件

RTX5070/驱动616.56/320Hz；用户GTA VI 4K30，NVIDIA原版NR310.8.0.0，单层内部1080p、总变化1/风格0，画面调控/时域及SR/FG/HDR关闭，实际GPU普通class2。每轮正常播放35–40秒，无压力/竞争负载。数值为一秒GPU平均读数的中位数，不是逐帧P95或屏幕延迟。

| 对照 | 修复前/旧版 | 修复后 | 结果 |
|---|---:|---:|---|
| 同EXE仅恢复旧统计动画，GTA NR区间 | 6.964ms | 6.559ms | 降低5.80% |
| 上述同组UI提交 | 320次/秒 | 35次/秒 | 减少89.06% |
| 原2.0.3与修复后，GTA相邻匹配 | 6.558881ms | 6.559476ms | +0.009%，未再出现约7%差距 |
| 原2.0.3与修复后，另一4K60素材 | 6.632515ms | 6.590205ms | −0.638% |

上述是fix2阶段匹配/反向因果证据，本版保留同一产品修复。全新2.0.5另用用户GTA视频复核，普通优先级32次观测NR中位6.295ms、视频提交30fps；这是独立单轮检查，不与前表拼接计算额外提速。时钟/覆盖层及其他活动未锁定，结果限列明环境/设置。UI提交减少不等于GPU总占用、端到端延迟或显存减少。[完整调查](STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md)。

## 2.0.5实际验收

全新生产/相关目标构建13项/513步通过；7项CPU契约套件、真实统计组件6/6、AMD共享图10例、显示同步12组和真实DLSS4X/6X/4X像素/生命周期检查通过。AMD共享图是恒等诊断提供器，不是HIP实卡推理。

FSR2X→H.264及FSR SR→原版NR→FSR2X→HEVC实际导出均60源帧→120编码帧，后者2560×1440；双层原版NR→HEVC为60→60帧。4份输出全部逐帧解码及严格CFR PTS核验通过。实际QML播放、暂停恢复、seek、全屏切换、列表/节点、FSR GUI导出和重启恢复通过。缺NVIDIA运行库的AMD包在RTX上验证选择迁移/入口，不伪装AMD实卡。

正式包与已测2.0.5共用相同EXE/QML/shaders，分别保留NVIDIA215/AMD694项原组件身份。发布仅更新文档/包元数据，重新进行最终ZIP逐文件/CRC、PE依赖与隔离冷启动核验；实际结果见发布回执。[2.0.5构建验收](RELEASE_2.0.5_ACCEPTANCE_2026-10-07.md)。

## 已知问题与边界

**NVIDIA显存持续增长尚未定位/修复，按用户决定留待下一版本深入排查。** 本机短测不证明长期不增长，不能把缺失Shutdown、驱动或AMD内存池假说写成根因。本版没有基于这些猜测的生命周期改动。

AMD真实HIP高分辨率导出/硬编码、FSR4 ML实卡、RTX20/30/40全部组合、主机/采集长稳、物理撕裂/VRR/HDR仍有验证缺口。XeSS补帧离线导出、节点离线导出及节点全局NR保护区仍不支持。严重后台掉帧/部分NR TDR不宣称根治。运行库/模型不升级不修改；源码与专有组件隔离，保留patched FFmpeg PS5 slice补丁和静态字幕依赖。

## English

Veyra 2.0.5 integrates repairs after public 2.0.4: OBS software-UI repaint, AMD NR linear-output/compositing, frozen multi-NR exports, adapted PR #19/#20, cross-vendor FG defaults/export, Auto display sync and telemetry animation performance. Strength-5 controls and unified List/Node presets remain.

- Bounded 33ms telemetry updates avoid monitor-rate GPU contention while preserving animation and NR work. On RTX 5070/616.56/320Hz, GTA 4K30, original single-layer internal 1080p NR and actual Normal priority, reverting only the fix in the same EXE changed NR 6.559→6.964ms and UI 35→320 submissions/sec. The fix reduces these by 5.80%/89.06%. Adjacent matched 2.0.3/fixed results differed +0.009% on GTA and −0.638% on 4K60. Local interval measurements are not universal FPS, display-latency or VRAM gains. The fresh 2.0.5 single 6.295ms run is not a new matched speedup.
- AMD/Intel new settings prefer available FSR then XeSS. Unavailable saved DLSS/VFG choices migrate while enabled state and independent List/Node settings survive; supported manual selections remain. Add FG checks all providers.
- FSR offline exports retain backend/multiplier instead of being forced DLSS. Frozen settings/plans fix multi-NR export. AMD videoNR uses at most internal 1080p inference and composites to original output size; it is not native 4K NR, and image budget restrictions remain. XeSS offline FG is explicitly rejected: the integrated interface exposes no encodable generated texture.
- lmxxf linear-output/shared composition and OBS software-UI scroll trails are repaired. User RX9000 preview confirmation does not establish real AMD offline encoding or GPU-UI Game Capture support.
- Auto uses Present(1,0), manual tearing uses Present(0,ALLOW_TEARING), synchronized low-queue limit remains 1. Twelve window/fullscreen combinations passed API/pixel checks; physical tearing/VRR remains partially unverified. Redundant GPU-priority write avoidance has no separate claimed gain.
- PR #19 adds opt-in HDR display curves/presets/amount/peak/metadata lifecycle with session/preset adaptation; PR #20 fixes MSVC UTF-8 logging and RemotePlay-off compilation. HDR display tuning is not baked into exports; physical HDR/metadata adoption are unverified. PR #13/#14 remain deferred.

Fresh 2.0.5 build and targeted CPU/GPU/QML checks passed. Real FSR H.264/HEVC and stacked-original-NR outputs passed full decode and strict CFR PTS. AMD selection tests ran the AMD package on RTX with missing NVIDIA components; AMD graph checks used an identity provider, not HIP. Final formal ZIPs retain the tested EXE/QML/shaders and runtime bytes; publication audits/cold starts are recorded separately.

**NVIDIA VRAM growth remains unresolved and deferred to the next version.** Short runs do not establish long-term stability. RealAMD high-resolution HIP/encoding, FSR4 ML hardware, all GPU combinations, console/capture long runs and physical display tests remain incomplete. XeSS FG/Node offline export remain unavailable. Experimental/community runtimes are not vendor certification. Extract the matching complete ZIP into a new writable folder, retain the old version for rollback; application and unchanged dependency sources are supplied.
