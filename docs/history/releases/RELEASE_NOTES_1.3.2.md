# Veyra 1.3.2beta 测试包说明（隔离分支构建，未发布）

**这是给用户实机测试用的 beta 包，不是正式发布。** 构建来源：分支
`codex/capture-decode-latency-20260916`（未合并 main、未推送、未创建 Release）。
本包在 1.3.1beta 全部内容之上，重点是**采集（MPEG/压缩格式）链路重构**。

## 1.3.2beta2 修复（收到 beta1 的人必看）

**beta1 有一个严重影响原生采集格式（YUY2/NV12/RGB 系）的回归**：解码 worker 的帧池改造
错误地套用到了非压缩路径，`read()` 把原生路径的 `pendingFrame` 置空，下一个驱动回调被判定为
采集错误 → 引擎反复重连（约每 1 秒一轮），用户看到的是"采集进去只有 2 帧"。
压缩格式（MJPEG/H.264/HEVC）不受影响，这也是它没在 beta1 的 MJPEG 测试里暴露的原因。
beta2 已修复，并在真机对**四条路径各跑 2 分钟**：YUY2 1080p60 7664 帧 / YUY2 4K18 2150 帧 /
MJPEG 1080p60 7176 帧 / MJPEG 4K18 2150 帧，全部 0 丢帧、无重连。
新增 `scripts/acceptance/capture-paths-smoke.ps1`：一条命令同时验证原生与压缩两条路径。

## 1.3.2beta3 修复：PS5 串流"连不上"

**现象**：上一版里串流经常连不上，报"PS5 已结束或拒绝串流（终止码 12 / 错误 2012）"或 2004，
且**越重试越连不上**。

**实测根因**（用本机 PS5 直接测出来的）：PS5 在**会话结束后约 20 秒内**都会把新连接判为
`RP_IN_USE`（会话仍被占用）。而我们旧的重试退避只有 **1/2/4 秒**——三次重试全部撞在这个窗口里，
每次都被拒，然后直接放弃；用户看到的就是"连不上"。反复点击只会不断刷新这个窗口。

**修复**：识别到 `RP_IN_USE` 时退避改为 **10 / 20 / 40 秒**，界面显示"PS5 正在释放上一个串流会话，
等待后自动重试（n/3）"；普通网络中断仍用原来的 1/2/4 秒。

**实测证据**（同一台 PS5、同一台 PC）：

| 场景 | 结果 |
| --- | --- |
| H.264 / 20 Mbps / 20 秒 | 1118 帧、硬解、0 错误 |
| H.265 HDR / 100 Mbps / 25 秒 | 1373 帧、硬解、0 错误 |
| **上一个会话结束 3 秒后立刻重连** | 自动等待 → 重试 → **2715 帧、0 错误**（旧版这里必然失败） |

新增 `veyra_remoteplay_probe.exe`（随构建产出，不在便携包里）：不开 GUI 就能测串流，
`--codec h264|h265|h265hdr --bitrate <kbps> --seconds <n>`，方便以后对比码率/编码器/重连。

**注意**：串流失败后**不要连续猛点重连**——每次失败都会让 PS5 的释放窗口重新计时；
新版本会自动等待，手动重连时建议等 20 秒以上。

## 40 系的多帧上限：可以到 6X（2026-09-17 澄清）

补丁在运行库里的落点是 `test dl,dl / je <Blackwell-only 检查> / mov esi,5`：**5（6X）本来就是
默认上限**，"仅 Blackwell"只是附在前面的一道条件分支。我们的补丁把那道分支跳过
（`0f 84 → eb 04`），注释明确"分支之后的 count/index/profile 限制全部保留"。因此 **40 系补丁后的
上报上限就是 5（6X）**，UI 会照此提供倍率选项（选项由运行库上报的 `maxGeneratedFrames` 驱动，
没有硬编码 40 系 = 4X 之类的限制）。

**尚未在 40 系实机验证**（本机只有 50 系），需要确认三件事：
`FG capability available=true multiFrameMax=5`、实际生成帧数接近 5×、画面稳定
（**Ada 没有 Blackwell 的 flip metering 硬件，高倍率时序是最大未知**）。

40 系验证命令：
```powershell
.\Veyra.exe <任意视频> --fg --fg-multiplier 6 --smoke-seconds 15
```

## 1.3.2beta4：30 系（RTX 30）DLSS 补帧的新路径

上一版里 30 系解锁虽然"应用成功"，但 provider 的补帧可用性闸门仍拒绝 sm_86（真实 3060
日志：`FG.Available=false`）。本版加入了**进程内的 NVAPI 架构伪装**，并把顺序修正为
"在 NGX 初始化之前安装"，**在 RTX 30 上默认启用**（其余架构完全不走这条路径）：

- provider 被告知运行在 Blackwell 上（0x1B0）→ 架构闸门通过；
- 内核仍由 sm_86 重写提供（69 个 fatbin）→ 程序真的能在 Ampere 上运行；
- 伪装装不上时自动回退到旧的重定向路径，不会因此不可用；
- 可用环境变量调试：`VEYRA_TEST_NVAPI_SPOOF_ARCH=0`（关闭伪装、回到旧重定向路径）、
  `VEYRA_DISABLE_AMPERE_MFG_UNLOCK=1`（整体关闭 30 系解锁）。**不要设 0x190**：本机实测按 Ada
  上报时第一个生成帧会 Evaluate 故障并挂住进程。

**本机验证（RTX 5070，模拟 30 系配置）**：伪装 + sm_86 内核 → `FG capability available=true
multiFrameMax=5`、376 真实帧/374 生成帧、无崩溃；50 系默认路径与 40 系路径都不安装伪装
（对照运行无 `nvapi-spoof` 活动）。**真实 30 系硬件未验证**——这正是这个包要测的东西：
把 `logs/veyra-app.log` 里 `[ampere-mfg] ... spoofed=?`、`FG capability ...`、
`[ngx] fg-backend ...` 与 `[app] smoke frames=/generated=` 几行发回来即可。

## 采集链路（本次重点）

- **压缩格式不再走系统解码器**：MJPEG/H.264/HEVC/AV1/VP9 由 `ConnectDirect` 直接送入我们的
  压缩 sink，解码由自有后端完成，不再经过"系统解码器 → 强制 RGB32 → SampleGrabber"这条路径。
- **MJPEG 自有软解 worker**：DirectShow 回调只拷贝压缩 payload（数百 KB），独立线程解码为
  full-range NV12；有界队列丢旧保新。本机 2 分钟实测：1080p60 与 4K18 均 0 丢帧、
  `errors=0 queueDrops=0`；图侧每帧 CPU P95 1080p 1.987→0.36ms、4K 6.218→0.92ms。
- **H.264/HEVC/AV1/VP9 走 D3D12VA**（共享 Veyra 设备），纹理直入图；首帧不可导入自动回退软解。
  本机采集卡没有这两种格式，合成码流验证：H.264 硬解/软解各 900 帧、HEVC 各 252 帧，帧数一致。
- **三级回退**：D3D12VA → 软解 → RGB32 兼容路径；任一级失败不断流，日志写明当前后端。
  采集面板的格式列表里压缩格式仍排在原生格式之后（延迟档位标注不变）。
- **延迟口径注意**：`[capture-timing]` 的 `callback→Present` / `readAgeMs` 现在包含我们自己的解码
  （旧路径的解码发生在系统解码器里、不在窗口内），因此这两个数字与旧版本**不可直接比较**；
  端到端延迟请用相机法或与 OBS/PotPlayer 同源对比。

## 颜色

- 采集颜色 A/B 工具（`veyra_capture_color_probe.exe <采集路径> <目录> [--legacy-rgb]`）修复了
  双平面格式的越界崩溃，现在新/旧两条路径都能输出对照图。**静止画面的逐像素验收还没做**，
  测试时若发现颜色异常请附上两条命令的输出。
- H.264/HEVC 的 in-band 参数集与 `MPEG2VIDEOINFO`（Annex-B / avcC / hvcC / 长度前缀四种形态）
  都做了解析；10bit 压缩输入目前会降位到 8bit（日志会写警告），HDR 压缩采集未实现。

## 继承自 1.3.1beta（不变）

- DLSS 6X（50 系）/ 40 系 MFG 解锁 / XeSS MFG（2X-4X）/ AMD FSR 帧生成与超分；
- 杜比/DTS 位流解码兜底（AC-3/E-AC-3/TrueHD/DTS/DTS-HD）与采集音频手动选择；
- 原生采集格式（YUY2/NV12/RGB 系）的 N1/N3/N4 优化（GPU 解包、格式排序、缓冲协商）。

## 测试重点

| 场景 | 建议 |
| --- | --- |
| MJPEG 采集卡（1080p60 / 4K） | 关闭全部增强，与 OBS/PotPlayer 对比延迟体感；看 `[capture-timing]` 的 `readAgeMs` 与 `captureDropped` |
| H.264/HEVC 采集卡 | 确认能出画面；日志应有 `compressed ConnectDirect codec=h264/hevc ... hr=0x0` 与 `backend=d3d12va`；若显示 `backend=software` 或回退 RGB32，请把日志发回 |
| 颜色 | 静止画面下跑 `--legacy-rgb` A/B（见上） |
| 原生格式卡 | 确认无回归（应与 1.3.1beta 一致） |

## 已知边界

- H.264/HEVC 未在本机真卡验证（本机卡只有 YUY2/MJPEG）；AV1/VP9 采集未验证。
- 端到端光子延迟未测；4K18 的 `readAgeMs` 与上一内部版本存在 0.4–3ms 无法定性的差异。
- 帧池固定 4 帧（1080p 约 12MB / 4K 约 50MB 系统内存）。

## 包内容

- `Veyra.exe`、FFmpeg 运行库、VC 运行库、`shaders/`
- `runtime/experimental/`（NVIDIA 组件）、`runtime_local/intel/experimental/`（XeSS）、
  `runtime_local/amd/fidelityfx/`（AMD FidelityFX SDK 2.3.0，MIT，AMD 签名）
- 许可证、第三方 notices、`release-runtime-manifest.json`
