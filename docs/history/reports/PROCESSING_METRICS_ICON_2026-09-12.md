# 增强处理耗时面板与应用图标

2026-09-12。用户确认主面板显示算法处理耗时，将原先的额外显示延迟指标放到小三角详情第一项，并使用提供的手柄 Logo。开工前存档 `7a30b66`，分支 `codex/processing-metrics-and-app-icon`。

## 统计契约

- 主曲线、大数字：总增强处理耗时，最近一秒平均；曲线保留约30秒。每个完成的源帧/补帧批次先计算一个总值，再求均值和P95。
- 包含同一图记录的 Flow、NR、SR、Residual、FgBatch GPU timestamp 区间；重叠区间求并集，避免重复统计。排除区间间隙，不以最早开始到最晚结束冒充算法计算耗时。
- FG使用完整批次，不再加Fg1/2/3，不除以补帧倍数。单项均值按执行样本统计；总计按源帧统计，未执行阶段为零，因此间歇补帧时不能强求单项均值之和严格等于总计。
- 不含输入颜色、最终输出合成、音频、解码、CPU等待和呈现等待。光流时间戳当前包围跨队列依赖等待，是实际测得的阶段区间，不代表仅光流硬件核心的执行时间。此边界在详情中说明。
- XeSS内部FG没有对应计时：主标题和页脚明确“不含XeSS FG”，单项继续显示不可测，不伪装为完整增强总时间。
- Presenter只提供Blit的记录不能作为零值混入平均；完整图记录由Color计时标识。异常/不可测区间不显示为零；旧revision/epoch记录不进入新窗口；过期样本显示未测。
- 原 `EnhancementDelayEstimate` 保留计算，仅移到详情第一行：文件显示“画面迟到·估计”；采集/PS5显示“额外显示延迟·估计”。文件参考音频/媒体播放时钟，实时路径参考解码后驻留减基础开销；未开启增强仍沿用原来的零基线。不是屏幕扫描实测，也不是增强GPU总耗时。
- 未改音频同步、播放时钟、呈现队列或跳帧策略。`FILE_OVERLOAD_LATENCY_REPAIR_PLAN_2026-09-12.md` 中预测选帧等P1/P2仍未实施，不能把本次显示口径修订称为过载性能修复。

## 图标

用户提供的 `1fb0cbee-feae-49c7-bd1a-2e27ec09f940.png` 原样缩放转换成 `assets/veyra.ico`，含16/24/32/48/64/128/256尺寸，保留白底与原图构图。ICO作为RC资源嵌入EXE，无外部图片路径依赖。应用大/小窗口图标与专业模式左侧品牌位共用此资源，资源变动触发重编译。

## 修改位置

- `EnhancementProcessingTime.h`、`FrameMetrics.h`、`FrameFlowWindow.h`：关联GPU区间聚合。
- `EngineController.cpp`：日志同时输出 `enhancementProcessingMs` 与 `extraDelayEstimateMs`，保留两种口径供排查。
- `LiveStatusDashboard.h`、`LiveStatusPanel.h`：主曲线、卡片耗时命名、详情首项及范围说明。
- `BrandIcon.h`、`AppShell.cpp`、`resource.h`、`version.rc.in`、`CMakeLists.txt`、`assets/veyra.ico`：程序与品牌图标。
- `RepairContractTests.cpp`：验证区间去重、批次成本、无关记录隔离、均值/P95、失效样本等。

## 验证记录

- 构建：`cmd /c out\remoteplay\build-extra-delay.cmd`，首次95步构建日志 `logs/processing-metrics-build.log`，品牌图标完成后二次增量构建 `logs/processing-metrics-final-build.log`，均成功。编译保留已有FFmpeg窄化等警告，无构建错误。
- 合同回归：`out/remoteplay/product-repair/veyra_repair_contract_tests.exe`，`logs/processing-metrics-contracts.log`：98 checks / 0 failures（含本次新增8项）。
- 4K素材180秒短测：`veyra.exe --smoke-seconds 180 --nr --video-sr 2 --fg --smoke-view professional <GTAVI_An_Extended_Look_4K_Native.mp4>`，`logs/processing-metrics-smoke.log`。5275帧、5251生成帧、failed=false，NR Evaluate计数5275、NVOF5274；实时NR内部约1080，4K输入本次SR自动旁路，不冒充SR生效测试。后台隐藏启动，因此此组不作为可见UI验证。
- 最终构建的可见短测：`veyra.exe --smoke-seconds 100 --nr --video-sr 2 --fg --smoke-view professional logs/ps5-p1-1080p30-long.mp4`，`logs/processing-metrics-visible.log`。1080文件源→4K、实时NR→DLSSG 2X。Feature18 Create及DLSSG Create均 result=0x1、seh=0，FG warm-up Evaluate result=0x1。结束记录1796源帧、1794生成帧、failed=false；片长60秒后正常EOF，100秒结束时的processedFps=0不是运行欠速。稳态日志例 enhancementProcessingMs=11.942、extraDelayEstimateMs=0.343，实际SR P95=2.406ms，未将合成色条文件测试称为PS5实机测试。
- Windows Computer Use实际激活Veyra检查：主面板约12.1ms，四卡约1.1/6.1/2.0/2.6ms；点击小三角后第一项“画面迟到·估计”约0.5ms；切回后曲线继续更新。左上品牌位可见用户Logo。未修改个人配置（smoke模式）。
- Windows Shell ExtractIconExW查询EXE得到1组图标；ICO包含16/24/32/48/64/128/256七种尺寸。资源文件93600字节，SHA256 `773E20831B2BD62FBEC80A22C68F24EB6AA49D2DAD16D8A30484142AACE2C026`。
- 最终EXE：`out/remoteplay/product-repair/veyra.exe`，SHA256 `55849CBB28FF3D65AC0BE49FF49617EAD4FDB9B1136E72CE787BB117B57A9B28`。现有桌面“Veyra PS5 测试版”快捷方式指向此路径。
- 本轮未执行实卡、PS5连接、XeSS内部计时或外部机器验收。没有新增SDK/运行时文件入Git。git diff --check通过（仅行尾规范提示）。

本轮不推送GitHub、不改变0.0.5版本号、不替换Release资产。
