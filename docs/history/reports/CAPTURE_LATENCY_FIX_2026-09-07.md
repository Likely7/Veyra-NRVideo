# 采集延迟修复 — 2026-09-07

状态：正式源代码与本机 EXE 已更新，实卡吞吐短测和软件联合回归通过；独立复核尚未完成，手柄体感/音画同步/实际显示帧间隔仍由用户确认。不是公开发布或新的阶段 checkpoint。

## 根因与修复

旧实现先 Run 采集，再初始化 NGX；回调保留唯一 RGB32 `IMediaSample`，占住生产者的单缓冲，积压旧帧。播放器拿第一张旧帧重建时间戳时钟，又对后来的新帧主动等待。诊断隔离到无 GPU/无 DLSS 后仍可复现：29 帧/4 秒，一次等待 940.79ms；仅去掉第二层等待，185 帧/4 秒。完整原始记录在 `logs/capture-diag-20260907/FINDINGS.md`，其中“退到 YUY2/10”的猜测已经由实际协商日志证伪。

实施内容：

- `src/source/CaptureCardSource.cpp` / 对应头文件：configure/start 分离，图与显示器就绪后才 Run；回调复制到自有的两张 AVFrame（一个读者持有，一个容量1待取帧），立即归还驱动 sample。消费者慢时覆盖待取帧而不积压；丢帧、时间戳断点触发历史 reset。记录实际上游 subtype、真实 callback cadence 和取帧年龄。
- `src/engine/EngineController.cpp` / 对应头文件 / `LivePresentationTiming.h`：采集不再等待绝对源 PTS。无 FG 立即提交最新帧；FG 的生成帧先提交，真实帧按每一对当前 host-time 锚点最多等待半个输入间隔（硬上限33.33ms）。文件播放仍用原音频主时钟和 PTS 调度。全关增强时跳过 NGX 创建。
- `src/engine/VideoPresenter.cpp`、`src/gfx/CommandSlotRing.cpp`、`src/pipeline/EnhanceGraph.cpp` / 对应头文件：图与显示共用 command-slot 游标，不再固定复用显示 slot；窗口 resize 限频并检查实际 buffer extent。最小化不再反复重建1×1 swapchain。
- `apps/veyra/main.cpp`：采集状态显示输入/处理 FPS、丢帧和 callback→Present 返回的程序内部耗时；不再显示虚假的近零采集 lateness。smoke 测试不改用户 Recent。
- `src/base/Log.cpp`：允许读取运行中的日志，仍排斥其他写入者。
- `tests/unit/LivePresentationTimingTests.cpp`、`tests/integration/CaptureSourceTests.cpp`、`CMakeLists.txt`：纯时间策略回归，以及真实设备的延迟启动、消费者停顿、内存所有权、latest-frame/drop/reset 验证。
- `scripts/gates/delivery.ps1`：增加 `-VisiblePlayer`，这轮播放器回归在可见窗口执行；不把隐藏窗口的成功提交当成可见显示证明。

没有改变驱动、OBS、远程软件、用户设置或专有二进制。用户删除的 `validation/fixed_clips/test_h264_1080p.mp4` 保留。实卡测试不保存采集画面；图片/导出回归使用项目的合成测试媒体。

## 实际命令与结果

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
out/build/x64-release/veyra_live_timing_tests.exe
out/build/x64-release/veyra_capture_tests.exe capture:0:0:2
out/build/x64-release/veyra.exe capture:0:0:2 --no-nr --smoke-seconds 6
out/build/x64-release/veyra.exe capture:0:0:2 --smoke-seconds 6
out/build/x64-release/veyra.exe capture:0:0:2 --fg --smoke-seconds 6
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -VisiblePlayer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate preflight
```

采集测试使用同一实卡 USB2 Video、MJPEG 1920×1080/50、原音频索引2。NR/SR/FG 原设置为1/0/0，因此第二/第三条应用测试分别为 NR、NR+FG。其他用户配置复现时必须核对日志中的实际开关及 Evaluate 计数，不能只看退出码。

| 首轮实卡短测 | 实际源处理 FPS | 源帧 / 生成帧 | 采集丢帧 | callback→Present返回 p95 |
|---|---:|---:|---:|---:|
| 增强全关 | 49.82 | 276 / 0 | 0 | 3.672ms |
| NR | 49.83 | 181 / 0 | 0 | 4.770ms |
| NR + FG | 49.53 | 201 / 200 | 0 | 16.277ms |

这些是每个6秒应用窗口中的有效采集阶段，NR 模式有约1–2秒初始化，不拿总6秒除计数伪造运行 FPS。原始日志：`logs/capture-fix-20260907/{off,nr,nr-fg}.log`。NR181/NVOF180 与 NR201/NVOF200 都在实际 RTX/runtime 上执行，不是桩或纯 counter。

最终 EXE SHA256：`4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9`。

最终 EXE 实卡复验：`final-nr-fg.log`，exit0，49.40 processed / 50.13 callback fps、195源帧/193生成帧、0采集丢帧、NR195/NVOF193、callback→Present返回 p95 16.395ms；不把 history reset 跳过的生成帧算成功。

时间策略单测7/7；实卡 source 测试8/8。消费者保留读帧停顿300ms时仍收到16个callback、覆盖14帧，读者持有的数据不变，恢复时读到最新帧（callback age 8.889ms），不是排队读旧帧。

收尾状态校验发现把 `needs_review` 错放入 phase 子状态（该字段只允许 `gate_passed` 等）且阶段名称不一致；已按现有 schema 修正为顶层 `needs_review`、Phase7 `gate_passed`，未修改门禁或控制 hash。保留失败记录 `logs/capture-fix-20260907/preflight-final.log`；随后复验另存 `preflight-state-corrected.log`。

软件联合 gate：exit0，21 checks，47.309秒，`logs/delivery/01b72df768524af9ab0aa8d2e3dbb59e/result.json`，EXE hash 与最终版本一致。包括真实 NR/NVOF/GBV、4K实时档文件播放和音频同步、暂停seek/resume、4K图片保存、4K H.264/HEVC各23帧输出+音轨解码、NVENC取消/partial。preflight71/71；`git diff --check` 无空白错误。没有跑30分钟耐久。

## 限制与下一步

- callback→Present返回不是 HDMI→屏幕光子的端到端延迟，也不是GPU完成时间。未测系统合成器实际显示了多少生成帧，不宣称屏幕100Hz或已证明FG均匀显示；GPU超预算时，CPU配对间隔不保证物理显示间隔。不得拿吞吐改善冒充所有画质/延迟问题都解决。
- 这是当前 USB2 的1080p50实卡证据；历史 OBS 的 USB3/YUY2/1080p60记录保留，不混为同一次输入模式。4K实卡模式仍未测。
- 原音频索引2对应Realtek麦克风，未擅自改成索引0的USB2数字音频。用户应在采集设置选择采集卡的USB数字音频，不能按旧索引猜设备。
- 独立只读 Reviewer 尝试异常结束，未返回最终审查结论；此前版本的 REVIEW_F6 PASS 不覆盖本补丁。保留 `needs_review`，未创建新阶段 checkpoint。
- 下一项：用户在重开的 NR 预览中操作主机确认体感和音频；若仍卡，读实时日志中的 callbackFps、processedFps、drop 和各段耗时，不再先怀疑驱动或关闭无关软件。
