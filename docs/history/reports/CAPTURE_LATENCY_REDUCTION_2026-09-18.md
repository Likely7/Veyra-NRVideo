# 采集 DLSS 成对呈现延迟优化

状态：本机历史版本同参数对照、候选构建与针对性回归完成；仅确认约 2ms 软件延迟收益，偶发长帧未解决，候选不代表全面验收。基准 `410061a`，存档 `checkpoint/pre-capture-latency-reduction-20260918`，沿用隔离分支 `codex/frame-pacing-20260918`，不合并或发布。

## 依据与范围

上一轮 VC-007PRO 4K30 NV12、NR 实时 1080p、DLSS 4X 实测原帧回调到呈现返回约 42.2ms；原帧就绪约 10.1ms，随后等待约 31.8ms。旧策略以源周期加基础 GPU 成本安排 B 的目标时刻。4X 的三张插值必须位于 A/B 之间且顺序呈现，不能将 B 提前插到它们前面冒充降延迟。

本轮只优化物理采集的自有 DLSS 呈现相位。保留倍率、PTS、画质、音频和资源 fence；文件、导出、PS5 与 XeSS 提供方时序不变。

## 实现

从既有 CPU fence 观测取得每个已完成有效批次的 `max(ready_i - arrival_B + pts_B - pts_i)`。它是保持原子帧间隔时所需的 B 延后量；CPU 观测是保守上界，不冒充 GPU 时间戳。

下一批提交前选择最近一秒、最多 64 批的 P95，加 1ms 唤醒余量。至少 8 个有效批次才开始缩短；每批缩短不超过 0.25ms 且不超过一个子帧间隔的 1/16，增加则立即跟随更保守的估计，最多恢复到旧策略。没有当前批次完成后重锚、隐藏降倍率、额外 fence wait 或丢帧抵债。

设置变更与显式排空清除估计，预热/恢复批次不训练；过期样本回到旧策略，旧呈现 generation 不参与新估计。诊断环境变量 `VEYRA_TEST_LEGACY_CAPTURE_PHASE` 仅用于同构建基线对照。

## 验收计划

单测覆盖 2X/4X/6X、缺测、慢帧、样本老化、逐步前移和重置。实际编译产品和测试程序，先短测，再固定实卡配置各 120 秒对照旧/新关闭模式与低排队，检查延迟、实际提交间隔、丢帧、补帧受限和递增延迟。只有收益与节奏证据同时成立才保留。所有运行均不超过 300 秒。

产物：构建沿用 `E:/项目/Veyra/build/frame-pacing-20260918`；本轮测试/日志分别为 `E:/项目/Veyra/tests/capture-latency-reduction-20260918`、`E:/项目/Veyra/logs/capture-latency-reduction-20260918`；临时目录沿用 `E:/项目/Veyra/tmp/frame-pacing-20260918`。不生成便携包。

物理 HDMI/卡内部/USB 和显示扫描未测；场景由当前主机提供，不能把静态主界面当作快速游戏验收。

## 同构建实卡对照

固定 VC-007PRO 3840x2160 30fps NV12、NR 内部 1080p、DLSS 4X 输出 4K、SR 关闭、音频入口开启但输出静音，RTX 5070 / 616.56。每轮预热 10 秒再正式采样 120 秒。窗口 1280x760、显示器 2560x1440 / 100Hz、允许撕裂，与上一轮相同。旧/新使用同一二进制，仅旧组设置进程环境变量绕过相位估计。

| 指标，毫秒 | 旧策略 / 关闭 | 新策略 / 关闭 | 新策略 / 低排队 |
| --- | ---: | ---: | ---: |
| 原帧回调到 Present 返回 P50 | 42.439 | 40.233 | 40.057 |
| P95 | 42.788 | 40.857 | 40.427 |
| P99 | 42.930 | 41.557 | 40.737 |
| 最大 | 43.230 | 42.466 | 41.278 |

同模式前后 P50 降低 2.206ms（5.20%）；低排队相对新关闭的差别很小，不能解释为普适模式收益。三轮 NR/FG 始终激活，正式窗新增采集 dropped、FG skipped、FG expired、command slot wait 全为 0，实际约 29.97 回调/秒、119.88 次 Present 提交/秒。100Hz 面板不会完整显示每秒 120 张画面。

旧/新关闭组的提交间隔 P01/P50/P95/P99 分别为 5.424/8.375/10.684/11.118ms 和 7.827/8.359/8.716/8.866ms；两组没有小于目标一半或大于目标 1.5 倍的间隔。新组首/末 30 秒延迟 P50 为 40.248/40.217ms，没有延迟积累。本轮观测到间隔波动缩小，不能推广为全部内容或机器都更平滑。

预热阶段仍有 provider warmup/跳过，正式窗开头约 0.85 秒保留既有受限标签（样本约 0.7%），正式窗没有新增跳过。未将启动标签隐藏或统计清零。

### 为什么没有省掉 32ms

原帧就绪后等待约 32ms，其中大部分是 A/B 补帧按 PTS 顺序排布的时间。第一张补帧已经拥有独立的 GPU fence，无须等第二、第三张算完。新关闭组第一张补帧就绪观察 P50 为 13.104ms、呈现为 15.222ms；再排三段约 8.34ms 后才到原帧 B，约 40.23ms。直接提前显示 B 会破坏时序，不能算同质量、同倍率的优化。

用户要求保持采集方案，继续对比加入帧同步前的真实 1.4.2beta。停止 60fps/MJPEG 替代试验，移除相应测试入口。上表的“旧策略”只是在新版本中关闭相位优化，不能当作历史版本对照。

## 历史版本对照方法

从 `checkpoint/pre-frame-pacing-20260918` / `6e69eeb` 创建 `codex/beta-latency-ab-20260918`，工作区 `E:/项目/Veyra/worktrees/beta-latency-ab-20260918`。将其 `src/`、`include/` 与原始 `Veyra-1.4.2beta-source.zip` 对比，220 个源码/头文件一致（仅统一 CRLF/LF）。历史引擎保持原样，只加入共同采样程序及 CMake 测试目标。

两版编译同一 `CaptureLatencyComparisonTests.cpp`，复用相同 NR/FG/SR 原件与 NGX 配置，关闭逐帧详细日志。每轮预热 10 秒，正式 120 秒；固定 VC-007PRO 4K30 NV12、NR 内部 1080p、DLSS 4X、SR 关闭、WASAPI 采集且输出静音、1280x760 窗口、100Hz 显示器，默认同步关闭。通过旧版已存在的 snapshot 读取计时，每 50ms 采样，只保留原帧 Present 计数变化的快照。统计是**抽样快照分布**，不是全帧统计，也不是 HDMI 到屏幕。

三组分别为历史 beta、新版同步关闭且绕过本轮相位优化、新版同步关闭且启用相位优化。最后重复历史 beta 检查顺序/负载漂移。此对照使用历史源码重新编译的产品引擎，不冒充原便携 GUI 二进制的外部端到端测量。

历史构建：`E:/项目/Veyra/build/beta-latency-ab-20260918`；证据：`E:/项目/Veyra/tests/beta-latency-ab-20260918`；临时：`E:/项目/Veyra/tmp/beta-latency-ab-20260918`；构建日志仍在本轮 `logs/capture-latency-reduction-20260918`。第一次启动缺少新测试目标的 PresentBlit shader 依赖和本地 NGX 配置，失败证据保留于 `beta120`；补齐测试目标依赖与配置后重新开始，失败启动不参与正式结果。

### 执行命令

在历史工作区执行：

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/beta-latency-ab-20260918 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/beta-latency-ab-20260918 -DisplayVersion 1.4.2beta -Targets veyra_capture_latency_tests
```

在当前工作区执行：

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/frame-pacing-20260918 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/frame-pacing-20260918 -DisplayVersion 1.4.2beta -Targets veyra_capture_latency_tests,veyra_presentation_pacing_tests
./scripts/test-capture-version-comparison.ps1 -Build E:/项目/Veyra/build/beta-latency-ab-20260918 -Name beta-a120
./scripts/test-capture-version-comparison.ps1 -Build E:/项目/Veyra/build/frame-pacing-20260918 -Name current-off120 -LegacyPhase
./scripts/test-capture-version-comparison.ps1 -Build E:/项目/Veyra/build/frame-pacing-20260918 -Name candidate120
./scripts/test-capture-version-comparison.ps1 -Build E:/项目/Veyra/build/beta-latency-ab-20260918 -Name beta-b120
python scripts/acceptance/analyze-capture-version-comparison.py E:/项目/Veyra/tests/beta-latency-ab-20260918 beta-a120 current-off120 candidate120 beta-b120
```

共同测试源 SHA256：`129F56C6EC5B705BBF95CC6C76D1E6BE3CDB474568A291D22905E56F9BC2B9B1`。源码审计和运行库身份分别保存于 `source-audit.json`、`runtime-identities.json`。旧引擎新增测试 target 包含独立 PresentBlit shader 依赖；NVIDIA/AMD/Intel 目录复用已批准运行库，根目录 FFmpeg DLL 沿用当前已打补丁依赖，本地 `ngx-local.json` 同源复制。没有读取或覆盖用户 UI 偏好。

### 已完成恢复回归

本轮 `recovery/result.txt`：实卡注入 80ms 工作延迟后报告 mailbox 丢帧，撤销后恢复 NR/完整补帧且没有持续积累延迟；暂停/恢复、2X→6X→4X 均恢复真实 NR/FG，正常停止。单测覆盖相位范围/过期/重置，`unit.log` PASS；修复契约 `contracts.log` 205 项 0 失败。RTX 30/40 未在本轮执行，不以 5070 结果代替。

## 历史对照结果

实际顺序为旧 beta A → 新版关闭且禁用相位优化 → 新版关闭且启用相位优化 → 旧 beta B。四轮均预热后正式运行 120 秒并 exit0，NR 与 DLSS4X 全程激活。软件回调约 29.97fps，提交读数中位数 120fps；不代表 100Hz 面板显示 120fps。

| 抽样软件延迟，ms | 旧 beta A | 新版关闭/未优化 | 新版关闭/优化候选 | 旧 beta B |
| --- | ---: | ---: | ---: | ---: |
| P50 | 42.578 | 42.631 | 40.496 | 42.683 |
| P95 | 43.412 | 43.503 | 42.829 | 43.392 |
| P99 | 45.179 | 44.171 | 43.662 | 44.437 |
| 最大 | 317.326 | 172.015 | 56.673 | 45.045 |
| 首 30 秒中位数 | 42.523 | 42.480 | 40.574 | 42.485 |
| 末 30 秒中位数 | 42.808 | 43.245 | 40.452 | 43.027 |
| 正式窗新增采集丢帧 | 23 | 5 | 0 | 0 |
| 正式窗新增 FG 提前跳过 | 3 | 0 | 0 | 0 |
| 正式窗新增生成帧过期 | 18 | 5 | 8 | 4 |
| command slot 等待 | 0 | 0 | 0 | 0 |

结论：**新版关闭模式的软件延迟落在两次旧 beta 中位数之间，未测出新增帧同步带来的明显基础延迟回归。** 优化候选比新版未优化下降 2.135ms（5.01%），与本轮前一组逐帧日志对照的约 2.2ms 收益一致。没有改变分辨率、采集格式、倍率或 NR 档位。

尾部不能省略：旧 beta A 正式窗约 87.5/87.8/92.4 秒出现丢帧与 276–317ms 延迟；旧 beta B 没有重现同级尖峰。新版未优化也有 172ms 抽样尖峰。优化候选仍过期 8 张生成帧，不能宣称消除了卡顿，也不能凭单轮更小的最大值断言稳定性修复成功。本机存在其他运行中的应用，未关闭用户进程；缺少系统级调度跟踪，无法把尖峰武断归因于应用、驱动或外部负载。受限状态的快照比例依次为 1.483%/0.719%/0.718%/0.719%，含正式窗开头继承的预热标签；各计数增量如实保留。

### 阶段交叉核对

以下是旧版已有 rolling-mean 指标的快照中位数，不是逐帧阶段总和，也不与前一组完整日志统计混用：

| 阶段，ms | 旧 beta A | 新版关闭/未优化 | 优化候选 | 旧 beta B |
| --- | ---: | ---: | ---: | ---: |
| 回调到 read | 1.066 | 1.047 | 1.098 | 1.038 |
| GPU 颜色/上传 | 0.193 | 0.196 | 0.193 | 0.195 |
| GPU 光流 | 1.064 | 1.070 | 1.083 | 1.089 |
| GPU NR | 6.270 | 6.297 | 6.296 | 6.292 |
| GPU 残差合成 | 0.258 | 0.262 | 0.262 | 0.271 |
| GPU FG 批次 | 5.836 | 5.904 | 6.137 | 6.138 |
| 增强阶段区间 | 13.425 | 13.507 | 13.770 | 13.787 |
| 每次呈现 CPU 服务 | 0.268 | 0.246 | 0.251 | 0.254 |

NV12 为原生像素输入，无 H.264/HEVC/MJPEG 压缩解码。旧版已有 `capture-pair` 时序与 A/B 插帧排布，约 42ms 不由新选项凭空产生；优化来自减少保守呈现余量，GPU 运算没有更快。CPU/GPU 异步重叠、三张生成帧与原帧按 PTS 排布，这些阶段不可简单相加为物理总延迟。

四轮日志均真实创建 NR Feature18 与 DLSSG，Create 返回 `0x1 seh=0`；DLSSG warm-up Evaluate `ok=1 result=0x1`，后续 NR/FG 计数持续增加，正式运行无 ERROR。详细 Evaluate 逐帧输出在本次历史 A/B 统一关闭，不能编造逐帧返回码证据；前述同构建详细日志保留。

## 交付与边界

产品引擎修改为 `LivePairLatency.h` 与 `EngineController.cpp`；单测/恢复场景、共用历史对照探针、CMake 目标、两个分析脚本和对照启动脚本负责验收，文档更新到 WORKLOG/CURRENT_STATUS。本轮产品构建记录 `build.log`，共同探针构建 `build-current-ab.log`、`build-beta-ab.log`、`build-beta-ab-repair.log`，结果 CSV/JSON、失败启动与四轮日志均保留。未新增 SDK/运行库到 Git；未打包、合并、推送或发布，原 beta 包未改。

这里只完成采集 callback→软件 Present 返回比较；卡内部/USB 前半段、Present 后显示队列和扫描均未测，因此不能排除用户体感在未测阶段的差异。下一项唯一任务是对偶发长帧作同源可重复归因，并补齐实际屏幕端对照，不能用改采集格式替代。
