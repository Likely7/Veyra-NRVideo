# 串流实机反馈修复（2026-10-01，beta 4）

来源：双机测试者用 beta 2（PC 串流）实测后的聊天记录、`veyra-qml.log`（两份）和一个崩溃转储 `veyra-crash-20261001-131137-4772.dmp`。
用户要求：码率拉满并可自定义；查卡顿；查"开光流后跑不满"和防闪烁；画面一顿一顿；修掉。分支 `codex/xbox-20261001`。

## 1. 画面一顿一顿 / 帧率上不去（已找到根因并修复）

**证据**：PC 串流时 moonlight-common-c 报了 685 次 `Video decode unit queue overflow`（解码队列 15 帧满了，丢掉并重新要关键帧），
引擎每秒只拿到 15–48 帧，读到的画面已在队列里放了 100–280 ms（`readAgeMs`）。引擎自己的 GPU 时间只有 2–4 ms，不是瓶颈；
`mailboxOverwritten=0` 说明是解码线程产出慢。

**根因**：本项目的 FFmpeg 同时带 libdav1d 和原生 AV1 解码器，`avcodec_find_decoder(AV1)` 返回的是 **libdav1d**（只有软件路径）。
所以"D3D12VA 打开成功"后第一帧其实是软件帧，被判为不可导入，退回单线程软件解码。测试者选的是 AV1（`formats=0x1000`）。

**复现**（`tools/stream_decode_bench`，本机 RTX 5070，150 Mbps CBR 噪声测试流，60 fps 节奏）：

| 流 | 修复前 | 修复后 |
|---|---|---|
| AV1 1080p | 软件 25.4 ms/帧，39 fps | d3d12va，0.4 ms 提交，60 fps |
| AV1 4K | 软件 35.9 ms/帧，27.5 fps | d3d12va，60 fps |
| HEVC 4K | D3D12VA 第一张图后报错（"hardware accelerator failed"，之后建纹理失败） | d3d11va，60 fps |
| H.264 4K | d3d12va 60 fps | 不变 |

HEVC 4K 的 D3D12VA 失败在系统自带的 FFmpeg 8.1.1 上同样出现；文件播放早已因此让 HEVC 走 D3D11VA（`docs/DIAG_HEVC_D3D12VA_AND_EXPORT_CFR_2026-09-17.md`）。

**修复**（只对串流来源生效，采集卡路径不变）：
- `FFmpegVideoDecoder::setPreferNativeAv1Hardware`：硬件打开时用原生 `av1` 解码器（带 hwaccel）。
- `CaptureCompressedDecoder::setStreamProfile(true)`（Moonlight、Xbox 调用）：AV1/H.264 → D3D12VA；HEVC → D3D11VA（独立设备，共享纹理给图）；
  硬件在第一张画面前就失败（显卡不支持该编码）时自动换软件解码；软件解码给多个低延迟线程。
- Moonlight 来源把 D3D11VA 画面的纹理视图交给图（与文件播放同一入口）。
- 软件解码仍是最后手段：150 Mbps 下每帧 25–45 ms，跟不上 60 帧，统计浮层会显示"软件解码"。

## 2. 码率（已改）

- 默认码率 150 Mbps（原来是"自动"：1080p60 约 20、4K60 约 80）。设置改成滑块，5–500 Mbps 任意（5 的倍数）。
  旧设置里保存的"自动"读入时按新默认 150 处理。默认没有直接放 500：无线网络下高码率会丢包，反而一顿一顿；有线千兆可自己拉到 300–500。
- 主机画面简单（桌面、静止）时编码器实际输出远低于上限，这是正常的。统计浮层（Ctrl+Alt+Shift+S）现在显示**实际收到的码率**和收/解帧率
  （以前这两个帧率字段没有计算，一直显示"—"）；日志每 10 秒一行 `[moonlight] stream ...`，含后端、帧率、码率、各段耗时。

## 3. 配对：第二次要重新配对、配完还连不上（已修复）

**根因**：Sunshine 只在带客户端证书的 HTTPS 请求里报告"已配对"，普通 HTTP 一律报 0。局域网自动发现和"手动添加"探测用的是不带证书的 HTTP，
软件把这个 0 当真，把已配对的主机改成"未配对"并保存。用户再去配对同一个证书，主机那边就出现重复记录，之后连不上，只能在主机端删掉重配。

**修复**：`ServerInfo::pairStatusKnown` —— 只有 HTTPS 的回答（或主机明确拒绝我们的证书）才会改配对状态；探测已知主机时带上保存的证书走 HTTPS；
点"配对"时先用保存的证书确认，主机仍信任就不再走 PIN；启动时主机明确不认我们才标为未配对。模拟主机改成和 Sunshine 一样（HTTP 永远 PairStatus 0），
新增测试"再次发现的主机仍是已配对""主机仍信任时不需要重新配对"（模型测试 42 → 47 项，连跑 3 次通过）。
**已经出问题的测试者**：在主机 Sunshine 网页里删掉旧的 Veyra 客户端，本机也删掉主机，重新配对一次即可。

## 4. 退出时崩溃（转储分析，已修复可能原因）

转储：退出时引擎线程在 `D3D12Core.dll+0xD285` 读取 `0x7FFD4F73F710` 失败，发生在"device context shutdown complete"之后，
该地址不属于任何已加载模块，正好落在 NVIDIA 用户态驱动 `nvwgf2umx.dll` 之后的空档里——典型的"回调进了已卸载的 DLL"。
（用 /MAP 重新链接 beta 2 的同一份代码，.text 段与包内逐字节相同，以此还原栈：`EngineController::run → ~D3D12DeviceContext → … → D3D12`。）
这个会话开关过 6 次 NVIDIA Reflex，`ReflexSession` 每次重开和退出都 `NvAPI_Unload + FreeLibrary(nvapi64.dll)`，而驱动在设备最终释放时还会用到 nvapi 的设备状态。
**修复**：nvapi64.dll 加载一次后固定在进程里（`GET_MODULE_HANDLE_EX_FLAG_PIN`），不再卸载；关闭 Reflex 只关睡眠模式。
没有调试器符号与驱动 PDB，不能 100% 断定，下个包请继续收集崩溃转储。

## 5. "开光流后 1440p 只有 52 帧"、防闪烁（排查结论：不是回归）

日志（RTX 5060 Ti，4K 采集，NR 内部处理 1440p）：NR 本身约 15 ms，NVIDIA 光流约 2.5 ms，残差/防闪约 0.7 ms，颜色约 1.2 ms，合计约 20 ms，
超过 60 帧每帧 16.7 ms 的预算 → 51–52 帧。关掉光流约 17 ms，勉强 57–60 帧，但防闪失去运动补偿，闪烁回来——和测试者看到的一致。
NR 内部 1080p 时：NR 9 ms + 光流 2 ms，稳定 60 帧。
与 1.4.4 对比：分辨率计划（光流跟随 NR 内部尺寸）和 NVOF 会话代码与 1.4.4 完全相同；1.4.4 只要开 NR 就**总是**跑光流，没有开关。
所以"以前默认有光流也能跑满"多半是以前 NR 内部用的是 1080p。建议：5060 Ti 这一档 4K 采集用 NR 1080p；或把光流后端换成 AMD FidelityFX（日志里 1080p 只要 0.5 ms）。

## 6. 玩一段时间掉到 10 帧（未定位，已加诊断）

05:53 那次：没有任何设置变化，所有 GPU 阶段同时逐渐变慢（NR 13→33 ms、颜色 1.1→7.5 ms、光流 3→8 ms），像是显卡降频或显存被挤到系统内存。
那个进程每次改设置记录的显存占用从 0.3 GB 涨到 9.5 GB。本机用 4K 文件做了两组复现（开关光流 20 次；全屏切换 40 次后再重建），
每次重建只多约 2 MB，没有复现大量增长。现在采集/串流时每秒在 `[frame-rate]` 行记录 `vramMiB`/`budgetMiB`，下次出现时能看出是不是显存。
请测试者出现时同时看一下显卡温度和频率（任务管理器或 GPU-Z）。

## 未处理

- 聊天截图里有一条关于鼠标的问题，截图分辨率太低看不清，需要测试者重述。
- 没有真实 Sunshine / Xbox 主机，以上串流修复只在本机解码基准和模拟主机上验证；HEVC D3D11VA 串流路径未见真机。

## 验证

- `veyra_stream_decode_bench`：上表四种流，修复后全部 60 fps、无回退（`tests/stream-fix-20261001/`）。
- 单元测试：Moonlight 协议 134、模型 47（×3）、Xbox 72、effect chain、easing、preset、repair 243、qml data —— 全部通过。
- PC 串流对话框端到端（模拟主机）通过；截图 `tests/stream-fix-20261001/ui-demo/05-settings.png`（码率滑块）。
  截图改用 PrintWindow，避免被别的窗口挡住。
