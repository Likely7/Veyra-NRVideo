# 5060、命名调色预设与图形字幕修复

## 基线与范围

- 用户授权本地修复、构建验收、报告后关机；不发布、不推送。
- 基线 `050811b`，存档 `checkpoint/pre-5060-presets-subtitles-20260918`。
- 隔离分支 `codex/5060-presets-subtitles-20260918`，工作区沿用 `E:/项目/Veyra/worktrees/frame-pacing-20260918`。
- 构建沿用 `E:/项目/Veyra/build/frame-pacing-20260918`；新增证据、日志、临时文件分别放 `E:/项目/Veyra/{tests,logs,tmp}/5060-presets-subtitles-20260918`。

## 实施与验收

1. 对照5060完整日志与UI能力/状态流，修复有证据的开启或状态问题；不以GPU占用反推限帧原因。本机5070测试不冒充5060验收。
2. 调色保存入口提供命名、重名处理、失败反馈，验证磁盘持久化与重新加载。
3. 复用FFmpeg图形字幕解码器和现有FG后字幕层，支持位置、透明度、时间、清屏与切换，缓存有界；验证真实解码及显示，不把文字转换冒充图形字幕。
4. 构建相关目标，运行针对性单测、集成测试与实际GPU/界面验证。记录失败、未验证范围与可用构建。

## 实施结果（2026-09-19 本地时间）

### 5060：修正误分类，保留尚未确定的原因

用户日志 `C:/Users/123/Desktop/5060补帧受限，显卡占用百分之5.log` 中两个处理图均 `sr=0 nr=0 fg=0 nvof=0`，生成数为零，没有 NGX 初始化失败或用户启用效果的记录。因此不能声称该日志证明了 DLSS 在 5060 上调用失败，也不能把低占用等同于显卡算力不足。

2560x1440 YUY2 会话标称60fps、实际约40fps，出现逐帧 `PtsDiscontinuity`（reason10）重置；1920x1080 会话实际约60fps。25ms间隔未超过现有41.67ms断点阈值，旧日志不足以区分设备 discontinuity 标记与原始时间戳异常。没有擅自忽略驱动标记、伪造连续时间戳或取消真实历史 reset。

- 状态面板按实际采集速率计算输出目标，区分“输入帧率不足”“输入时间线异常”“补帧调度降档”“输出未达标”；关闭FG时不报补帧降档。40fps输入输出160fps的4X不再因未达标称240fps而误判FG受限。
- 新增限频 `capture-discontinuity` 日志，包含 driverFlag、clockBreak、原始PTS、回调间隔与样本时间，供该用户复测定位。
- 基线已有仅开补帧自动打开总增强/光流的修复，本次真实UI回归确认 DLSS/XeSS 单独选择均能生成帧、NR未执行；不将基线修复重复算作本次新增。
- **5060 实卡根因仍未完全确定，未宣称已经根治。** 下一步唯一远端任务：该用户在此构建明确选择DLSS4X后复现并回传新日志；核对设置事务与新增时间线原因，再决定设备兼容处理。本机5070结果不代替5060/616.92验收。

### 命名调色预设

根因是保存/应用/删除/导出/导入五个按钮重叠在同一位置；实测又发现调整窗口大小时布局读取旧面板宽度，窄窗口按钮会落到可视区外。

已修复两组工具栏的分列换行与当前宽度布局，名称框有“预设名称”占位。保存要求名称非空，同名确认覆盖，失败弹出明确原因，成功更新选中项和状态。入口：专业模式 → 调色 → 填名称 → 保存为预设。保存至当前应用本地数据目录的 `color-looks.v1`；本机构建为 `E:/项目/Veyra/build/frame-pacing-20260918/runtime_local/color-looks.v1`。

### 图形字幕

复用现有FFmpeg的PGS/DVD/DVB解码器，支持MKV内嵌位图字幕；保留作者画布、多个矩形、调色板、透明度、起止时间及清屏事件。在现有FG后的字幕层显示，随视频区域缩放和平移；字幕菜单沿用主/副轨选择。

缓存采用索引色游程数据与共享不可变图像，沿用异步渐进加载；正常视频处理未新增GPU像素回读。未重编译/替换FFmpeg，PS5 H.264 slice补丁和dav1d依赖保持。

边界：缓存上限仍为全文件64MiB/12万cue，达到上限会在轨道注记中报告后续未加载；多图形语言的长片需要后续按需缓存改造，不能保证任意数量轨道整片都容纳。位图不能通过字体/字号设置重新排字，不新增OCR或导出烧录字幕。未用用户真实PGS长片验收，已用有效封装样本验证三种解码器。

## 实际验收

| 检查 | 结果 |
| --- | --- |
| Release构建、UI合同、ColorLookStore、LiveTiming、RepairContract | 通过；命名保存、原子写入、损坏文件保护、状态误分类回归均通过 |
| 调色UI烟测 | 保存、应用恢复实际参数、删除通过 |
| 跨进程界面操作 | 1180/900像素宽按钮命中、中文名称保存、真正退出重启后找回通过 |
| PGS/DVD/DVB解码集成 | 三者通过；时间、清屏、随机/后退查找、位置、透明度等检查 |
| 图形字幕实际窗口 | 像素差分验证窗口/缩放/全屏两个作者位置、开关、定时清屏通过；截图留存 |
| Money Heist 文本回归 | 32轨均可用、15720cue；异步渐进结果可用，完整扫描约10.1秒，不等同首帧等待时间 |
| RTX5070 DLSS2X/4X/6X | 各15秒，实际原帧增量358/358/360、生成增量358/1074/1800，零预览跳帧；媒体速度约0.996，干净退出 |
| 冷启动只开FG界面 | DLSS和XeSS均实际光流/生成成功，NR未执行 |
| VC-007PRO 4K30 NV12 + NR实时1080 + DLSS4X | 10秒预热后15秒测量，449输入、1347生成、零采集丢帧，正常退出 |

GPU证据：RTX5070/616.56，DLSS `Create result=0x1 (NVSDK_NGX_Result_Success), handle=non-null, seh=0`；Evaluate真实计数和逐帧日志见各 `engine.log`。显示器100Hz，以上为应用生成/提交数，不是物理屏幕刷新数。

失败记录：初版PGS封装缺 `-copyts`、转DVD/DVB缺 `-fix_sub_duration`，导致测试样本时间改变，修正后通过。跨进程编辑测试用 `SetWindowTextW` 未写入文本，改用系统封送 `WM_SETTEXT` 后通过。首轮采集 `Run hr=0x800705AA`、零输入（FAIL，证据保留）；正常关闭原先运行的旧版Veyra窗口后，同参数复测通过，符合资源占用冲突，未把该轮失败冒充通过。

未运行无关全量旧delivery脚本（其固定媒体/产物路径仍为旧布局）；本次运行以上针对性构建、CPU、GPU、界面及采集回归。未做长时稳定性、真实5060、30/40系列或远端卡顿复现验收。

## 命令与证据

工作目录为隔离源码，所有子进程TEMP/TMP设为本任务tmp目录。实际执行的命令形式如下，转码与测试日志均保留：

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory 'E:/项目/Veyra/build/frame-pacing-20260918' -DependencyCache 'E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt' -TempDirectory 'E:/项目/Veyra/tmp/5060-presets-subtitles-20260918' -DisplayVersion 1.4.2beta -Targets veyra,veyra_color_look_tests,veyra_subtitle_loading_tests,veyra_bitmap_subtitle_tests,veyra_live_timing_tests,veyra_repair_contract_tests,veyra_ui_contract_tests
# 后续布局修复另构建 veyra,veyra_bitmap_subtitle_tests,veyra_presentation_pacing_tests
python scripts/tests/make-bitmap-subtitle-fixture.py E:/项目/Veyra/tests/5060-presets-subtitles-20260918/bitmap.sup
ffmpeg -hide_banner -y -copyts -f lavfi -i 'testsrc2=size=1280x720:rate=30:duration=9' -i bitmap.sup -map 0:v -map 1:s -c:v libx264 -preset ultrafast -crf 28 -c:s copy bitmap.mkv
ffmpeg -hide_banner -y -copyts -fix_sub_duration -i bitmap.mkv -map 0 -c:v copy -c:s dvdsub bitmap-dvd.mkv
# dvbsub 同上，实际输入/输出均为本任务 tests 下绝对路径
veyra_bitmap_subtitle_tests.exe <bitmap/bitmap-dvd/bitmap-dvb.mkv>
veyra_subtitle_loading_tests.exe 'C:/Users/123/Desktop/Money.Heist.2019.S03E02.V2.1080p.NF.WEB-DL.H264.DDP5.1-LeagueNF.mkv'
veyra_ui_contract_tests.exe <本任务独立测试目录>
veyra_color_look_tests.exe <本任务独立测试目录>
veyra_live_timing_tests.exe
veyra_repair_contract_tests.exe
veyra.exe <visible-scene.mkv> --smoke-color --smoke-seconds 30
python scripts/acceptance/ui-bitmap-presets.py E:/项目/Veyra/build/frame-pacing-20260918 E:/项目/Veyra/tests/5060-presets-subtitles-20260918/ui E:/项目/Veyra/tests/5060-presets-subtitles-20260918/bitmap.mkv
veyra_presentation_pacing_tests.exe E:/项目/Veyra/tests/1.4.2beta/visible-scene.mkv <dlss2/4/6证据目录> -1 0 <2/4/6> 15 normal
veyra.exe E:/项目/Veyra/tests/1.4.2beta/visible-scene.mkv --smoke-fg-only --smoke-seconds 30
veyra_presentation_pacing_tests.exe capture <capture4-retry证据目录> -1 0 4 15 capture-nr
```

- 可运行构建：`E:/项目/Veyra/build/frame-pacing-20260918/veyra.exe`，本机运行库引用保留；不是新的便携发布包。
- EXE SHA256：`05EB267EEE624538E02A8C452F4EA1D5B804DD884FCD1CD09B838F8E77BFED4E`。
- 构建/CPU/UI结果：`E:/项目/Veyra/logs/5060-presets-subtitles-20260918`。
- GPU日志/CSV、位图样本、界面截图及 `ui/result.json`：`E:/项目/Veyra/tests/5060-presets-subtitles-20260918`。
- 仅清理本任务无效转码中间文件；保留可用构建、有效样本、失败与成功证据。未修改用户源日志/电影文件，没有新包、合并main、推送或发布。

## 关机与次日复核

2026-09-19 次日恢复任务时核对 Windows System 日志：00:08:18 的 User32/1074 记录本任务 `shutdown.exe` 发起关机，00:08:25 的 EventLog/6006 记录服务停止；08:41:56 的6005记录再次启动。关机脚本日志 exit=0，授权的关机动作已履行，不在自动续接时重复关机。

源码修复提交 `4c41ffa`，交付存档 `checkpoint/5060-presets-subtitles-local-20260919`；次日工作区干净、EXE SHA256与上述记录一致。5060原日志仍为4419693字节、最后修改2026-09-18 20:35:12，未收到新构建复现证据，不能关闭其根因待办或将整个目标标成完成。
