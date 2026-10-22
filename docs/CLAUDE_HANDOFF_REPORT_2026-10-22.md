# Claude 中断任务接手记录

本轮在用户当地 2026-10-21 执行；任务目录保留原中断任务的 20261022 标识。用户要求接手完成后，明确决定“Lecram，先放着，不弄了”，因此本轮交付仅包含 AMD 超分关闭与录制修复。Lecram 原树、原件与资料保留。

## 源码与存档

- 工作树：`E:/项目/Veyra/worktrees/screen-recording-fix-20261022`。
- 分支：`codex/claude-handoff-20261022`，基线包含原录制分支 aa27732。
- 20 个 Claude 未提交文件的原始 ZIP、二进制 patch、逐文件 SHA：`E:/项目/Veyra/archives/codex-claude-handoff-20261022`。
- `5614e8d` / `checkpoint/claude-recording-before-handoff-20261022`：未经验证的原始实现存档。
- `88dbc26`：移入 AMD 修复 1b94bae。
- `20a30a6`：录制生命周期、资源释放、队列计数、MFT 收尾、界面与翻译修复。
- 本轮不合 main、不 push、不发布、不改驱动或专有运行库，不触碰真实配对主机和其他工作树。

## 实际修改与原因

承接 Claude 的呈现命令列表取帧、异步 NVENC/系统编码器、音频挂钩、AAC 与分段封装实现，补齐以下问题：

1. 码流队列的字节数原先在解锁后增加，写线程可能先扣减导致无符号下溢，误判磁盘过慢。改为锁内发布计数。
2. UI 可在 record 与 submitted 之间停止录制，原先编码线程可能过早退出。保留待提交捕获，提交/丢弃之后再关闭输入队列。
3. 编码失败后的呈现命令可能仍使用录制纹理，完成判断现在等待相关 GPU fence 已完成才回收，不在正常播放线程逐帧等待。
4. 音轨状态跨线程改写改成原子值；限制未收尾分段数量，上一段仍保存时拒绝新录制，避免资源无界增长。
5. MFStartup/MFShutdown 生命周期串行化，防止旧分段退出与新分段启动交错；系统实时编码事件等待按录制超时配置限制，导出既有超时不改。
6. 退出超时保留的工作线程使用进程生命周期存储，避免静态容器析构时释放仍在使用的 Segment。
7. 保存中/无播放内容时禁用录制点击；倒计时期间片源消失会取消；补齐繁中、英文、日文录制提示。没有提高既有 UI 轮询频率。

NVIDIA 使用 GPU NV12/P010 + D3D12 NVENC，压缩码流在工作线程回收、落盘。系统编码器沿用 MF 后端，NV12 回读发生在录制工作线程；这一后端不是零拷贝。声音取自播放器实际送出的 PCM、在音量/静音之前，不能录到没有开启监听的采集音频。XeSS 插件内部生成帧不在共享纹理池，仍不宣称录到这些帧。

## 构建与自动回归

可复现命令（在本工作树运行）：

```powershell
python -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-claude-handoff-20261022 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.0-rec veyra_qml_ui veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_repair_contract_tests
python -X utf8 -B scripts/i18n/extract.py --check
python -X utf8 -B -I scripts/acceptance/amd-sr-handoff.py after
python -X utf8 -B -I scripts/acceptance/recording-handoff.py long
python -X utf8 -B -I scripts/acceptance/recording-handoff.py real4k --media 4k
```

沿用 2.1.0 已验依赖，-j2，所有构建在单次 900 秒内。最后五目标增量构建收据：`logs/codex-claude-handoff-20261022/build-20261021-203437/receipt.json`。

实际执行结果：Repair 272 checks / 0 failures；UI contract 通过（含 384 布局情况）；UI i18n 0 failures；Qt Quick 105 passed / 0 failed；翻译提取 2659 条、三目标语言缺译和占位符错误均为 0。最后回归日志：`logs/codex-claude-handoff-20261022/regression-204301`。

应用 EXE SHA256：`37d18c58ddb7378ac2149812b6c4b87c3c3350e8ee32aacd97be90323a0e4ea2`。C++ 产品源码对应 20a30a6；后续提交的产品变动仅录制 MouseArea 的 objectName，另含测试与文档收尾。每次录制报告记录所用 EXE SHA，测试包另有完整清单。

## 实际播放器与文件验收

所有案例使用独立数据目录、AppData、缓存与 TEMP，不操作用户真实配置。通过生产 bridge 打开片源、录制并实际解码分析输出；退出码 0 单独不算通过。脚本拒绝无帧、缺音轨、引擎卡住、编码错误及不完整分段。合成片用每秒闪光/蜂鸣对齐；30fps 限帧测试使用 50ms 闪光，避免一帧脉冲恰好被抽掉。以产品规格既有 ±50ms 作为本机音画偏移检查范围，但本轮不是 30 分钟稳定性验收。

| 项目 | 证据与结果 |
|---|---|
| AMD 超分开关 | `sr-toggle-after-203525`：真实 Radeon 核显上 FSR 开启后 3840×2160 / sr=1，关闭后恢复 1920×1080 / sr=0，UI srActive 同步 |
| 约62秒连续录制 | `rec-long-203814`：3702帧，61.78秒，H.264+AAC，61次闪光/蜂鸣；偏移中位 -1.3ms、范围 -2.4至-0.2ms；编码器报告丢帧0 |
| 原生4K素材 | `rec-real4k-204145`：3840×2160、248帧、8.29秒、有AAC音轨；不是1080放大冒充4K。此素材不作闪光音画同步证明 |
| 系统编码器 | `rec-mf-203937`：1080p60，745帧，有AAC；偏移中位 -0.5ms |
| AMD核显录制 | `rec-amd-202714`：真实 AMD 硬件 MFT 输出1080p60及AAC；后续更改只涉及失败/收尾边界，未外推独显AMD型号 |
| 30fps限速 | `rec-limited-203619`：250帧、8.36秒，帧间隔中位33.32ms，偏移中位-1.0ms |
| 连续启停 | `rec-rapid-203638`：5次独立录制均收尾并保存 |
| 录制按钮 | `rec-button-204058`：真实界面处理函数与计时器，3→2倒计时、取消、再次开始、停止保存；494帧，音画偏移中位-0.9ms。通过 MouseArea 信号触发，不冒充原生坐标点击或视觉验收 |
| 播放器静音 | `rec-muted-203754`：仍录到AAC与蜂鸣，偏移中位-1.1ms |
| 关闭录音/HEVC | `rec-silent-203134` 确实无音轨；`rec-hevc-203114` 为HEVC+AAC |
| 停止播放自动保存 | `rec-stop-203718`：文件正确收尾，AAC保留 |
| 无效目录 | `rec-bad-directory-204049`：明确错误，无录制开始、无输出文件 |
| 改调色设置 | `rec-settings-204656`：同一文件继续录863帧，音画偏移-47.1至-0.3ms，不能称切设置时零卡顿 |
| FSR切输出尺寸 | `rec-resize-fsr-204724`：1080→4K→1080三段均有画面和AAC，分别253/300/364帧（按时间顺序） |
| 暂停恢复 | `rec-pause-204755`：620帧，约3.8秒暂停间隔保留，声音持续时间线，恢复后的音画偏移在-9.9至-0.4ms |
| 4倍补帧录制 | `rec-fg-204821`：2966帧/约12.44秒，音画偏移-6.2至-0.6ms；DLSSG真实Create和Release返回0x1，evaluates=2314，日志确认真实生成帧呈现，不把文件帧率当物理屏幕刷新率 |

上述日志、报告、输出片统一位于 `E:/项目/Veyra/{logs,tests}/codex-claude-handoff-20261022`。切效果、FSR切尺寸、暂停恢复与4倍补帧的最终重跑回执在该目录对应最新 `rec-settings-*`、`rec-resize-fsr-*`、`rec-pause-*`、`rec-fg-*`。

低延迟限制：本机 CPU 呈现日志抽样，不录制基线14样本与连续录制64样本的 recordSubmitMs 中位分别0.139/0.147ms，slotWaitMs 中位均0。它只能说明这些抽样没有明显的 CPU 等待增长，不能据此声称 GPU 增量为零或屏幕光学延迟不变。显示/采集硬件端到端延迟本轮未测。

## 失败与边界

- 隔离环境（新 CUDA 缓存、关闭预热）中切到 DLSS SR，CreateFeature 期间超过8秒没有播放进展。接手版本不录制的对照同样复现；未修改的2.1.0 EXE（SHA 5b8a8920…a03a65）在 `rec-released-resize-204431` 也复现，堆栈位于 NVIDIA PTX JIT 编译器。该案例明确失败，不计入验收。不将首次编译卡顿直接断言为永久死锁，也不宣称本轮已修复 DLSS 初始化。
- 首轮限帧测试因为单帧闪光被30fps抽样全部错过，不能计算同步；修正为50ms闪光后重跑通过。首轮无效目录夹具忽略了设置拒绝，实际落到默认目录；该报告标记失败、测试输出已移回E盘，夹具修正后真正验证失败路径。
- 一次停止案例仅出现 Qt 剪贴板重试错误；验收只排除此条已知外部提示，其他引擎/编码错误仍失败。
- 未验证真实采集卡、真实主机串流、HDR录制、RTX20/30/40与其他AMD/Intel型号、长时间显存稳定和光学显示延迟。测试窗口与本机短测不能外推所有硬件。

## 本地测试包

供体为已发布2.1.0 NVIDIA/AMD便携目录，原样运行库/模型/着色器逐文件验证，仅替换本轮程序、QML、翻译和说明。打包脚本 `scripts/acceptance/package-recording-handoff.py --zip`，产物 `E:/项目/Veyra/test-packages/codex-claude-handoff-20261022`，回执 `reports/codex-claude-handoff-20261022/packages.json`。包内仍显示 `2.1.0-rec`，外部 `rec2` 用来区分旧无声录制包；不是正式发布。
