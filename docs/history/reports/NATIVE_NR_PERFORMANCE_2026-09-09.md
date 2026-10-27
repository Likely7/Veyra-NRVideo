# 原生 NR 性能复核与本轮修复

日期：2026-09-09。状态：Phase7 in_progress；计时修复已验证，整体性能问题未解决。用户明确不再做实卡颜色 A/B，本轮未打开采集卡，未修改 Magpie、运行时或远端 Release。

## 结论与证据边界

用户反馈 Magpie 同时开启 SR/NR/FG 仍流畅，Veyra 原生 NR 卡顿且 GPU 占用高；用户补充 Magpie 可拖动改变顺序，实际体验差异不大。不能用一次 NR→SR 日志把整体差距归因于顺序，更不能把用户的实际体验判为错误。

本轮额外运行了 Veyra 原生 2560×1440 NR：90次 Evaluate、89次 NVOF，88条已完成 GPU NR timestamp，中位 **9.74784ms**、平均 **9.76670ms**、P95 **10.11734ms**。Magpie 已存同尺寸原生 NR 的120帧窗口平均为 **9.718–10.095ms**。这支持“同尺寸 NR 单次执行没有显示出倍数级差距”，不证明两者全管线、同素材、GPU占用或实际显示延迟相等。Veyra 测试素材为固定4K视频缩到1440p，没有同时启用SR/FG。

Veyra 用户运行日志中的原生4K NR 中位22.450ms、P95 24.268ms是真实 GPU NR 阶段计时。不能据此声称5070无法运行所有4K输出组合，也不能承诺仅修计时就把4K NR降到10ms。

## 自然运行日志

冻结证据位于 `logs/native-nr-performance-20260909/`，含 `veyra-app.log`、`magpie.log`、`magpie.1.log`、`analyze.py`、`veyra-summary.json`。Veyra日志为UTC，Magpie日志为本地UTC+8。阶段摘要按应用revision区间分组，边界可能包含少量上一revision延迟回收的timestamp；中位值不是逐帧配对的竞品benchmark。

| 运行片段（本地时间） | 实际链路/观察 |
| --- | --- |
| Magpie 21:47:13 | SR1535×971→2276×1440，然后NR2276×1440、FG2276×1440；NR GPU窗口平均9.227/9.279ms |
| Magpie 21:52:14 | NR1535×971、SR→3840×2160、FG backbuffer2560×1440；NR GPU窗口约5ms |
| Magpie 19:55 | NR2560×1440；GPU窗口平均9.718–10.095ms |
| Veyra revision15 21:37:55 | 输入1080p60、SR4K、NR4K、FG4K；NR22.450ms；callback到Present返回P95约71ms，丢帧持续增加 |
| Veyra revision16 | 恢复实时NR1080，NR约5.929ms；callback到Present返回P95约29.3ms |
| Veyra revision21 21:39:31 | FRUC2X；处理CPU P95约335ms、画面年龄约346ms；worker约每330ms重建，生成数不增长 |
| Veyra revision27 21:42:14 | NR关闭、处理CPU约2ms，但画面年龄约109ms；设置重算缓存帧把约49ms旧帧年龄加入新时间线 |

Magpie来源行：`magpie.1.log:2198/2351/2398/2409/1531/1575–1671`；最新组合 `magpie.log:390–393`。其中确有超分4K中间纹理，不能说“根本没开4K”。但没有在这些保留片段中找到NR实际3840×2160的对应测试，不能把屏幕上的“4K”标签当每一阶段的尺寸证明。截图13.529ms不是HDMI到显示的延迟；FG的0.037ms也不能直接当完整GPU插值成本。不同阶段存在重叠/嵌套计时，不能简单相加。

## 已落地的改动

- `FramePacket.h`：帧包携带采集回调的单调时钟时间。`CaptureCardSource.cpp` 将真实 arrival 与对应帧一起传递，供时间线和画面年龄使用。
- `EngineController.cpp`：运行中的采集设置事务读取新帧；等待时保留事务与回滚信息。暂停时才重算缓存；等待途中暂停也能转回缓存。缓存重算不建立新的live时间线。新epoch以回调时间锚定，不以处理开始时间锚定。
- `CaptureTiming.h`、`CaptureCardSource.cpp`：区分连续回调间的时钟异常和消费者漏读；driver discontinuity跨mailbox覆盖保留。Drop仍使全部历史reset，没有删除reset规约。
- `EngineController.cpp`：实时呈现线程复用会话级DeadlineWait，消除每批次创建/销毁定时器。双批次容量和FG因果lookahead保持。
- `LivePresentationTimingTests.cpp`、`LivePresentationTests.cpp`：覆盖回调断点保留、新帧设置、暂停缓存预览和真实引擎事务。未修改颜色公式、NR参数、处理顺序、默认NR策略或导出检查。

## FRUC 未解决，候选已撤下

确认旧实现每次reset重建worker，造成“耗时→丢帧→reset”的循环。尝试了状态更新bSkipWarp、延后重建/保持未播种worker、进程内相对时间基准及首对预热。像素测试未通过，不能只因API返回成功就交付。

重要补证：撤回所有FRUC生产改动后，原实现的重置后像素检查仍失败。`nrperf-baseline-fruc-reset.log` 中epoch1/2首对MAE等于重复上一帧；epoch3/4第二对误差更高，而API返回0且repeated=false。异常不只存在于轻量reset候选；也不能武断归咎于SDK，需要继续查共享纹理生命周期、同步、输入首帧和时间语义。

保留 `veyra_fruc_tests.exe 2 <pan.nv12> --reset-pixels` 作为明确失败的附加复现入口。常规既有短测仅验证初始平移像素和静态reset可调用，未曾覆盖本项。不能用常规短测或delivery通过覆盖这个失败。

本地候选补丁：`logs/native-nr-performance-20260909/fruc-recovery-candidate.diff`，含前轮graph改动上下文，仅供人工核对，禁止整份盲目套用。失败日志均在 `logs/baseline-repair-20260909/nrperf-fruc*.log`。最终FRUC Backend/Worker/Protocol及graph FRUC生产段保持本轮前行为，因此重建循环仍存在。

## 本轮实际验证

下列命令由 `python logs/baseline-repair-20260909/run.py <标签> <命令...>` 包装，每次外部timeout290秒。全部为本机RTX5070，测试之间串行，不占实卡。

1. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <项目目录> -Preset x64-release`：exit0，`nrperf-final-build.log`。
2. `out/build/x64-release/veyra_live_timing_tests.exe`：22项PASS，`nrperf-timing.log`。
3. `veyra_live_presentation_tests.exe loop/local/fixed_clips/test_av_1080p.mp4 logs/native-nr-performance-20260909/live-dlss`：12项PASS，实际NR/DLSS2X/4X、设置/暂停/恢复/退出。
4. `veyra_live_presentation_tests.exe logs/video-sdk-trial-20260909/known-pan15.mp4 logs/native-nr-performance-20260909/live-fruc --fruc`：12项PASS，使用既有FRUC；不证明重置后所有插值像素正确。
5. `veyra_fruc_tests.exe 2 logs/video-sdk-trial-20260909/pan.nv12 --reset-pixels`：**exit1，未解决**；`nrperf-baseline-fruc-reset.log`。
6. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File logs/baseline-repair-20260909/delivery-env.ps1 -Root <项目目录>`：原版delivery23项PASS，45.671秒，`logs/delivery/d3b2dd62224a4df986941a8a4a1cb8a6/result.json`。包装仅导入系统Utility模块，未改gate；非preflight或实卡验收。
7. `veyra_quality_probe.exe --input logs/native-nr-performance-20260909/native1440.mp4 --native --guidance motion --frames 90 --json-file logs/native-nr-performance-20260909/native1440.json`：exit0，90NR/89NVOF，失败0，正常路径像素回读0；真实NR阶段timestamp来自`nrperf-native1440.log`。probe为吞吐运行，CPU fence计数86不能解释为实卡每帧必然等待的额外延迟。
8. `git diff --check`：PASS；NR固定SHA一致；没有提交SDK/runtime、push或发布。

可运行程序：`out/build/x64-release/veyra.exe`，SHA256 `8E1A694229257A66DA5F36DE439423632631B310E81258EA032C92CB5FFA159D`。根目录 `Veyra.cmd` 指向本机构建。旧GitHub包没有更新。

## 下一步执行边界

下一条任务：在保持用户现有顺序的条件下，给同尺寸同倍率的完整采集管线建立逐段CPU等待/GPU执行/呈现节奏证据，再修已定位的热点。以同一源尺寸、NR尺寸、SR质量、FG输出尺寸、实际输入/显示帧率匹配，分别记录源回调、mailbox读取、上传slot等待、graph锁等待、NVOF、SR、NR、FG、GPUready和Present提交。不能用理论FPS或GPU占用百分比单独归因。用户未批准改变顺序，不自动添加或替换产品路线。

FRUC分支须先通过重置后的真实像素复现，再恢复轻量化工作；不要盲目跳过历史reset或把重复帧计作有效生成。显卡负载、自然运动、实卡感受与长期稳定仍未验收。已有控制hash漂移保留，没有改控制面自我放行。
