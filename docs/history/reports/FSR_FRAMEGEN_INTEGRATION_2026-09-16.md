# AMD FSR 帧生成接入记录（隔离分支，2026-09-16）

分支 `codex/framegen-fsr-dolby-20260916`。**2026-09-16 已合并进 main（merge `24e7de7`）；未推送、未发布。** 施工依据
`docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md`。

## 1. 结论（先说能做什么、不能做什么）

| 项 | 状态 | 证据 |
| --- | --- | --- |
| AMD FSR 帧生成 3.1.x 接入播放器 | **可用，2X** | 本机 RTX 5070 玩家实测 559 真实帧 / 555 生成帧，exit 0 |
| 3X / 4X（请求 2–3 张生成帧） | **3.1.x 提供方不支持** | 探针实测：请求 2 或 3 张，回调仍只收到每真实帧 1 张生成帧 |
| FSR 4.0.1 ML 提供方 | **本机 NVIDIA 上未被枚举** | 探针版本枚举只有 3.1.7 / 3.1.6；需 AMD 卡复测，未宣称可用 |
| 与 DLSS / XeSS 并存 | 三者互斥，UI 单选 | `--fg-dlss` / `--fg-xess` / `--fg-fsr`，settings 预设 schema v13 |

**没有验证的东西**：AMD 显卡上的实际表现（本机只有 NVIDIA 适配器）、HDR10 输出下的 FSR 帧生成、
采集卡实时输入的 FSR 帧生成、屏幕实际扫描输出的均匀性。以上都不得对外当作已完成。

## 2. 实现结构

- `include/veyra/gfx/FsrFgPresenter.h` / `src/gfx/FsrFgPresenter.cpp`
  - 加载 `runtime_local/amd/framegeneration/amd_fidelityfx_loader_dx12.dll`，显式解析
    `ffxCreateContext/ffxDestroyContext/ffxConfigure/ffxQuery/ffxDispatch`；
  - `ffxCreateContextDescFrameGenerationSwapChainForHwndDX12` + 版本描述
    （`FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION` = 3.1.7）创建**代理交换链**；
  - `ffxCreateContextDescFrameGeneration`（+ `ffxCreateBackendDX12Desc` 设备、+ 版本描述）
    创建帧生成上下文；`maxRenderSize = max(交换链尺寸, 工作分辨率)`；
  - 每帧 `ffxDispatchDescFrameGenerationPrepareV2`（运动/深度）→
    `ffxConfigureDescFrameGeneration` → `ffxQuery...InterpolationCommandList/Texture` →
    `ffxDispatchDescFrameGeneration`；随后照常 `Present`，插帧由提供方在 present 路径完成；
  - 自带 present 回调：统计真实/生成帧，并在没有 UI 资源时执行提供方默认的
    「替代缓冲 → 真实交换链缓冲」拷贝（与 FSDK 默认回调等价）。
- `PresentSink`：`desc.fsr` 时用代理交换链替换自建交换链；代理**跨会话保留**（见第 4 节）。
- `VideoPresenter`：在既有 XeSS 打点位置对 FSR 打点，使用同一份
  `presentMotion`（工作分辨率像素、current→previous 约定）与 guidance 深度。
- `EnhanceGraph`：`presentSinkFg() = XeSS || FSR` 统一驱动 NVOF 执行与 guidance 纹理分配；
  两者都不走图内 DLSSG。
- 引擎/UI：`FrameGenerationBackend::Fsr`、能力上限 2X、设置项「AMD FSR 帧生成 · 2X」、
  `--fg-fsr`、导出明确拒绝（与 XeSS 相同：交换链插帧没有编码器纹理输出合同）。

## 3. 三个必须记住的 API 事实（都踩过）

1. **版本描述必需**：交换链上下文必须挂 `ffxCreateContextDescFrameGenerationSwapChainVersionDX12`；
   帧生成上下文的版本描述可省略（探针里两种情况都成功）。
2. **必须先 Configure 再 Query**：交换链只在自身 `frameGenerationEnabled` 为真时才返回插帧命令列表，
   顺序反了会拿到空命令列表，`ffxDispatch` 返回 `ERROR_RUNTIME_ERROR`（本地复现并修复）。
3. **`intra-frame` 尺寸关系**：`prepare.renderSize` 必须 ≤ 创建时的 `maxRenderSize`；
   播放器的窗口缓冲常小于工作分辨率（contain 缩放），所以 `maxRenderSize` 取两者较大值。

## 4. 已知约束：代理交换链必须跨会话保留

`ffxDestroyContext` 之后，提供方**没有释放**真实 DXGI 交换链（探针实测：销毁后为同一个 HWND
再创建交换链返回 `ERROR_RUNTIME_ERROR`，且代理对象释放前引用计数为 3）。
DXGI 规定一个 HWND 同时只能有一个交换链，因此：

- `PresentSink::shutdown()` **不销毁** FSR 上下文，只释放自己的交换链引用；
- 下一次 `initialize()` 复用同一代理（`using the retained AMD proxy swapchain`），
  不使用帧生成时通过 `Configure(frameGenerationEnabled=false)` 把它当普通交换链；
- 这样设置切换（例如开超分、换补帧方式）不会因为窗口被占用而失败；实测设置事务重建通过。
- 代价：一次 FSR 会话之后，该窗口的 FSR 上下文常驻到进程退出（约数十 MB 级，
  取决于窗口/渲染分辨率）。这是提供方引用计数的取舍，不是我们主动选择的行为。

## 5. 复现证据（全部在本机 RTX 5070 / 616.56）

| 命令 | 结果 | 日志 |
| --- | --- | --- |
| `veyra_fsr_probe <loader> 1 upscale` | 90 帧、89 生成帧、0 失败 | `logs/fsr/probe-recreate.log` |
| `veyra_fsr_probe ... 1 upscale pipelined` | 同上（无每帧 GPU 等待） | `logs/fsr/probe-pipelined.log` |
| `veyra_fsr_probe ... 3` / `... 2` | 请求 3/2 张，回调仍只收到 1 张/真实帧 | `logs/fsr/probe-4x-20260916.log`、`probe-3x-20260916.log` |
| `veyra.exe --fg-fsr --smoke-seconds 8 <1080p>` | 226 真实 / 222 生成，exit 0 | `logs/fsr/smoke-fsr-final.log` |
| `veyra.exe --fg-fsr --smoke-settings --smoke-seconds 12 <1080p>` | 设置事务重建后继续 2X，exit 0 | `logs/fsr/smoke-fsr-rebuild2.log` |
| `veyra.exe --fg-fsr --smoke-seconds 10 <4K>` | render 3840×2160 → display 1280×712，559 真实 / 555 生成，exit 0 | `logs/fsr/smoke-fsr-4k.log` |
| 回归：`--fg-multiplier 6` | 177 真实 / 875 生成（5 张/帧），exit 0 | `logs/fsr/regress-dlss6x.log` |
| 回归：`--fg-xess --fg-multiplier 4` | 217 真实 / 639 生成，5/5 补丁回滚，exit 0 | `logs/fsr/regress-xess4x.log` |
| `scripts/gates/delivery.ps1` | PASS 23/23 | `logs/delivery/4f387d93def9440f8fa9f7efe92d6bd3/result.json` |

单元/契约：`veyra_repair_contract_tests` 157 项 0 失败；`veyra_repair_preset_tests` 60 组迁移 +
全字段往返 + FSR/XeSS 上限校验全部通过（此前因 XeSS 4X 放宽而失效的旧断言已按现合同修正）。

## 6. 与 DLSSG 的运动矢量约定差异（第一性原理）

- Veyra guidance 运动矢量：**current→previous、工作分辨率像素**。
- DLSSG 期望 (current−previous) 归一化，所以 `DlssFgBackend` 传 `mvecScale = -1/w`。
- FSR 帧内插重投影是 `previous_uv = current_uv + mv`（`ffx_frameinterpolation_reconstruct_previous_depth.h`
  与 `ffx_frameinterpolation.h` 中 `fReprojectedUv = fUv + fMotionVector`），与 Veyra 约定同向，
  因此 FSR 侧 `motionVectorScale = (1,1)`（存储值已是像素），不得再加负号。
- 深度使用图内 guidance 深度（R32F）。它不是引擎原生深度，遮挡处理能力有限，这一点不粉饰。

## 6.1 生成帧真的带运动吗（E-1 门槛的"真实运动"）

`veyra_fsr_probe ... motion` 模式：黑底 + 白色方块，每帧右移 40 像素，插帧目标回读后
用方块**中心**（不是边缘，插帧后边缘会变软）与理论中点比较。本机实测
（`logs/fsr/probe-motion.log`）：

```
realCenter=1060.0  previousCenter=1020.0  expectedMidpoint=1040.0
interpolatedCenter=1041.5 (left=1006 width=72)  midpoint=1 notDuplicate=1 notOlderFrame=1
```

即生成帧里的方块位于两真实帧的正中间（误差 1.5 像素），既不是重复帧也不是外推帧。
这同时反向验证了运动矢量的符号约定：符号错了方块会出现在错误一侧或偏移加倍。

节奏（同一探针的 present 回调时间戳，**提供方提交节奏，不是屏幕扫描实测**）：
47 个间隔，均值 9.52ms、最大 15.57ms、<5ms 的间隔仅 3 个 → 没有成串突发。

## 7. 归属

- AMD FidelityFX SDK 2.3.0（MIT）：头文件与签名运行库仅本地使用，
  `third_party_local/amd/FidelityFX-SDK-2.3.0`（gitignore）、`runtime_local/amd/framegeneration/`。
  接入代码是按 SDK 文档与示例独立编写，未复制 SDK 源码到产品源码树。
- 参考实现：`Kits/FidelityFX/docs/techniques/frame-interpolation-swap-chain.md`、
  `Samples/Upscalers/FidelityFX_FSR/dx12/fsrapirendermodule.cpp`。
