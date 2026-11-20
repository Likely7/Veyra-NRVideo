# PushFPS 推荐功能移植（2026-11-19）

来源：PushFPS（https://github.com/DosukaSOL/PushFPS，GPL-3.0-or-later），它带着 Veyra 完整历史，从 Veyra main `8a690f5d`（2026-11-15）分出。审查与取舍见 `reports/claude-pushfps-review-20261119/FINDINGS.md`；用户要求把“建议直接移植”的全部并入。
只读副本 `downloads/claude-pushfps-review-20261119/PushFPS`（origin/main `3be641ca`），未构建、未运行其中任何东西。

许可：新文件保留对方 SPDX 与版权行，并写明移植日期、来源文件、提交和改动（改名、接入 Veyra 设置/引擎/界面）；`THIRD_PARTY_NOTICES.md` 顶部列出全部来源。类名、文件名、日志标签去掉 PushFPS 前缀，界面文字按 Veyra 用语重写。

## 移植内容

| 功能 | 文件 | 默认 | 说明 |
|---|---|---|---|
| 输出上限降倍改用内容帧率（修复） | `EngineController.cpp` | — | 原来按采集 `averageFps`（60Hz 信号为 60）算，去重后 30 帧游戏在上限 120 时被降成 2X；现在用去重实测内容帧率（未测出前不降），60→30 按半速。PushFPS 9d4e61a2 |
| CUDA 计算缓存（修复） | `apps/veyra-qml/main.cpp` | — | `QApplication` 前在用户未设置时设 `CUDA_CACHE_MAXSIZE=4294967296`，并清掉非 0 的 `CUDA_CACHE_DISABLE`；日志 `CUDA compute cache maxSize=`。RTX 30 的 VFG 重定向内核从 PTX 编译，默认缓存放不下会每次会话重编卡住。PushFPS 9e9a4340 |
| 帧节奏修复 | `include/veyra/engine/FramePacingRepair.h` | 关 | 对去重保留帧的到达时间做 16 帧窗口最小二乘直线，把锁帧但 17/50ms 交替送出的帧排回均匀网格；只处理能解释为锁帧量化抖动的情况（斜率 ≥1.4 个采集刷新、全部点在 1.1 刷新内），卡顿/真变帧率保持原时间；最多晚或早一个采集刷新。只在补帧开启、非 XeSS、且启用自动去重时生效。日志 `pacing-repair`，状态栏“帧节奏修复 · 已重排 N%” |
| 自适应倍率 | `include/veyra/engine/AdaptiveFgMultiplier.h` | 关 | 所选倍率当上限，每对源帧按去重实测内容帧率选最接近目标（输出上限，未设上限时用屏幕刷新率）的整数倍，对数误差最小、平局取低，0.5 秒防抖；只改逐对倍率（复用已有逐对降倍），不重建补帧。DLSS/ANR/VFG 且倍率 >2 时可用；FSR/AMD FRC/XeSS 不提供。与已有「自动选择倍率」（每次开播按源帧率选一次档）可叠加。日志 `adaptive-fg`，状态栏“自适应 · 现在 NX” |
| HUD 自动锁定 | `include/veyra/pipeline/HudLock.h`、`src/pipeline/HudLock.cpp`、`shaders/HudLockUpdate.hlsl`、`shaders/HudLockCompose.hlsl` | 关 | 每个真实帧更新逐像素“静止时长＋邻域对比度”（R16G16F）；每个生成帧中静止约 1 秒且有边缘（含相邻一圈）的像素取自前一真实帧，羽化混合；像素一变立即释放。图内补帧（DLSS/ANR/VFG/FSR/AMD FRC）可用，XeSS 自行呈现不可用。开关会重建图（`EffectChain.cpp`）。状态栏显示运行/无法启用/当前补帧不支持 |
| 录制二次编码与心理视觉调优 | `NvencLiveEncoder`、`LiveRecorder`、`Recording.cpp` | 关 | 设置→录制：二次编码 关/四分之一/完整（NVENC `multiPass`），心理视觉调优（空间 AQ，显卡支持时加时间 AQ）。显卡拒绝时自动去掉这两项重开并记日志；Media Foundation（非 NVIDIA）忽略；即时回放判同也比较这两项 |

接线：`EnhancementSettings` 新增 `fgAdaptive`、`pacingRepair`（不参与图配置比较，切换只更新引擎每对读取的值、不重置 NR 历史）、`hudLock`（参与比较，切换重建）；`GraphDescription` 传 `desc.hudLock`；`QmlPlayerBridge` 新属性 `fgAdaptive`/`pacingRepair`/`hudLock`，保存在界面偏好；列表模式补帧卡「自动选择倍率」之后、节点模式「补帧运动来源」之前各三行；`Status.cpp` 状态文字；`i18n/catalog.json` 只追加 20 条（繁中/英/日/韩齐全）。

## 测试结果

原移植分支通过帧节奏和自适应倍率单测。HUD GPU 对照中静止 HUD 的 2400 个像素均取自真实帧，31364 个运动像素均保留，重置检查通过，D3D12 调试错误为 0。NVENC 六组编码测试各完成 30 帧并产出关键帧，包括 H.264 的五种组合和 HEVC 完整两遍加 AQ。

接手时 VFG 和 Video AI 设置测试均退出 2。两个程序要求传入尚不存在的绝对目录，用各自独立的新目录重跑，分别为 683 和 274 项检查、0 失败。原始失败见 `reports/claude-pushfps-ports-20261119/REGRESS.json`，补测日志在同目录的 `vfg_settings-verified.log` 和 `video_ai_settings-verified.log`。

在综合测试版内打开 VFG、自适应倍率、帧节奏修复和 HUD 锁定，对自建移动图案窗口做 Windows Graphics Capture。RTX 5070 上实际创建了 HUD 处理资源，帧节奏统计收到输入帧。切到传输帧率模式，以 60 FPS 输入和 120 FPS 输出目标测试自适应倍率，手动上限 4X 实际选择 2X。该 WGC 样本没有产生可判定的重复帧；节奏修复实际重排的验证来自单测。详见 `reports/codex-claude-handoff-20261119/APP-FEATURES.json`。

## 未验证

现场采集卡的节奏改善、RTX 30 冷缓存的编译耗时，以及 AMD/Intel 实机表现仍待硬件复测。本机 NVENC 结果限于编码可用性，没有测量相同码率下的主观画质。
