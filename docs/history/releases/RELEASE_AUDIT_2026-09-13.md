# 正式发布前审查 — 2026-09-13

> 后续：用户随后授权仅修复三个 P1。本地修复及针对性/交付回归已完成，见 [P1 修复记录](../plans/RELEASE_P1_REPAIR_PLAN_2026-09-13.md)。两个 P2 未修。以下保留修复前基线的审查证据，不代表新构建仍存在这三个已修 P1。

结论：当前候选版本不建议作为正式版发布。确认 5 项问题，其中 3 项 P1（发布前应修复）、2 项 P2。本次是审查，不是修复交付；产品源码未改，未提交、推送或发布。

## 审查基线与证据范围

- 分支：`codex/ps5-sampling-repair`；HEAD：`f037f49`（`fix(remoteplay): add chroma-aware sampling and cubic presentation`）。开工时工作区干净。
- 独立构建：`out/release-audit-20260913/build`，Remote Play 实际后端开启，444 步编译/链接成功。
- 构建 EXE SHA256：`F08F2C1E1296591CD3C2F2CDC3C1701195EAA37F51DC5F0147F2594A1E63C694`。
- 本机：Windows x64、RTX 5070、驱动 616.56、MSVC 19.44.35226.0。构造测试素材使用本机 FFmpeg 8.1.1；产品实际运行使用下述 patched FFmpeg 9.0.1。
- 审查覆盖：文件 demux/decode、颜色/上传、共享增强图、播放调度与缓存寿命、暂停/seek/reset、音频、采集入口、图片/视频导出、PS5 会话/恢复/配置、UI 设置及便携包依赖。重点检查 v0.0.5 之后的变更，同时复查共享旧代码。
- 所有本轮日志、合成素材和临时诊断代码位于忽略目录 `logs/release-audit-20260913/`、`out/release-audit-20260913/`。没有拿历史通过代替本轮结果，也没有独立 Reviewer 验收。

## 确认的问题

### F1 · P1 · 播放过载到文件尾部时崩溃

位置：`src/engine/EngineController.cpp:555-568`，尤其第 558 行；关联 `src/media/FFmpegVideoDecoder.cpp:209`。

过载追赶循环继续调用 `activeSource->read()`，遇到 EOS 后仅 `break`，注释宣称保留最后候选帧。但 `frame` 是 decoder 内部复用的 AVFrame；下一次 `avcodec_receive_frame()` 会先 unref/清空该对象，即使最终返回 EOS。此时保存的指针已不代表原先有效帧，后续 clone/process 收到 format=-1、尺寸被清空的帧，触发 libswscale 断言。

本轮复现：2 秒、640×360、30fps、H.264/AAC 合成文件 `repros/regular.mp4`，关闭 NR/SR/FG，正常播放退出 0；仅加入已有的负载注入 `VEYRA_TEST_VIDEO_WORK_MS=150`，相同文件到尾部退出 `3221226505 / 0xC0000409`。负载注入模拟逐帧工作欠速，不修改 decoder、EOF 或帧数据。

日志证据：

```text
[player-timing] ... previewSkipped=38 playbackSpeed=1.004 processed=14
Assertion desc failed at C:\veyra-deps\ffmpeg-ps5-slices-source\libswscale\swscale_internal.h:778
```

完整命令和结果：`logs/release-audit-20260913/repros/commands.json`，`play-normal.*.log`、`play-overload-tail.*.log`。生成与复现代码：`out/release-audit-20260913/reproduce.py`。

修复门槛：在继续拉取下一帧之前持有最后候选帧的独立引用，或将 EOS 与候选帧寿命分开处理；不能把空 AVFrame 交给图。相同负载跑到 EOS 应正常结束，保留真实尾部时序，不能靠禁用追赶或丢弃声音掩盖。

### F2 · P1 · 文件中途改变分辨率会写出原有上传缓冲范围

位置：`src/pipeline/EnhanceGraph.cpp:820-835`；原缓冲容量在 `104-110`、`146-149` 行确定；`src/engine/EngineController.cpp:520` 的尺寸变化重建仅适用于 Remote Play。

软件 YUV 分支将 swscale 输入、输出尺寸都设为当前 `frame->width/height`，但目标地址、行跨度和容量仍按图初始化时的 `srcW_/srcH_` 分配。文件路径没有统一的每帧尺寸校验或重建，视频导出也直接复用同一图。合法 H.264 分辨率切换即可进入该路径。

本轮复现：拼接 1 秒 320×180 与 1 秒 640×360 H.264，容器初始尺寸 320×180，ffprobe 确认两种尺寸各 30 帧。播放和导出均返回 0，仍按初始 320×180 图处理 60 帧，没有 Resize 重建。本轮没有观察到该素材导致进程崩溃，不能将其报告为已复现崩溃。

进一步采用安全 canary 检查：使用产品同一 `swscale-10.dll`、相同尺寸及 stride，但实际分配更大的诊断数组，避免诊断本身破坏内存。结果如下：

| 平面 | 产品初始声明容量 | 超过该容量仍被写入的字节数 |
| --- | ---: | ---: |
| Y | 92,160 | 92,288 |
| UV | 46,080 | 46,208 |

这是已验证的越界写入条件；系统/GPU 分配留白可能使某次运行没有立即崩溃，不构成安全性证明。

证据：`repros/changing-inspect.stdout.log`、`repros/play-changing-size.*.log`、`repros/export-changing-size.*.log`、`resolution-canary.json`。安全 canary 在 `out/release-audit-20260913/final_checks.py` 中。

修复门槛：在任何像素读取/转换前校验实际帧尺寸和格式；文件预览按契约重建全部历史或明确拒绝；固定尺寸视频导出应明确定义缩放/拒绝策略。不能继续用新帧尺寸写旧容量。

### F3 · P1 · 损坏尾部视频帧后仍报告导出成功，并截短音频

位置：`src/source/MediaFileSource.cpp:173-175`；关联 `src/media/FFmpegVideoDecoder.cpp:209-212`、`src/engine/VideoExportJob.cpp:85-124`。

源读取对 `sendPacket` 的硬错误仅记录日志并跳过；`receiveFrame()` 也把 EAGAIN、EOF、真正解码失败统一折叠成 nullptr。导出因此把“跳过坏帧后耗尽”当作正常 EOF。完成验证只重新解码第一帧，未发现尾部缺失，随后把 partial 提升为正式输出。

本轮控制实验：原文件 60 帧、2 秒，只将最后 8 个视频 packet 的 payload 置零，保留容器、时间戳和完整音轨。输入已经损坏；问题不是要求修复坏数据，而是导出应该明确失败或报告损失，不能宣称完整成功。

| 项目 | 输入 | 被报告成功的输出 |
| --- | ---: | ---: |
| 视频 | 容器 60 帧；可解码 52 帧；2.000 秒 | 52 帧；1.733333 秒 |
| 音频 | 2.000 秒 | 1.749333 秒 |
| 程序结果 | 解码错误 `-1094995529` | 退出 0；`export result=true` |

日志同时出现 `send_packet failed ... Invalid data found when processing input`、`source=52 generated=0 hold=0 output=52` 与最终成功。损坏的源文件测试使用故意构造的尾部错误，没有修改用户媒体。

证据：`repros/corrupt-source-inspect.stdout.log`、`repros/export-corrupt-tail.*.log`、`repros/corrupt-export-inspect.stdout.log`；详细命令见 `repros/commands.json`。

修复门槛：区分 decoder 的待输入、正常 EOF 与硬错误，导出严格传播硬错误；核查源帧/输出帧、尾部时间线及音轨，而非只读第一帧。失败应保留 partial，并明确告知用户。

### F4 · P2 · 视频导出对 partial 文件的保护不是原子的

位置：`src/engine/VideoExportJob.cpp:23` 与 `84`。

入口只检查一次目标/partial 是否存在，随后进行设备、源扫描与增强图初始化；最终 `avio_open(..., AVIO_FLAG_WRITE)` 会截断打开已有文件。另一导出进程在检查与打开之间创建相同 partial 时，双方都可能通过初始检查，导致覆盖或交错写入。

本轮在第一进程完成 preflight 后、打开输出前，用审查脚本独占创建该进程目标的 partial 哨兵文件。进程继续退出 0，哨兵被覆盖，文件最终被提升为正式输出。实验仅涉及本轮专用路径和本轮创建的哨兵，没有覆盖用户文件。

证据：`partial-collision.json`（`inserted_after_preflight=true`、`sentinel_preserved=false`、`exit=0`）、`partial-collision.*.log`；命令/控制逻辑位于 `final_checks.py`。

修复门槛：用原子独占创建/持有的句柄接入 FFmpeg AVIO，或为任务生成并持有独有临时文件，再无覆盖地提升目标；不能依赖 exists 后普通 write-open。

### F5 · P2 · 视频导出无提示丢弃内嵌字幕

位置：`src/engine/VideoExportJob.cpp:58-68`、`124-125`。

导出只创建视频和第一条音频流，不检查字幕，也没有导出前缺失提示或结果报告。本轮 MP4 输入包含 H.264、AAC、mov_text 三条流，导出成功文件只剩 H.264、AAC；mov_text 本身可由当前 MP4 muxer 承载。

产品规格第 315 行要求兼容字幕复制/转换，否则开始前明确报告；这项要求并未被当前执行例外撤销。额外静态检查发现播放器 `AppShell.cpp:137` 仅自动读取同名外置 SRT；不能据 README 的“字幕”入口宣称已实现容器内基础字幕。内嵌字幕播放缺失是代码检查结论，本轮未进行可视化播放验收。

证据：`subtitles-inspect.stdout.log`、`subtitles-export-inspect.stdout.log`、`export-subtitles.*.log`；源文件 `repros/subtitles.mp4`，只有本轮生成的测试文本。

修复门槛：实现并验证兼容字幕保留，或开始前明确告知不保留、给出可检查的结果；同步澄清播放器支持范围。

## 本轮实际验证

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 独立 x64 Release + PS5 后端完整构建 | 444 步成功 | `build.log` |
| 当前 `scripts/gates/delivery.ps1` | 23 项通过，45.033 秒 | `logs/delivery/c992884d6d924d02a4cce0374a758094/result.json` |
| 第一组单元/集成/实 GPU 检查 | 27 次运行通过 | `checks.json` |
| 扩展采集/FG/图像/HDR/调度检查 | 16 次通过；seek 素材不符合条件的一次作废，另用有效素材重跑通过 | `more-checks.json`、`final-checks.json` |
| Remote Play 离线 core | 新构建，73/73 通过 | `core.log` |
| 最新精细采样数值对照 | 30 组通过：2 ingress、16 呈现、12 色度位置 | `sampling-result.log` |
| 本地便携包制作与内容扫描 | 通过，7 个批准运行文件，forbiddenFiles=0 | `out/release-audit-20260913/package/package-audit.json` |
| 便携包独立启动 | 5/5 通过 | `portable-smoke/result.json` |
| 故障/边界验证 | 确认 F1–F5；常规 gate 未覆盖这些边界 | 上述逐项证据 |

除证据单列完整路径者外，表内日志均相对 `logs/release-audit-20260913/`。

交付 gate 实际包含 1080p NR/NVOF/GBV、native 4K 正确性、4K 输入实时档播放器、暂停 seek、PNG/JPEG、native 4K H.264/HEVC D3D12 NVENC 导出、完整短片解码/时长/音轨核验及取消保留 partial。它只证明所测短场景。

额外测试确实执行了 DLSS 2X/3X/4X、SR/NR/FG、双向光流、图片尺寸/方向、软件/硬件 Main10 和 HDR 转 SDR 增强组合、运行时切换、NR→SR 预览、文件欠速、音频端点故障、有效素材 seek 压力、配置损坏保护和 WASAPI 时间线。XeSS 记录 generated=43、debugErrors=0；AMD OF 记录 dispatches=48、debugErrors=0；这些是在本机 RTX 上的 SDK 执行，不是 AMD/Intel 显卡验收或显示器扫描输出证明。

采集卡 `USB3 Video` 实际读帧：YUY2 与 MJPEG 1920×1080@60 均收到 19 帧，消费者停顿时丢弃过期 17 帧，返回帧保持拥有权，callback fps 约 60、最新帧年龄分别 1.103/3.808ms。没有据此判断画面内容、端到端延迟或完成 4K60 实卡验收；该设备本次枚举的 3840×2160 模式为 18fps。

新采样测试使用独立临时诊断程序，显式启用 `highQualityPresentation`，对 720p 缩小、1080p 1:1、1440p/2160p 放大以及两个交换链模式采样比较；与 CPU Catmull-Rom/局部限幅参考的最大码值误差 0.560/255，1:1 误差 0。YUV420P/NV12 的六种色度位置与 CPU 双线性/颜色参考最大误差 0.588/255。这里只验证合成 SDR 数据到呈现缓冲；没有作游戏画质判断或测量该新路径的 PS5 实时成本。临时文件 `SamplingAudit.cpp` 的统计均值分母未针对抽样步长调整，因此报告只引用正确的最大误差，不引用 meanError。

最初 seek 压力测试使用 60.016 秒素材，但 harness 固定跳到 344.93/643.709/700 秒，并要求超过目标后继续呈现；因此那次退出 1 不能算产品失败。改用新生成的 710 秒素材，相同未修改的测试程序全部断言通过，22.156 秒结束。原失败日志保留，没有删除失败来制造通过。

真实运行日志摘录：

```text
snippet CreateFeature id=18 result=0x1 (NVSDK_NGX_Result_Success) handle=non-null seh=0
sr-backend: Create DLSS 1920x1080 -> 3840x2160 result=0x1 ... seh=0
fg-backend: Create DLSSG 3840x2160 ... result=0x1 ... seh=0
RESULT source=12 generated=11 contentValid=11 display=unmeasured
fg-backend: ReleaseFeature result=0x1 ... evaluates=13 resets=2
nvOFDestroy status=0 executes=11
[video-sr] op=0 result=0x1 seh=0x0
smoke frames=150 generated=115 failed=false ... nrEvaluated=120 nvofExecuted=117
```

对应 `nr-flow.stdout.log`、`sr-nr-fg.stdout.log`、`portable-smoke/video-sr-nr-fg.stdout.log`。FrameLease 测试中的 `still leased; refusing overwrite` 是故意保留旧帧触发的防御成功，不是本轮新增故障。

## 实际命令入口

以下均在项目根目录执行，构建依赖路径使用本机已有的受控资源：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release -BuildDirectory out/release-audit-20260913/build -RemotePlay -ChiakiCheckout C:/veyra-deps/chiaki-source -ChiakiStage out/remoteplay/chiaki-msvc-stage -RemotePlayPrefixPath C:/veyra-deps/remoteplay-installed/x64-windows-static -ProtocPath C:/veyra-deps/remoteplay-installed/x64-windows/tools/protobuf/protoc.exe -PkgConfigPath C:/veyra-deps/vcpkg/downloads/tools/msys2/3e71d1f8e22ab23f/mingw64/bin/pkg-config.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/release-audit-20260913/build
python out/release-audit-20260913/run_checks.py
python out/release-audit-20260913/more_checks.py
python out/release-audit-20260913/reproduce.py
python out/release-audit-20260913/final_checks.py
& out/release-audit-20260913/core.cmd
python out/release-audit-20260913/prepare_sampling.py
& out/release-audit-20260913/sampling.cmd
& out/release-audit-20260913/build/SamplingAudit.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/package-portable.ps1 -Root . -Version 0.0.5 -OutputDirectory out/release-audit-20260913/package -BuildDirectory out/release-audit-20260913/build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/portable-smoke.ps1 -PackageDirectory out/release-audit-20260913/package/Veyra-0.0.5-win64-portable -InputFile logs/ps5-p1-1080p30-long.mp4 -OutputDirectory logs/release-audit-20260913/portable-smoke
git diff --check
```

详细逐条 EXE 参数和退出码在三份 `*-checks.json` / `checks.json` 与 `repros/commands.json`；每个运行有 stdout/stderr。诊断脚本生成的素材/输出采用独有路径，重跑应另选新的审查目录以保留原证据。

## 运行时与源码隔离

- 原版 NR SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，165840496 字节、310.8.0.0、Valid。
- 批准的社区 NR SHA256 `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`，同版本/大小、HashMismatch；未伪称有效原版签名。
- patched `avcodec-63.dll` SHA256 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`，19345920 字节。实际构建依赖、此前修复版与新审查版一致，保留 H.264 slices 32→256 修复。
- 本地包带 `licenses/FFMPEG-VEYRA-BUILD.json`，通过当前构建来源 manifest 校验；未把 vcpkg SPDX 当作 patched tree 的完整身份。
- 包内运行组件按现有 7 文件清单校验来源/哈希/签名/许可证；未新增或替换运行组件。测试移除 publisher manifest 后，软件仍能独立加载符合用户选择的原版/社区运行时，测试结束恢复 manifest。
- 审查 ZIP SHA256 `4746BCCFCDEB789DB291E5A2A1130A14996EDD33EAC7F549BE1BF2EFE01CC2A1`；它是有上述已知问题的本地审查材料，不是批准发布的新版本。
- `git ls-files` 对 DLL/LIB/EXE/模型/PDB/ZIP/addon 与本地 runtime/SDK 目录扫描为空；未把凭据、个人配置、测试媒体、SDK 或运行时纳入 Git。

## 未执行与后续门槛

本轮未连接实际 PS5，未操作 PSN 登录/注册/唤醒/真实网络重连，未读取或导出主机密钥；未复测真实 PS5 AU 的软硬解全图一致性。Main10/HDR 的合成/文件检查不等于 HDR 显示器验收。精细采样的 P010 与实际 PS5 HDR 组合、游戏主观画质、扫描输出和端到端延迟仍未执行。

未进行数小时稳定性、真实设备拔插/显卡 device-lost、电源睡眠恢复、多 GPU/驱动矩阵、AMD/Intel 独立显卡、4K60 采集卡持续增强及人工全 UI 验收。此次短测不能证明这些项目已通过，也不能保证项目不存在其他 bug。

下一项唯一任务：先修复 F1 的 EOF 候选帧寿命，加入正常/欠速/暂停 seek 到尾部的回归；随后按 F2→F3→F4→F5 处理。正式发布前至少清零本报告 P1，并对导出数据保留问题完成修复或明确可验收的功能边界，再运行针对性回归和 delivery gate。
