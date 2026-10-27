# AMD FSR 超分（F 工作流）：接入记录与实测结论（隔离分支）

**已接入产品并实测。** 分支 `codex/framegen-fsr-dolby-20260916`；2026-09-16 已合并进 main（merge `24e7de7`）；未推送、未发布。

状态一览：

| 项 | 状态 | 证据 |
| --- | --- | --- |
| FSR 3.1.5 超分接入图内 SR 阶段（1080p→4K） | **可用** | 播放器实跑 205–226 次 dispatch、0 失败；质量探针 19/19（`logs/fsr/smoke-fsrsr.log`、`logs/fsr/q-fsrsr-diag.log`） |
| 非 NVIDIA 路径形状（AMD FFX 光流 + FSR 超分） | **本机验证** | `--flow-amd --video-sr 5`：226 次 dispatch、0 失败、exit 0（`logs/fsr/smoke-fsrsr-amdflow.log`） |
| 输出内容正确性 | **验证** | 同帧 index=45、同 NR 设置、4K 对照：平均绝对差 **0.31/255**，平均亮度 115.98 vs 116.03（`tools/image_check/compare_sr.ps1`） |
| 相对画质 | **未胜出，如实记录** | 同一对照里梯度能量 0.563（直通缩放）vs 0.500（FSR SR），比值 0.888 → 本片段上 FSR 反而略软 |
| AMD 卡 FSR 4.1 | **不可用** | NVIDIA 上只枚举到 3.1.5 / 2.3.4，4.x 需 AMD 实机复测后另行开放 |

## 1. 用户问题的直接回答

> 「FSR超分能不能也加进来？N卡能用吗」

**能，而且本机已经用证据证明了**（RTX 5070 / 616.56）：

| 问题 | 实测结果 | 日志 |
| --- | --- | --- |
| N 卡能跑 FSR 超分吗 | 能。`CreateContext(upscale)` OK，`Dispatch(1280×720 → 2560×1440)` OK，回读是真图：均值 127.33、动态范围 0–255、15 个灰度级、棋盘相位正确 | `logs/fsr/upscale-probe.log` |
| N 卡上是哪个 FSR 版本 | **只有 3.1.5 与 2.3.4 被枚举**；4.x(ML) 提供方在 NVIDIA 上根本不出现 | 同上 |
| 所以 N 卡分档应该怎么写 | N 卡 = FSR 3.1.x（超分），4.1 ML 只能留给 AMD 卡实测后再开 | — |

工具：`tools/fsr_upscale_probe`（CMake 目标 `veyra_fsr_upscale_probe`），做法是上传
64 像素棋盘 + 水平渐变 → FSR 放大 2 倍 → 回读校验内容（不是"跑通不报错"就算过）。

## 2. 接入方式（已实现）

`include/veyra/gfx/FsrSrBackend.h` + `src/gfx/FsrSrBackend.cpp` 封装 FidelityFX 超分上下文与逐帧 dispatch；
`EnhanceGraph::runSr()` 里作为**新的第一分支**（`fsrSrEnabled() && haveFlow`）直接写 `workRgba_`，
没有有效运动的那一帧退回直通缩放而不是猜测；`EnhanceGraph::initFsrSr()` 负责创建，
`initNgxFeatures()` 在该会话只启用了 FSR 超分时**不再要求 NGX 核心**（这是它能跑在非 NVIDIA 卡上的前提）。

引擎/UI 侧：`videoSrQuality = 5`（`engine::kVideoSrFsr`，vendor neutral）作为新档位，
设置里出现"AMD FSR 超分 · 3.1.x（N卡可用）"，非 NVIDIA 归一化不再关掉这一档，
图创建后若 `fsrSrRequested() && !fsrSrEnabled()` 会走既有 `FailedBackend::Sr` 恢复（关档并提示），
不会静默变成直通。命令行：`--video-sr 5`（测试用 `--flow-amd` / `--flow-gpudis` 切换光流后端）。

原来的 SR 阶段结构（供对照）：

1. `videoSrBackend_`：RTX 视频超分（`videoSrQuality` 1–4），输入 `videoSrInput_`（SR 尺寸 RGBA8）→
   `videoSrOutput_`（工作尺寸 RGBA8）→ 回填 `workRgba_`；
2. `srBackend_`：DLSS SR（`videoSrQuality=0`），输入 `srcRgba_` + `nrZeroDepth_` +
   `baseFlow_`（工作分辨率像素运动）→ 直接写 `workRgba_`；
3. 都没有时走 `blitPass_` 直通缩放。

FSR 超分应作为**第 3 个后端**接入，语义与 DLSS SR 分支最接近，需要的输入是：

| FSR 输入 | Veyra 现有资源 | 备注 |
| --- | --- | --- |
| color（渲染分辨率） | `srcRgba_` | FP16 线性，比 FSR 要求的 R8/FP16 更好 |
| depth（渲染分辨率，可选） | 无源分辨率深度 | 现阶段传常量深度；SR 不依赖它做重建，只影响边缘处理质量，必须如实标注 |
| motionVectors（渲染分辨率，像素） | 源分辨率光流（`flowTex_` 在当前图上就是源分辨率像素运动） | 需要按 FSR 约定确认符号（见第 3 节） |
| output（展示分辨率） | `workRgba_` | 直接作为输出，省掉回填 pass |
| jitterOffset | 0（内容本身没有 jitter） | 不造假 jitter；代价是细节累积不如游戏原生 |
| preExposure | 1.0 | 我们的颜色已经是线性工作域 |
| sharpness / enableSharpening | 0 / false（默认关） | 与现有 SR 行为一致，避免双重锐化 |

上下文生命周期：SR 上下文按最大渲染/展示尺寸创建一次（`maxRenderSize = 源分辨率`、
`maxUpscaleSize = 工作分辨率`），窗口/分辨率变化走既有 rebuild 路径重建；
`reset` 直接复用现有 reset 语义（open/seek/scene cut/设置切换）。

引擎/UI 侧要做的最小改动：`videoSrQuality` 现在 0–4 全部是 NVIDIA 后端，需要新增
"FSR 超分"选项（N 卡走 3.1.5，AMD 卡走 4.1 需在 AMD 机器上实测后放开），
并放开 `gd.enableSr = plan.srApplied && nvidiaAdapter` 这个非 NVIDIA 硬门控
（只对 FSR 后端放开，DLSS/RTX 视频超分继续只给 NVIDIA）。

## 3. 限制与还没验证的点（必须如实保留）

1. **符号约定已核对**：FSR3 upscaler `ffx_fsr3upscaler_reproject.h` 里是
   `fReprojectedHrUv = fHrUv + fMotionVector`（previous = current + mv），与 Veyra 的
   current→previous 约定同向 → 传 `motionVectorScale = (1,1)`、像素单位，不加负号。
2. **常量深度的代价**：没有源分辨率深度，只能传常量远深度；边缘/遮挡处理弱于游戏原生路径，
   本文与 UI 都不宣称等同原生。
3. **HDR 不支持**：`hdrOutput` 时 FSR 超分档**主动不创建**（日志写明原因），不做没验证过的传递函数猜测。
4. **需要 flow 与源同尺寸**：实时档若把 NR/光流降到比源小（例如 1440p 源走 1080 光流），
   当前实现不走 FSR 超分（直接退回直通），因为 FSR 期望运动矢量在渲染尺寸。
   要支持就得先加一遍运动矢量重采样，不能糊过去。
5. **画质没有胜出**：本片段实测 FSR 3.1.5 比直通缩放略软（梯度能量 0.888 比值）。
   下一步可做的是把 FSR 自带的 RCAS 锐化（`enableSharpening`）做成可选并 A/B，
   以及用真实影视素材而不是合成测试片段复测；在那之前不得宣传"更清晰"。
6. **AMD 卡 4.1**：本机枚举不到 4.x 提供方，任何"4.1 AI 超分已支持"的说法都必须等 AMD 实机。
7. **GPU-based validation 不适用**：`--diag` 的 GBV 在 FidelityFX 超分 dispatch 上不完成
   （独立探针 `tools/fsr_upscale_probe --diag` 在 dispatch 前挂住，说明是验证层与提供方的交互），
   所以质量探针在 FSR 超分档会**显式跳过 GBV、保留调试层并打印警告**，不假装跑过。

## 4. 归属

与帧生成同一套运行库（AMD FidelityFX SDK 2.3.0，MIT），本地目录与 gitignore 规则见
[AMD FSR 帧生成接入记录](../reports/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md) 第 7 节。
