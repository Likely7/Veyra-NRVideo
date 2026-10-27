# 2026-09-16 导出失败两例修复 + XeSS/FSR 补帧导出（隔离分支）

分支 `codex/framegen-fsr-dolby-20260916`；2026-09-16 已随该分支合并进 main（merge `24e7de7`）；未推送、未发布。
用户指令原文："修，并且看看是不是用XeSS补帧没办法导出，也一起修了"。

## 1. 案例一：所有导出进程死在 NVENC 会话打开（用户 A）

证据（`C:\Users\123\Desktop\导出失败\export-worker-*.log`，5 个 worker 一致）：

```
[nvenc] CreateInstance status=0 detail=
[nvenc] OpenD3D12Session status=15 detail=
```

`status=15` = `NV_ENC_ERR_INVALID_VERSION`。同一日志里设备创建、NVOF、NGX、DLSS-G
能力查询全部正常，唯独 NVENC 会话按版本号被拒。机器为 RTX 5060，驱动
32.0.15.8157；用户重装驱动后现象消失，指向 System32 里 `nvEncodeAPI64.dll`
的身份/版本与驱动不一致。

修法（`src/sink/NvencD3D12Encoder.cpp`）：首次用编译期 `NVENCAPI_VERSION`
打不开时，按 13.0 → 12.0 → 11.0 降级重试（跳过刚失败的主版本），成功后在日志里
写明实际接受的版本。H.264/HEVC + D3D12 + 低延迟在 11.0 起全部可用，因此降级不改变
产品能力。

本机（RTX 5070 / 驱动 32.0.16.1656）复现不出"陈旧 DLL"，因此加了仅测试用钩子
`VEYRA_TEST_NVENC_FIRST_OPEN_FAILS=1` 强制跳过第一次打开，验证阶梯本身：

```
[nvenc] test-only: skipping the first OpenD3D12Session so the apiVersion ladder runs
[nvenc] OpenD3D12Session retry apiVersion=13.0 status=0
[nvenc] OpenD3D12Session accepted apiVersion=13.0 (compiled 13.0 was refused)
[export-verify] decoded=300 expected=300 eof=true cancelled=false passed=true
```

结论边界：阶梯已实现并验证可运行/可恢复，但"用户那台机器上的失败"本机无法复现，
需要该用户在实际发生的环境下用新包复测。

## 2. 案例二：99% 处整体失败（用户 B，两份 12MB worker 日志同点）

证据（`C:\Users\123\Desktop\BUG 导出失败 99卡死\export-worker-11628.log`）：

```
[media] demuxer: opened ... durationUs=854150000 timeBase=1/1000
[source-file] opened 1920x1080 dur=854.15s avgFps=60.000 container=matroska,webm codec=hevc
[export-timeline] CFR declared=60/1 candidate=60/1 timestampQuantum=0.001 sampled=120
[export-timeline] CFR rejected source=51247 pts=854.133 expected=854.1166666666667
```

854.15s × 60fps ⇒ 约 51249 帧，出错索引 51247 是流的最后几帧之一；偏差
16.33ms ≈ 恰好一个帧间隔 ⇒ **最后一帧时间戳晚一格**（容器/编码器尾帧取整或最后
GOP 截断）。`CfrTimeline` 的相位窗逐帧收紧，尾帧一格格子就被判为"不均匀"，25 分钟
的导出在最后 1 帧整体失败。

修法：

- `include/veyra/engine/CfrTimeline.h` 新增 `tailAccepts(index,pts,estimatedFrames)`：
  仅当该帧属于"容器时长 × 帧率"估算出的**最后 3 帧**、且偏差 ≤ 1.5 帧间隔 + 1 个
  量化步长时放行；估算不到总帧数（duration 缺失）时不放行。
- `src/engine/VideoExportJob.cpp`：拒绝时先走 `tailAccepts`，命中则计 `tailSnapped`
  并 warn；未命中仍硬失败，但文案改成可读的"源文件第 N 帧时间戳偏移 X 毫秒…已停止
  写入并保留 partial"。完成消息在发生尾部对齐时写明"尾部 N 帧已按恒定帧率对齐"。

为什么这样做是安全的：编码器写包用的是**输出帧序号**（`writer` 的 `pts=outputIndex`
再按输出时基重采样），输出本来就是严格 CFR 网格；尾部对齐只改变最后一帧的显示时长，
不移动任何已写帧，也不影响音轨交错（音轨按输出网格推进）。中间跳变仍走硬失败路径，
不能用尾帧通道蒙混。

## 3. XeSS / AMD FSR 补帧导出（用户新问题）

结论：**不是没接线，是取不到纹理。**

- XeSS-FG 3.0.2 头文件只有 `xefg_swapchain.h` / `xefg_swapchain_d3d12.h` / `xefg_swapchain_debug.h`；
  `libxess_fg.dll` 导出表里只有 `xefgSwapChain*`（`...CreateContext/InitFromSwapChain/TagFrameResource/
  SetPresentId/...`），没有任何"非交换链、输出到应用纹理"的入口。
- FSR 侧，FFX 的交换链上下文由 provider 自己持有代理链（`FsrFgPresenter` 只拿到
  `IDXGISwapChain4*`）。生成帧直接进显示链路。
- 因此导出管线（NVENC 取纹理编码）不可能拿到这两条路径的生成帧。旧实现直接拒绝整个
  导出任务，用户拿不到任何文件。

新行为（`VideoExportJob.cpp`）：

1. 请求 XeSS/FSR 补帧时，导出改用**图内 DLSS 补帧**（导出本来就要求 NVIDIA NVENC），
   开始前把替换关系写进进度提示与日志（`present-sink frame generation cannot feed the
   encoder requested=... ; substituting the in-graph DLSS path`）。
2. DLSS 初始化因倍率被拒时降级到 2X，并提示"本次导出降为 2X"。
3. DLSS 整体不可用（例如无 DLSS-G 的显卡）时，改成只输出原始帧并提示"补帧关闭"，
   任务仍然产出完整文件。
4. 三种结果都会出现在 `export-timeline` / `export-counts` / 完成消息里，不静默。

## 4. 本机验证（RTX 5070，全部逐帧解码验证）

测试素材（复现案例二）：`out/tail-jump.mkv` —— 300 帧 1080p30，MKV 1ms 量化，
最后一帧 PTS = 10.000s（网格应为 9.967s），由 ffmpeg `setpts` 构造。

| 命令 | 结果 |
| --- | --- |
| `veyra.exe out/tail-jump.mkv --export-out out/tail-jump-export.mp4 --no-nr --no-sr --no-fg` | exit 0；300 帧 @30fps；`tailSnapped=1`；`export-verify decoded=300 passed=true` |
| `... --fg-xess --fg-multiplier 4` | exit 0；1200 帧 @120fps；`backend=DLSS`、替换提示写入 note；逐帧验证通过 |
| `... --fg-fsr --fg-multiplier 2` | exit 0；600 帧 @60fps；`note=AMD FSR补帧由显示交换链直接生成…` |
| `VEYRA_TEST_FG_MULTIFRAME_MAX=1 ... --fg-dlss --fg-multiplier 4` | exit 0；4X→2X 降级；600 帧 @60fps |
| `VEYRA_TEST_FG_MULTIFRAME_MAX=0 ... --fg-dlss --fg-multiplier 4` | exit 0；补帧整体关闭；300 帧 @30fps |
| `VEYRA_TEST_NVENC_FIRST_OPEN_FAILS=1 ...` | 阶梯恢复；300 帧；exit 0 |

自动检查：`veyra_repair_contract_tests.exe` 169 项 0 失败（新增 4 项尾帧规则，含
"中间跳变不得走尾帧通道"）；`scripts/gates/delivery.ps1` PASS，
`logs/delivery/95a4effb615949cbaedd502d87272cb1/result.json`（46.9s）。

## 5. 顺带修掉的 CLI 缺陷

`apps/veyra/ui/AppShell.cpp`：`--fg-xess` / `--fg-fsr` / `--fg-dlss` 在第一遍参数解析里
没有被识别，会落进位置参数 `autoInput`，于是 `clip.mp4 --fg-xess …` 会去打开名为
`--fg-xess` 的文件（`demuxer: avformat_open_input failed code=-2`）。改为在位置参数
重新推导时把已知带值开关全部跳过，GPU 后端参数现在与位置无关。该路径只服务于测试/诊断
入口，UI 不受影响。

## 6. 未做与边界（如实）

- **FFX 非交换链 FG**：`ffxCreateContextDescFrameGeneration` +
  `ffxDispatchDescFrameGeneration{outputs[4]}` 确实存在（FidelityFX SDK 2.3.0），
  理论上能实现"真正的 FSR 补帧导出"。本轮未实现，列入后续任务。
- **XeSS 补帧导出**：SDK 没有非交换链入口，只能替换或关闭，永远拿不到真正的 XeSS 帧。
- **案例一现场复现**：本机驱动正常，无法复现陈旧 `nvEncodeAPI64.dll`，需要原用户复测。
- **未测**：AMD 显卡上的 FSR 补帧导出（导出路径仍只支持 NVENC）；40/30 系实机导出。

## 7. 测试包（已合并 main、未推送、未发布）

`C:\veyra-test-packages\final-exportfix-r5\Veyra-1.3.1beta-win64-portable.zip`，
469,779,240 字节，SHA256
`A7B10DD98C47A052F5479ED5B652BBA67E1B0D8F7D3C645D5388B611AB9930EC`；
包内 `Veyra.exe` SHA256 `81782558FE0486632A6A544F7E19B0D91D76D2B2C716681094AAEC5AE5E70D26`
（与构建树一致）。用包内 EXE 直接跑 `--fg-xess --fg-multiplier 2` 导出 300 帧素材：
600 帧 @60fps、逐帧验证通过、exit 0。
