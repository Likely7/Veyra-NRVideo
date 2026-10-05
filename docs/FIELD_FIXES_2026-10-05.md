# 2026-10-05 现场问题修复（Claude）

用户要求：预热默认关闭、设置页重新分类；排查软件不在前台时画面卡顿（`logs(4).7z`，用户本机不能复现）；AMD NR 开启无效（`veyra-qml(26).log`）；特效字幕支持（用户选择引入 libass）。

- 工作树 `E:/项目/Veyra/worktrees/field-fixes-20261005`，分支 `claude/field-fixes-20261005`，基于 Codex 性能分支 `2dbf26c`（未合并、未推送）。
- 构建 `E:/项目/Veyra/build/field-fixes-20261005/B`（`tmp/field-fixes-20261005/build.py`，沿用 2.0.3 依赖缓存），日志 `logs/field-fixes-20261005/`，测试 `tests/field-fixes-20261005/`。

## 1. 预热默认关闭 + 设置页分类

- `prewarmEnhancement` 默认 false（bridge 三处、`EngineController::prewarmEnabled_` 初值），开关提示写明约 0.8–1.8 GB 显存常驻；RTX 30/40（Ada/Ampere 补丁会话）上 `enhancementPrewarmAvailable` 为 false，开关置灰，不再显示可用却不生效。
- 设置页从 6 类改为 10 类：外观、启动与窗口、播放、音频、性能、兼容、PS5 串流、快捷键、组件与许可、关于。原“通用与外观”和“播放”里的行原样搬迁，objectName 不变；`scripts/ui-check/slider-typed.qml` 音量改到“音频”分类。三语新增 12 条，`extract.py --check` missing=0。

## 2. 后台卡顿（采集 + 补帧）

用户日志（RTX 5080 + AMD 核显，2560×1440@60 RGB24 采集，NR+SR+DLSS 3X）05:37:43 起：一次 50–100 ms 断档后，每约 100 ms 就出现 `scene history boundary cadenceBreak=true` + `reset-lifecycle`，`gpuCompletedFps` 60→22，持续到窗口重新获得前台。两个自我放大的环节：

1. 场景检测把“引擎自己跳帧/采集信箱丢帧”造成的 PTS 断档当成源节奏中断（3× 上一间隔 + 5 ms），每次都重置 NR/FG 历史；而引擎的有界跳帧策略本意是 ≤250 ms 保留历史。改为按源帧序号步长换算每帧间隔（`SceneCadenceAnalyzer::analyze(..., sourceStep)`，`EnhanceGraph` 传入 `sourceFrameId` 差），真实源断档仍会触发；顺带修正“重复帧后下一帧被误判为中断”。
2. 采集补帧的呈现相位间隔沿用 A/B 实际 PTS 跨度（≤100 ms 都接受），跨越丢帧时 100 ms 的跨度被当成节奏，下一对帧被拉长展示，信箱又丢帧，稳定在约 10 对/秒。现在跳帧/丢帧时用标称源间隔（`capturePairInterval100ns(..., historyReset||gapCandidate)`）。
3. 触发源推测为 Windows 对非前台进程的电源节流（EcoQoS 执行速度、计时器精度）。播放期间通过 `SetProcessInformation(ProcessPowerThrottling)` 退出这两项节流，停止播放即交还系统策略（`PlaybackPowerGuard`），日志 `[playback-power] process power throttling ...`。

验证：`veyra_scene_tests` 18/18（新增跳帧不算中断、真实 400 ms 断档仍中断、重复帧后不误判）、`veyra_live_timing_tests` 通过。本机无法复现用户现象，真实采集卡 + 用户系统版本上的效果**未验证**，需要用户实测；若仍卡，可在设置→性能试“GPU 优先级：高”。

## 3. AMD NR 开启无效

日志：`normalized requested NVIDIA effects ... nr=false`、`ui-nr-rollback`。`disableUnsupportedNvidiaEffects` 和 `describeStages`（`enableNr` 要求 NVIDIA 适配器）在非 NVIDIA 显卡上把 NR 关掉，所查用户会话中的 AMD NR 因此未生效；此前在 5070 上的宿主合同测试不能证明 AMD 实卡执行。修复：AMD 适配器保留 NR（图层已有 lmxxf 路径，不走 NGX），NR 光流若为 NVOF 改用 AMD FidelityFX；状态提示不再声称 NR 被关；导出在 lmxxf 预算内（≤1920×1080 像素、高 ≤1080）同样启用，超出时明确提示。合同测试新增 2 项。RX 9000 实卡推理/画质**未验证**（本机仅 RTX 5070）。

## 4. 特效字幕（libass）

1.4.4 实际只有“样式类”ASS（其文档写明 `\move`/`\t`/`\clip`/卡拉OK 未实现）。现引入 libass 0.17.5（vcpkg 静态 `x64-windows-static`，DirectWrite/GDI 字体提供者）：

- `SubtitleTrack::ass` 保存外挂 .ass/.ssa 全文，或内嵌轨的编解码器头 + 原始事件块；读取 Matroska 字体附件（≤64 MB/个，合计 ≤256 MB）。
- `apps/veyra/ui/AssSubtitleRenderer` 按视频显示区域（含缩放/平移）渲染并合成到原有分层字幕窗口；只在 libass 报告变化或几何变化时刷新。SRT/VTT 等仍走原样式渲染（字号/描边/背景偏好不变）。
- 实测（`logs/field-fixes-20261005/sub-ext-v1`、`sub-mkv-v1`、`sub-srt-v1`）：外挂与 MKV 内嵌 ASS 的卡拉OK、`\move`、`\t` 变色缩放、`\clip`、矢量绘图、淡入淡出均正确；SRT 逗号/两行回归正常。

构建注意：vcpkg/MSVC/meson 在含中文的路径下失败（D8050、跨盘 relpath、ninja 链接路径编码），libass 构建树与安装目录放在纯 ASCII 的 `E:/veyra-ascii-20261005/`（`installed/x64-windows-static` 供链接），另有一份 `x64-windows-static-md` 失败尝试的产物留在 `E:/项目/Veyra/deps/libass-20261005/`；vcpkg 默认二进制缓存写入了 `%LOCALAPPDATA%/vcpkg/archives`。打包需随附 `share/*/copyright` 许可文本（见 THIRD_PARTY_NOTICES）。

## 回归

最终包副本 + 新 EXE 实际运行：设置页 6 个新分类截图、预热默认关、字幕三组截图，均无 ERROR。早期 S4 普通播放 B/C 交错各 2 轮：两版都出现每 7–8 秒一次 0.5–2 s 的 CPU 侧卡顿（同时段验收版 B 也有，此前同配置为 0），当时推测为机器外部负载；两版均无节奏中断重置。后续复测见第 5 节，不能用卡顿消失证明具体根因。

## 5. 播放中改效果后 GPU 耗时全部显示“未测量”（用户实测发现）

用户用第一版测试包（4K 片源，播放中多次开关 NR、加层、开 2X 补帧）发现界面耗时全是“未测量”，日志 `enhancementProcessingMs=-1`，但 `player-timing` 里各阶段 GPU 时间正常。原因是性能分支（2a/2c/2d）把处理图改成每次重建都新建对象，而“记录 GPU 计时”只在开播时对最初的对象打开一次；之后任何重建、缓存复用或预热接管得到的新图都不再记录。优化前图对象全程复用，所以没有这个问题；Claude 验收时的测试都是先设效果再开播，也没覆盖到。修复：每次收集计时前对当前图重新打开记录（`EngineController.cpp` collectTimings）。验证 `logs/field-fixes-20261005/timing-toggle-v1`：播放 3 秒后开 NR，重建后增强耗时约 8.5 ms 持续有值。

用户这轮的“停顿”记录：0.2–2.4 s 的都对应效果切换时的重建（首次 NR 约 2.4 s、开补帧约 1.8 s、再次开关/加层 0.2–0.8 s）；330 条约 50 ms 的记录都在暂停期间，是暂停循环 50 ms 轮询被停顿记录器记下，不是卡顿。播放期间跳帧 0、补帧过期 3。

此前 S4 对比里“每 7–8 秒卡 0.5–2 s”的现象复测已消失：B/C 交错各两轮，>200 ms 停顿 0、跳帧 0、出帧间隔 P99 约 17.2 ms。本组未观察到明显版本差异；没有查明具体外部进程或因果链，不能据此确认当时是外部负载。

## 6. Codex 接管复核（2026-10-05）

用户明确要求读取 Claude 最新聊天并接手。已读取 `C:/Users/123/.claude/projects/E-----Veyra/b667394c-e878-4628-ae05-663760b1a66b.jsonl` 中今天的真实用户指令、验收、现场修复和最后打包调用；Claude 的最后消息是额度耗尽，但打包进程已经完成，不需要重打同一包。

继续使用本工作树 `claude/field-fixes-20261005`，HEAD `2dbf26cfe5d5dfe4491d68415064a8a0588b459b`；继承全部未提交修复。Codex 本轮没有改产品源码、重新构建、提交、合并、推送或发布，没有修改桌面旧工作树和用户配置。Claude 原验收报告的总判定仍为“有条件通过”，后来发现的 GPU 耗时 P1 已在本分支修复，不能把原报告理解成零剩余问题。

### 已完成的第二版 NVIDIA 本地包

- 包：`E:/项目/Veyra/test-packages/field-fixes-20261005/Veyra-2.0.3-field-20261005b-NVIDIA-win64-portable.zip`，714196456 bytes，SHA256 `85c8da0203abe79d07715a3afe08f1f377f278f5743aceaf16148235c84b75c6`。
- 对应源码：同目录 `Veyra-2.0.3-field-20261005b-veyra-source.zip`，SHA256 `93c773d6216caa908921869e5d43f30dd2ed35ca000797f3fec64f463198262d`；1605 个源码文件与接管时工作树原字节一致。本节及后续交接文档不在该历史源码 ZIP 中，生产源码没有变化。
- 主程序 SHA256 `09b439d8faf4df121a0749b6eaaccf71de0b8c344d144feb1ea743cc7d973a14`，包、既有解压副本与 `build/field-fixes-20261005/B` 一致；Claude `build-6.log` 记录实际构建完成。本轮没有冒称新构建。
- 独立审核 ZIP CRC/全部 1578 个载荷 SHA、源码 ZIP 和 123 个运行组件原字节通过；继承的两次基础/内嵌 ASS 启动记录在 `DELIVERY-2.0.3-field-20261005b.json`。`b` 是包修订号，程序编译时显示标签仍为 `2.0.3-field-20261005`，manifest 显示标签记录为 `...05b`，这项身份差异明确保留，不能用界面标签判断是否拿到最新 EXE。

### 本轮实际验证

Run：`takeover-20261005T061823Z-1ad4ac`。以下路径均在 `E:/项目/Veyra/`：

- 日志：`logs/claude-handoff-20261005/takeover-20261005T061823Z-1ad4ac/`；
- 独立测试副本及配置：`tests/claude-handoff-20261005/takeover-20261005T061823Z-1ad4ac/`；
- 夹具及子进程 TEMP/TMP：`tmp/claude-handoff-20261005/takeover-20261005T061823Z-1ad4ac/`；
- 开始状态、逐文件 hash 和接管前原文：`archives/claude-handoff-20261005/takeover-20261005T061823Z-1ad4ac/`。

命令为 `py -3.11 -B <本轮tmp>/handoff-check.py <run> audit|units|timing`，分别保存 `package-audit.json`、`unit-results.json`、`timing-results.json` 与原始日志。五个相关测试程序全部 exit 0：scene 18/18，live timing、effect chain、repair contract 246/246、UI i18n。`git diff --check` 通过。

使用第二版包的独立副本，仅在副本注入测试 QML，真实播放约 45 秒，串行执行七阶段：效果全关 → 播放中开 NR → 关 NR → 再开 NR（日志确认 feature-cache 命中）→ DLSS SR → DLSS 2X → 第二层 NR。所有已开启阶段的 UI GPU 计时均 measured、有真实样本；增强合计约 7.22 / 7.07 / 8.18 / 10.43 / 16.85 ms。关闭效果时零增强耗时正确，不要求禁用阶段有数值。exit 0，无 ERROR/FATAL/QML 错误；这组证明计时显示恢复，不是匹配 A/B 性能收益测量，不外推为物理延迟或全部组合通过。测试进程已退出，未跑压力或竞争负载。

### 剩余事项与下一步

1. **下一项本机工作：** 独立 OBS 兼容模式下，先暂停再开始游戏采集，核验实际录像。Claude 的 P2-1 仍只有静态推断，不能先记修复或通过；若真实复现，再最小修复并做暂停/恢复回归。
2. RX9000 用户实际 NR 推理/画质及超预算导出回退、受影响用户后台采集卡卡顿，需要实机日志，当前 NVIDIA 包无法证明；不恢复被拒绝的压力测试。
3. 其他 RTX 上 NGX 核心/最近图缓存生命周期未验；Auto 档与先粗后细的主观画质未获批准。公开发布前须处理，不能把 5070 证据外推。
4. libass 既有 ASCII 构建路径和 vcpkg 缓存越出约定产物根，仍为 Claude 历史残留；后续重建先处理路径与依赖来源，不能继续默认向外写，也不为整理而移动/删除原件。

当前交付是本地测试包。合并 main、push、Release、删除旧 UI 和新的功能优化不由本轮接管自动授权。
