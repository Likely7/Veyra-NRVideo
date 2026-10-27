# 调度修复实施与证据

日期：2026-09-10。当前状态：已构建并通过本轮软件短测；FRUC 恢复问题和完整 S3/S4 仍开放，不宣称所有性能问题解决。

## 旧 Loop 为何阻塞，以及现在的规则

旧 `scripts/loop-gate.ps1 -Gate preflight` 仍把 `loop/CONTROL_HASHES.json` 内的早期内容作为唯一基线。README 和 AGENTS 已随功能、发布策略更新，因此只有这两项控制哈希失败；并非构建错误、DLL 身份失败或 SDK 缺失。用户明确要求废弃旧体系后，已更新 AGENTS、README、早期 Playbook、ACTIVE_DELIVERY_PLAN 和 gates README：旧 Loop、STOP、Phase 排队和控制哈希不再控制当前任务。没有改旧哈希清单，也没有伪造 preflight 通过。

`delivery.ps1` 删除对旧 `loop/STOP` 的依赖，并显式加载当前 PowerShell 自带的 Utility 模块，修复 Windows PowerShell 从 PowerShell 7 继承模块路径后找不到 Get-FileHash 的问题。播放、图像、编码、帧数、音轨和取消检查原样保留。产品导出完整性代码未修改。历史测试素材仍位于 ignored `loop/local`，路径保留并不意味着启用旧工作流。

## 实现

1. `FrameFlowWindow` 为每个 session/revision/epoch 独立保存账本。异步任务持有自己的窗口，设置切换或 Drop 后的旧完成不能写入新窗口。关闭窗口仍落一次完整计数，活动窗口每秒汇总。
2. 统计源帧接受/采样跳过、真实帧提交、FG 候选/提交前跳过/Evaluate/预热/有效/无效、真实/生成呈现、过期和取消。每个窗口记录最后的源 sequence、batch 和 ready/consumer fence。GPU 计时和 blit 同时过滤 revision 与 epoch。
3. 有效生成 FPS 和 Present 提交 FPS 采用最近一秒事件窗口，暂停显示零，停流会衰减。XeSS 的 SDK 生成提交单列，不把它叫经过本应用逐帧验证的生成数或屏幕扫描率。
4. 采集 packet.sequence 改为对应回调的序号，读出计数仍独立；mailbox 覆盖后序号缺口可见。容量仍为 1，half-rate 的有意跳过单独统计。
5. 仅采集的 DLSS FG 在 Evaluate 前执行 admission。沿原 arrival/PTS 时间线判断；整对的最后生成时间戳也已过期，或同窗口实测预测无法赶上时，跳过该对全部 FG 工作。保留既有 10ms 呈现容差，不改变 SR/NR 质量。
6. 跳过后只使 FG 内部历史失效，下一次执行先 reset/reseed，恢复帧只输出真实画面。NR、SR 与运动估算继续遵守原历史边界；文件与导出不启用 admission。
7. `PresentationWorker` 的 queued 取消也有回调计数；析构前先 join，防止任务引用的局部统计对象已被析构。呈现前取得 GPU 锁后再次检查过期，避免在等锁期间失去时效仍提交。
8. 修复 `EnhanceGraph::applySettings` 的倍率比较：界面的关闭值 1 应对应至少 2 倍的已分配容量。原来关闭 FG 后再改内容节奏会被错误拒绝。60 -> 30 -> 60 的回归已复现并修复。

文件：`include/veyra/{diagnostics/FrameMetrics,engine/FrameFlowWindow,engine/LiveFgAdmission,engine/EngineController,engine/PresentationWorker,engine/TimingWindow,engine/VideoPresenter,gfx/CommandSlotRing,pipeline/EnhanceGraph}.h`；`src/{engine/EngineController,gfx/CommandSlotRing,pipeline/EnhanceGraph,source/CaptureCardSource}.cpp`；`apps/veyra/TelemetryWindow.cpp`；对应 tests、CMake 和 `scripts/acceptance/scheduler-short-test.ps1`。

## 同源过载对照

测试实际调用同一产品 EngineController 和实时 PresentationWorker，使用文件回放模拟实时调度；没有打开物理采集卡，回放没有模拟 DirectShow mailbox 的回调覆盖。

输入：`loop/local/fixed_clips/test_av_1080p.mp4`，SHA256 `7952AD2904C8FED78402BC299EA2A04D1D869663EBCE17BBAC0C297F84FA2A91`。配置：1080p60 -> RTX Video SR quality4 -> 原生 3840x2160 NR -> DLSS 4X。唯一差异是 test-only admission 关闭/开启。

| 150 源帧观察点 | 旧调度对照 | 新 admission |
| --- | ---: | ---: |
| FG Evaluate | 450 | 0 |
| 提交前跳过 | 0 | 450 |
| 已计算后过期 | 444 | 0 |
| 生成帧呈现 | 0 | 0 |
| GPU 完成处理速率 | 27 fps | 33 fps |
| 真实帧 Present | 149 | 148 |
| 合成回放 arrival 到 Present 返回 P95 | 71.9153 ms | 60.6036 ms |

最后 1--2 批由测试停止取消，不是把真实源帧当生成帧跳过。旧对照结束时多处理了 1 源帧，因此 closed 汇总为 151 源帧/453 Evaluate/447 expired；表格固定使用停止前 150 源帧观察点，不能混用两个采样时刻。

日志：`logs/scheduler-repair-20260910/overload-{baseline,admission}/{engine.log}`，stdout 和 result JSON 位于同级，记录测试 EXE hash。结束后的旧槽复用等待 752 次/4821.273ms，新策略为 0 次；这是此过载回放的观察结果，不是所有配置都没有 CPU 等待。

没有实测 GPU 占用百分比，没有外部 scanout/光子延迟测量，不据此声称与 Magpie 等效或原生 4K NR 已达 60fps。

## 构建和回归

所有测试串行，每次进程由短测脚本限制到 290 秒。GPU 为本机 RTX 5070；不读取实体采集卡。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
./scripts/acceptance/scheduler-short-test.ps1 -Name cpu-final -Exe ./out/build/x64-release/veyra_repair_contract_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name worker-final -Exe ./out/build/x64-release/veyra_presentation_worker_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name live-half-final -Exe ./out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('loop/local/fixed_clips/test_av_1080p.mp4','logs/scheduler-repair-20260910/live-half-final','--half-rate')
./scripts/acceptance/scheduler-short-test.ps1 -Name live-fruc-verified -Exe ./out/build/x64-release/veyra_live_presentation_tests.exe -TestArgs @('logs/video-sdk-trial-20260909/known-pan15.mp4','logs/scheduler-repair-20260910/live-fruc-verified','--fruc')
./scripts/gates/delivery.ps1 -Root .
```

- Release build exit0，`logs/scheduler-build-20260910-final.log`；首轮 LNK1104 因正在运行的 EXE 被占用，正常关闭应用后成功。
- CPU 43 项、worker 12 项 PASS；half-rate 实际 engine 23 项 PASS（10.89秒），FRUC 保留路径 18 项 PASS（6.98秒）。
- `veyra_fg_admission_tests.exe dlss N <1080 clip> <logdir>`，N=2/3/4：各处理 40 真实帧、40 次 NR；分别跳过 8/16/24，Evaluate 32/64/96，恢复 2 次，有效生成 29/58/87，D3D12 debug errors=0；真实输出读回非黑，合法 PTS。单次约 3--5秒。它不是自然运动质量评分。
- `veyra_experimental_backend_tests.exe xess logs/scheduler-repair-20260910/xess-final`：SDK generated=43、debugErrors=0，2.92秒；不是 AMD 实机或屏幕扫描验证。
- `python scripts/acceptance/ui-window-resize.py out/build/x64-release/veyra.exe loop/local/fixed_clips/test_av_1080p.mp4`：24移动、24缩放、4弹出选择器、干净退出，20.12秒；`logs/optimization-goal-20260908/resize-1789042165506228700/result.json`。
- 联合 delivery 23 项 PASS，47.64秒，`logs/delivery/13a7fe7290f643d29c64f6acc8fe8a32/result.json`。包括真实 NR/NVOF、GBV、native4K、播放/暂停/seek、PNG/JPEG、NVENC H.264/HEVC、CFR/音轨/取消；后续仅补充采集统计归属与 XeSS 计数，已分别复跑 live 和 XeSS 回归。

NR CreateFeature 的实际日志为 `id=18 result=0x1 (NVSDK_NGX_Result_Success) handle=non-null seh=0`；上面的 NR Evaluate 数来自实际 graph metrics，DLSS 次数来自实际调用。固定 NR 运行时身份沿用本次已核对的 E16BCF15...1FC8E / NVIDIA Valid，没有替换文件。

## 失败与明确保留项

### 续接审查与 FRUC 定位

本轮继续审查后补齐新窗口首批 command-slot wait 统计，以及文件播放在暂停/seek/对比时取消输出的计数。短测脚本新增 FRUC worker SHA256 和独立运行日志目录；此前 JSON 只记录主测试 EXE，worker 单独构建后 EXE hash 不变，不能据此推断 worker 也没变化。

`--reset-pixels` 在当前基线复现失败。新增失败输出的平移匹配诊断：首对错误输出对应上一真实帧，第二对常对应上一对插值结果（相对当前输入为 -12px，正确目标为 -4px）。因此存在读取旧输出的证据，不能简单说成光流估计不准。

按独立实验依次验证：FRUC 重建后相对时间起点、仅首帧上传等待、所有输入上传等待、D3D11 Wait/Signal/Flush、CUDA legacy-stream 完成信号、官方 `cuCtxRecordEvent` + GPU semaphore、独立完成 fence、持久化参数，以及 D3D11 输出纹理再拷贝共享纹理。上述候选全部未通过 reset 像素检查，均已撤下；Backend/Worker/Protocol 最终与本轮前一致。

诊断性 `cuCtxSynchronize` 加 D3D11 完成通知的版本通过全部 6 个重置 epoch 的像素检查（`fruc-reset-cuda-sync`，5.54秒）。这是同步/可见性问题的线索，尚不能确定具体漏掉的资源交接；该同步版本没有进入产品，不引入逐帧 CPU GPU 等待。后续应检查 CUDA/D3D11 互操作的资源映射、实际完成与缓存可见性，不能再次只凭 API 成功或 event 到达宣称修好。

证据位于 `logs/scheduler-repair-20260910/fruc-reset-*.{stdout.log,result.json}`；对应 build 日志同目录。最初输出桥接纹理 Register 返回3，补共享标志后可初始化但像素仍失败。官方 API 依据：<https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__CTX.html>，仅查阅文档，没有新增或替换 SDK/runtime。

最终审查构建 `scheduler-reviewed-build.log` exit0；`cpu-reviewed` 43项、`worker-reviewed` 12项、`live-half-reviewed` 23项（8.42秒）、`live-fruc-reviewed` 18项（7.02秒）通过。联合短测 `delivery-reviewed` 的23项功能检查通过，但结果落盘缺少 Get-FileHash 导致整体exit1（44.74秒，run `52145ee98a0140b799749c8c3792f106`），保留失败记录并修正模块加载后完整重跑。

修正后 `delivery-reviewed-fixed` exit0，23项全部通过，外部计时42.78秒（gate内部42.43秒）；`logs/delivery/ac24ad4a2e1e4169a171fda66e4b7e84/result.json`。对应最终产品 EXE SHA256 `429C5600E2A4ABF8D72B83B0F658B3190FE5397FE9A11256946B924CABA560F4`。`git diff --check` 通过；Git跟踪/待加入范围未发现runtime、SDK、EXE、DLL、LIB、模型或测试媒体。实卡仍未执行。

- `live-half` 初次因关闭 FG 后倍率比较错误而两项失败；修复后 `live-half-fixed` / `live-half-final` 全过。原失败日志保留。
- `admission-fruc4` 使用普通测试片时 API/资源调用成功，但 valid=0，测试 exit1；明确运动的 `known-pan15.mp4` 得到 valid=87。没有把重复帧计成有效生成。
- `live-fruc-final` 试启用产品 FRUC admission 后，90 候选中 73 跳过、17 次全用于预热，未产出有效生成；原因是恢复每次重启 worker，导致再次迟到。产品入口已撤下 FRUC admission，`live-fruc-retained` / `live-fruc-verified` 全过。
- FRUC 既有 reset-pixels 失败仍未解决。最终保留原 worker，不声称像素质量或轻量 reset 已通过。
- S3 单线程 GPU 所有者状态机、真实源帧进一步 coalesce、全部 reset 耗时分解、固定容量逐帧诊断环，以及 S4 额外提交合并尚未实施。保留双线程互斥 GPU 访问、容量 2 呈现队列和 6 槽环；本次没有增加无界队列、全帧 CPU 回读或每 pass fence wait。
- 下一任务：先以 FRUC 重置后像素与时间戳复现定位恢复链，再决定轻量 reseed 与 admission；实卡表现由用户观察，禁止以此回放代替实卡验收。

未 push、未发布、未打包 runtime，未提交 SDK/二进制/测试媒体。当前可执行文件为 `out/build/x64-release/veyra.exe`；使用 `Veyra.cmd` 启动。
