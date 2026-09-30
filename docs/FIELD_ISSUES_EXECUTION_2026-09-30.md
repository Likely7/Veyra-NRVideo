# 2026-09-30 用户反馈修复执行记录

## 授权与修复前存档

用户要求先存档，将 RTX40 改用 SF-v2、RTX50 保留 Lecram，NR 节点提供两种运行库选择，淘汰新候选中的旧40运行库，随后按 Claude A–K 方案排查修复。

工作区 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927`；独立分支 `codex/field-issues-20260930`，HEAD `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`。已有 UI/NR/FSR/XeSS 工作保留，不 commit/tag/merge/push/Release，不派子 Agent。

存档 `E:/项目/Veyra/archives/field-issues-20260930-start`：1096 份源码哈希、184 份修改/未跟踪文件增量副本、working/index diff、状态、worktree 身份与原反馈方案。不可变基线 SHA256 `9914a06b381fb6d5b90247d4169065b0c9503f84c48dd2272f571b771a18d354`。旧 FG guard 开工 PASS；后续使用 `python -B scripts/acceptance/field-issues-control.py`，新批次追加独立 amendment，不重写初始哈希。

原方案 `E:/项目/Veyra/reports/field-issues-20260930/FIX_PLAN_1.4.4_FIELD_ISSUES_2026-09-30.md` 保持原件。其推测不作为已证实故障原因。

## 顺序与工程判断

1. NR 双版本：20/30/40 默认 SF-v2，50 默认 Lecram；手选保留。现有运行库全链共享，选择必须原子同步所有 NR 层。旧 Community 值迁移为 SF-v2；新候选和 2.x 打包去掉旧40文件，历史 beta/1.4.4/下载原件保留。
2. P0 A/B/C/E：核查原日志，补齐 DRED/device-removed、驱动错误、采集协商与实际呈现策略诊断，修复已成立的控制逻辑。设备仍存活的 fence timeout 与实际设备移除分开处理。
3. D/F/G/H：HDR10/scRGB、显示比例、各消费者运动输入、补帧 invalid 分类。5090 先判定 invalid 原因，不自动改变倍率语义、不将预览去重扩展到导出。
4. I/J/K：5/6/7K ID 追加以保留旧预设；8K 红屏先复现/隔离，重连做有界恢复。每片记录构建、定向回归与实卡未验项。

不能据单份日志硬编码“572 为最低驱动”；优先使用 NGX 能力与真实返回码。GC573 的 OBS 状态关联尚未证明 15 FPS 根因；厂商控制须核对固定上游来源、许可证和设备匹配；4K X 不套用其他型号 HID 请求。

## 原样运行库

|选项|内部路径|身份|
|---|---|---|
|RTX 50 · Lecram|`runtime/experimental/nvngx_dlssnr.dll`|310.8.3.0 / 165840496 / HashMismatch / F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC|
|RTX 20–50 · SF-v2|`runtime/experimental/nr-ampere/nvngx_dlssnr.dll`|310.8.SF.0 (310.8.2.0) / 165830144 / NotSigned / 6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927|

`nr-ampere` 是历史磁盘目录名，不限制 SF-v2 在 RTX30。本机 RTX5070 前轮 AB：SF-v2 NR-only 慢 4.52%，NR+DLSS 2× 慢 6.63%，保留 Lecram 默认合理。20/30/40 实卡性能及长期稳定未验。运行库不 patch、不入源码 Git。

## 实际进度

- NR：两选项接入共享链、列表和节点编辑器，旧 Community=1 读取时迁移到 SF-v2=2。20/30/40 新配置默认 SF-v2，50 新配置默认 Lecram，保存的手选优先。新 staging/2.x 包去掉旧40 DLL；下载原件、历史包和桌面源码不动。实际切换两层 NR、列表/节点、暂停 seek/resize/resume、预设和重开均已通过。
- NR 失败事务：缺少 SF-v2 的负例首先发现原路径关闭 NR；已修复为重新创建上一份工作的 Lecram 图，界面按精确 revision 回滚，播放继续。新选项不会因为一次失败把工作状态留在半途。
- E（用户追加：所有效果关闭也撕裂）：显示同步独立于低排队开关；新默认 Automatic，全屏 VSync、窗口允许撕裂，手动选择保留。DXGI/XeSS 及等待失败回退统一遵守。实际纯播放六阶段验证全屏 `sync=1 flags=0`，无需开启 NR/SR/FG/RTX HDR。物理屏幕撕裂尚未拍屏验收。

|原方案|本轮代码与软件证据|尚未完成或不能据此宣称|
|---|---|---|
|A / K4|创建设备前启用 DRED；记录真实移除原因、breadcrumb/page-fault、近期显示驱动事件、adapter/driver、fence 请求/完成值；`UINT64_MAX` 不再冒充 fence 完成。存活设备等待超时与实际移除区分。|40 系两小时、真实 TDR、10 秒自动完整重建设备/采集、反复故障自动关 NR 未完成。不能宣称已根治40系卡死。|
|B|NGX PlatformError/NotSupported 明确中文提示和真实返回码；使用能力查询给出的驱动要求及实际驱动信息。|不锁死未经验证的 572 下限；566/596 对照不能证明所有中间版本兼容。|
|C|GC573 属性集控制按 P010/P016 关闭、其它格式开启，每次重新连接前执行，记录 Set/Get HRESULT、请求/读回、设备路径和选中格式。来自固定 libdshowcapture `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`，LGPL notice/license 已附。|GC553 UVC、Elgato UVC HID、强制开关 UI/色彩媒体类型扩展未做；4K X 没有型号证据，不发送其它设备请求。GC573 15fps 与红色是否消除需实卡，仍不能证明限速根因。|
|D|HDR 输出默认 HDR10；无 FG 可选 scRGB，有 FG 保持 HDR10 合同。共享图、旧界面/QML、预设/session 同步。TrueHDR HDR10 有限像素/JXR及 scRGB 多颜色层 GPU 检查通过。|真实 HDR 屏 + NVIDIA App Smooth Motion 鬼影/两格式亮度一致性未验。|
|F|共享 DAR/适应/填充裁切/拉伸/1:1/16:9/4:3/21:9；呈现、provider region、XeSS motion、鼠标、比较分割线、QML 影院比例使用同一变换。12 个 GPU 呈现截图通过。|产品截图仍保存处理输出纹理，保留原尺寸合同；未改成包含窗口缩放黑边的呈现截图。旧 Win32 没有新增七档比例选择 UI；共享 DAR 渲染生效。|
|G|FG/SR/NR 独立 zero/flow；XeSS Automatic 默认 zero，DLSS/SR/NR 保留原 flow 默认。无需外部 flow 时跳过会话，NR 抗闪烁需要 flow 时保留独立计算。FSR SR zero 使用源尺寸的零纹理/正确 pitch；zero XeSS 保留有效历史。真实 SR/NR zero、XeSS 4X zero/flow、保存重开通过。|重复栏杆120fps真值 PSNR/SSIM A/B 未完成；不能断言光流就是竖纹根因，不能声称 zero 普遍画质更好。FSR SDK 内部光流不受这个外部 motion 选项关闭。|
|H|无效生成帧分为 provider disabled / duplicate suppressed / both；每秒记录内容帧率策略、传输 fps 与计数。|5090 实卡闪烁矩阵未做。未把“2X”偷偷改为插3帧，也未将4X扩成8X；会改变倍率、容量和延迟，须基于新日志确认后单独处理。|
|I|旧 Qhd/4K/8K=0/1/2 保留，追加5/6/7K=3/4/5；QML旧1/2/3保留，新4/5/6。列表/节点/导出/旧设置、验证器、预设/session 全部同步，243合同断言与保存往返通过。1080p→5/6/7K+默认实时NR各30秒通过，实际输出5120×2880/6144×3456/7168×4032。|此测试NR内部1920×1080，不是原生5/6/7K NR；不扩展为其他显卡验收。|
|J|最终5070真实共享导出器：GTA4K→8K SR+原生7680×4320 NR，及SR-only对照，HEVC/CQ各120帧成功。全帧解码成功，缩至320×180 area后每帧RGB均值/红像素比例统计均未检出整帧红色异常。|未复现反馈者5080问题，不能宣称根治。没有静默夹值/更换编码参数来掩盖未查明原因；NR非有限值保护计数、不同源/编码器二分仍未做。|
|K1 / K2|原生 swapchain 只对 `E_ACCESSDENIED` 在有效 HWND 有界重试最多3次、50ms间隔，仍失败真实报错；架构 NR 默认值已实现。|没有额外窗口/副显示器；实际4070S偶发拒绝和40系/20系NR未持卡验证。|

## 构建、回归和失败记录

最终当前构建 `E:/项目/Veyra/build/field-issues-20260930-clean`；候选 `E:/项目/Veyra/tests/field-issues-20260930/candidate-final`。日志、临时文件分别在 `E:/项目/Veyra/logs/field-issues-20260930`、`E:/项目/Veyra/tmp/field-issues-20260930`。构建900秒以内，单次测试300秒以内，TEMP/TMP 仅子进程设置；Qt staging 工具也已增加 E 盘临时目录隔离并验证。完整 target 命令和固定依赖来源参数见 `tmp/.../build.py`、`build.cmd`、`init.cmake` 与 build 日志；本次没有宣称整个旧 probe 默认 all-target 通过。

- 干净重建三次均 exit 0：`build-clean-r1.log` / `build-clean-r2.log` / `build-clean-r3.log`，422项全部从源编译；第二次包含 session sidecar 修正和 motion-policy 日志，第三次将显示同步移至通用显示页、FG分类日志扩展到文件播放、收紧E_ACCESSDENIED重试边界。之后新增 Win32 目标与设置布局目标14项新对象编译/链接通过，未改动头文件或共享库源，`build-legacy.log`。
- `unit-clean-r2/summary.json` 五项 exit 0；contract 243 checks/0 failures，effect chain、旧/新预设、session、NR 架构/历史迁移断言通过。`qml-clean-r2` 三项 exit 0；`extra-clean-r2/ui-contract.log` 通过。
- `nr-live-clean-r1` 实际两层NR切换 + 重开 exit 0；`nr-reject-clean-r1` 缺 SF DLL 后恢复 Lecram exit 0；`display-clean-r1` 六种实际 Present 策略全部通过。这些使用第一份完整干净构建，后续第二份仅日志、session sidecar 修正，最终再核对候选身份。
- `options-clean-r3` 第二份构建真实UI live/restore 均 exit 0，列表/节点公共 HDR/分辨率/motion 字段与显示比例恢复正确。
- `gpu-clean-r2` 五项 exit 0：NVIDIA/AMD FSR 独立输出、同 HWND 多次 FSR/XeSS/native 往返（NVIDIA18周期774生成帧、AMD15周期660生成帧，D3D debugErrors=0）、DLSS4/6/4X。
- `extra-clean-r2` 五项 exit 0：UI合同、12个preview截图、TrueHDR HDR10/JXR（最后峰值401.505nits）、scRGB颜色链（debugErrors=0）和 XeSS4X+VSync+低排队关闭六秒持续播放。数字是软件纹理或SDK计数，不是屏幕色度或物理刷新率。
- 最终候选重复相关验收：`unit-final` 五项、`qml-final` 三项、`nr-live-final` live/restore、`nr-reject-final`、`display-final` 六个实际策略、`options-final` live/restore、`extra-final` 五项均 exit 0。`options-final` 额外验证真实 FSR SR3.1.5 zero→flow→zero，所有context evaluations有进展、failures=0；DLSS SR/NR zero、XeSS4X zero/flow仍通过。旧设置96/192DPI和滚动三项 exit0，`legacy-layout-final`；未宣称旧Win32新七档缩放菜单已接入。
- `resolution-clean-r2/measurements.json`：5/6/7K三段各30.357/30.125/30.526秒；整卡显存峰值5722/6490/7428MiB，三段SDK额外内存包含在采样总量中。各段记录到的SR GPU p95最大6.812/8.049/10.307ms，NR GPU p95最大7.962/8.102/8.338ms。是该素材、默认实时1080 NR的短测，不是额外帧延迟或全型号性能承诺。
- `export8k-final` / `export8k-no-nr-final`：真实NVENC D3D12 HEVC，120帧，7680×4320，30/1，4.000000秒，导出/解码exit0，red_frames=[]。源由用户指定GTA素材派生，RGB统计只用于整帧红屏检测，不代表原始像素画质或NR无NaN证明。整卡显存峰值含NR11528MiB、无NR7805MiB（约11.26/7.62GiB），不能因此认定5080红屏由显存导致。初次脚本错把CQ写成CBR且未设码率，正常被拒绝；随后4秒包含终点导出121帧，改3.98裁剪门槛后获得120帧，失败日志保留。
- 保留失败证据：初次构建漏 include/旧 fsr_probe SDK include、runner 不存在 target；NR 负例原来降级关闭NR；输出比例的两像素舍入断言阈值不合适；旧 TEMP 的 field-render sidecar 污染预设测试，隔离 TEMP 后全通过并补幂等清理；options 最初4K→4K被SR正常旁路，改1080p源；Timer在Qt.quit期间再次触发重复保存，停止Timer后通过。
- MSVC本机只有中文 `/showIncludes`，Ninja `msvc_deps_prefix` 检测成乱码，头文件增量变更曾漏编译、混合ABI导致 EffectChain 假失败/访问异常。`build-rendering-r1` 和相应混合候选不作为交付证据；改为新目录 + 每次 `--clean-first`，全部对象重建后问题消失。没有安装/修改全局工具链语言。

尚待真实设备：RTX20/30/40/5090、GC573/4K X、HDR实屏/Smooth Motion、物理撕裂/延迟、长时稳定。硬件缺口不冒充软件通过。

## 本轮本机候选与使用边界

`candidate-final/veyra_qml_ui.exe` 大小13889536字节，SHA256 `ac86e3c2a1a0c9389401d9b0d7c5488683bc66b5de4807b756a5d49c99b5191c`。这是本机修复候选，AMD/Intel目录仍引用本机已批准 beta，**不是新便携分发包**。Main/QML测试注入均已恢复；默认DLL按原件复制，新候选不含旧40运行库。GPL/本轮LGPL许可证及更新notice随候选保留。

已有设置若明确保存“允许撕裂”，会继续保留；验证本次全屏修复时在“显示 → 显示同步”选“自动”或“垂直同步”。无需开低排队、NR、SR或补帧。只能在已知纯播放路径上说软件策略修正，实际扫描撕裂仍需屏幕观察。

本轮中间候选与旧增量构建已清理，保留最终构建、候选、派生测试素材、独立测试配置、增量源码存档及全部必要日志；导出worker日志汇总在 `logs/.../final-export-worker-logs`，清理清单 `logs/.../cleanup.json`。未移除其他任务或历史发布产物。最终源码/运行库/原beta复核见 `logs/.../final-audit.json`，增量结案存档在 `archives/field-issues-20260930-final`。

## 2026-09-30 晚 Claude 复核修正（用户要求撤销“全屏垂直同步”）

- **E 撤销“自动 = 全屏垂直同步”。** 现在只有用户明确选“垂直同步”才 `sync=1`，只有明确选“允许撕裂”才传 `DXGI_PRESENT_ALLOW_TEARING`；默认“自动”在窗口和全屏都是 `Present(0, 0)`：不等垂直同步，也不请求撕裂。改动：`PresentationSettings.h`（`presentationVsync` / 新增 `presentationTearing`）、`PresentSink`（`Desc::tearing`、`configurePacing` 第三个参数）、`VideoPresenter` 状态文字、`ProPage.qml` 提示、合同测试、`field-issues-display.qml`。上文“全屏 VSync、窗口允许撕裂”“全屏 `sync=1 flags=0`”的描述作废。
- **C 收窄圆刚色调映射。** 只在 P010/P016 时请求关闭硬件色调映射；8 位格式不再强制打开，只读回并记录当前状态。原因：反馈日志里 GC573 的 4K RGB24 在别的程序关掉色调映射后才有 60fps，每次连接都强制打开可能把用户固定在 15fps。
- 构建 `build-claude-sync-r2.log` exit 0（422 项）；候选 `E:/项目/Veyra/tests/field-issues-20260930/candidate-claude-r1`，`veyra_qml_ui.exe` SHA256 `1c55d82918d6dd97ce9d9d6e06cf892c1b1b0cecc1946597fff517cc12bf5200`。`display-claude-r2` 六阶段通过：自动（窗口/全屏）`sync=0 flags=0x0`，允许撕裂 `0x200`，垂直同步 `sync=1`；`unit-claude-r1` 五项 exit 0（合同 243/0）。`tmp/field-issues-20260930/run_field_ui.py` 的期望值同步更新。
- 未验：物理屏幕是否还撕裂（本机无法观察）；GC573 实卡。`candidate-final` 保留未动。

## 2026-09-30 夜 Claude：时间线异常、崩溃诊断与全屏交互

- **时间线异常**（日志 `桌面/时间线异常.log`，1440p144 MJPEG 采集）：处理链每秒只取约 47 帧，压缩队列溢出丢掉的包被标成 `bad`，随帧带上 Discontinuity，于是每次丢帧都硬重置 NR/补帧历史（5 万次 reason=4）。MJPEG 是帧内编码，丢一张不影响其它帧：`CaptureCardSource.cpp` 里 MJPEG 队列丢弃不再标 `bad`，只计入 Drop（走已有的“有界跳帧保留历史”）。尝试过 FFmpeg 帧线程，MJPEG 解码器不支持、且瓶颈不在解码，已撤回。**未在真实采集卡上复验**（本机采集卡按约定不碰）。
- **2.0.0beta 闪退**（日志 `桌面/新版日杂hi.log`，RTX 5090 D，4K YUY2 采集）：10 次进程直接消失，全部发生在 XeSS 补帧开启期间，QML 程序没有崩溃处理所以无任何记录。本机 5070 用同一 beta 包和新候选各跑了一轮 XeSS 2×/4× + NR + 弹窗/全屏/面板压力，均未复现。已给 `veyra_qml_ui` 加上崩溃处理：记录异常码、出错模块与偏移，并在日志目录写 minidump。根因未定。
- **全屏任务栏**：进入/退出全屏时调用 `ITaskbarList2::MarkFullscreenWindow`。
- **界面**：采集“输入色彩/输入范围”改成 SDR/HDR 说明；双击画面在窗口下进入全屏；专业列表和节点页加“全屏”按钮；全屏控制条加“极简/专业”切换（不退出全屏）；全屏按 Home 在左侧弹出列表模式的参数面板和三个性能球（仅专业列表模式）。
- 构建 `build-claude-ui-r3.log` exit 0；候选 `tests/field-issues-20260930/candidate-claude-r2`，exe SHA256 `f11f0dff480a72c0de19dd275526959273cc40cd384dd83b5040d093aa6ae8fa`。`tests/claude-ui-20260930/ui-final` 真实界面流程通过（全屏、面板开关、条上切换模式、退出），`ui-shot`/`ui-shot2` 有截图；`display-claude-final`、`unit-claude-final`（合同 243/0）、`qml-claude-final` 通过。双击和 Home 键本身没有用模拟输入验证，逻辑经属性驱动验证。范围追加 `scope-amendment-claude-ui-capture-20260930.json`。

## 2026-09-30 采集内部延迟优化（Claude Code，用户要求“只能更低”）

- 口径：采集回调拿到帧 → Present 返回（`VEYRA_VERBOSE_FRAME_LOGS=1` 的 `capture-present-sample`），效果全关，KUHAIMI 27P NV12，窗口模式，RTX 5070。不含采集卡硬件/USB 和 Present 之后的系统合成、显示器。
- 结果（中位数 / P95，ms）：2K60 —— 1.4.4 1.95/2.59，r3 1.98/2.49，r4 0.90/1.09；4K30 —— 1.4.4 3.24/3.84，r3 3.30/3.99，r4 1.47/1.67。各两次，丢帧 0。
- 改动一：原生 NV12/P010/P016 采集的三个邮箱帧直接分配为 D3D12 upload buffer（`include/veyra/pipeline/CaptureUploadFrame.h`），回调里的拷贝成为唯一一次 CPU 拷贝，图直接从该 buffer 做 CopyTextureRegion；GPU 仍在读时回调改写 CPU 备用平面并走原路径；亮度分析的 64x36 采样改在回调里取。`VEYRA_TEST_CAPTURE_CPU_FRAMES=1` 回到旧分配。
- 改动二：实时采集的就绪等待增加 GPU fence 事件唤醒（`DeadlineWait::slice` 第二事件），不再只靠 0.2 ms 轮询。
- 试过未采用：多线程拷贝（本机内存带宽已饱和，无收益，已撤回）。
- 验证：两种格式截图画面正确；NR+补帧开启与 r3 行为一致（丢帧 256 对 245，属原有负载）；unit 合同 243/0；guard PASS（`scope-amendment-claude-latency-20260930.json`）。未验证：其它采集卡/格式（YUY2、RGB、MJPEG 不走新路径）、P010 HDR 实测、GPU 忙回退路径未被触发过、运动画面肉眼检查。
- 候选：`E:/项目/Veyra/tests/field-issues-20260930/candidate-claude-r4`，exe SHA256 `114d565e3ff9e51f4d6ecfb31f587ea0d7247cdaec811de1b88da270a9113f1f`；日志 `E:/项目/Veyra/tests/latency-20260930/`。未提交。

## 2026-09-30 效果链延迟排查（Claude Code，2K60 NV12 采集，后台离屏运行）

- 条件：用户同时在剪视频（GPU 有其它负载），测试窗口离屏、不抢焦点；口径同上（采集回调 → Present 返回）。
- r4/r5 中位数（ms）：全关 1.0；NR 8.4–8.8（GPU：光流 1.1 + NR 5.9 + 残差 0.1）；补帧 2X 真实帧 13.7–14.4、生成帧在 B 到达后 5.4–6.1；NR+补帧 真实帧 20.2–21.7、生成帧 11.9–13.4（相对 A 到达约 29–30）。1.4.4 同条件：NR 9.9，NR+补帧 22.3 / 14.0。
- GPU 时间线各阶段首尾相接（间隙 0.05–0.2 ms），补帧相位为“就绪 p95 + 0.5 ms”，正常负载下链路已贴近 GPU 耗时，没有可挤的大块。
- 发现并修复：NR/SR 耗时超过一个源帧间隔时（3 层 NR，GPU 约 20 ms/帧），无补帧路径会在 GPU 上叠第二帧，延迟变成两倍 GPU 时间（r4 53.4 ms）。`EngineController.cpp` 的 `enhancementPending` 现在对所有实时源在上一帧 GPU 未完成时不再提交，完成后取邮箱最新帧：r5 30.6 ms，帧间隔不变（21.8 对 22.5 ms）。GPU 跟得上时无影响。试过“提前 CPU 录制”的预测，未见收益，已撤回。
- 测试假象（未改代码）：早期一次 2.3 s 停顿来自测试进程的 stdout 管道（逐行 fflush），stdout 改为 NUL 后不再出现。
- 已知未处理：开启 NR 或补帧瞬间画面停约 1.7–2.1 s（特性创建，1.4.4 同样）；开启 NR 约 10 s 后有一次 20 ms 的 graph CPU 尖峰（1.4.4 同样，原因未查）。
- 候选：`candidate-claude-r5`，exe SHA256 `486be828e90bd37672bf18d4c0c8f71da488b2fdf19cb7b43488bfb2fa6554b0`；unit 合同 243/0。未提交。

## 2026-09-30 单次拷贝扩展到其余原生采集格式（Claude Code）

- `CaptureUploadFrame.h` 现支持：NV12/P010/P016（半平面）、YUY2/UYVY/YVYU/BGR24/RGB555/RGB565（单平面打包，图按原有 RGBA8 纹素上传）、I420/YV12（回调里用 SSE2 交织成 NV12，取代图里的 swscale）。64x36 分析采样改为保存原始像素字节。NV21、旧 CPU 回退布局、32 位 RGB、MJPEG 等压缩格式仍走原路径。
- KUHAIMI 27P、效果全关、离屏后台，r5 → r6 中位数（ms）：YUY2 1080p60 1.04 → 0.81；YUY2 1440p50 1.43 → 1.01；I420 1440p60 1.50 → 0.95；RGB24 1080p60 1.50 → 0.93；RGB24 1440p30 2.20 → 1.21；P010 1080p60 0.93 → 0.94（已是新路径）；NV12 1440p60 0.94 → 0.96（同）；MJPEG 1440p144 27.5 → 27.9（未改，瓶颈在解码）。
- 画面：每种格式 r5 与 r6 的截图 SHA256 完全一致（信号源为采集卡“无信号”静态画面）；I420 与 NV12 输出一致。未验证运动画面、UYVY/YVYU/RGB555/565（本卡不提供）。
- 候选 `candidate-claude-r6`，exe SHA256 `7b394ca93bbcfeaa2d23ccb4125ad65a8e4aeef4faaeb056e3c398fbc859e689`；unit 合同 243/0；未提交。测试时 16 张截图误落到用户图片目录 `Pictures/Veyra`，已移回 `tests/latency-20260930/shots-fmt`。

## 2026-09-30 MJPEG 采集解码延迟（Claude Code）

- 原因：单个软件解码器 1440p 约 110 fps，144 fps 输入时 3 帧队列常满，每帧排队约 18 ms；解码后还经 swscale 通用缩放器转 NV12，再由图拷贝上传。
- 改动：MJPEG 为帧内编码，`CaptureCardSource.cpp` 新增 `mjpegLoop`，2–4 个解码器并行处理连续帧（晚于已发布帧完成的直接丢弃并计数）；`CaptureCompressedDecoder.cpp` 对全范围 4:2:2 / 4:2:0 用 SSE2 直接写 NV12（4:2:2 垂直两行取平均），目标帧为 upload buffer 时即单次写入；解码线程发布画面时触发 `frameEvent`（原先只有回调入队时触发）。`VEYRA_TEST_MJPEG_SINGLE=1` 回到单线程。H.264/HEVC 等路径只加了发布事件和耗时日志。
- KUHAIMI 27P、效果全关、离屏后台，r6 → r7 中位数（ms）：1440p144 27.5 → 7.2（处理帧 1996/2741 → 2721/2731）；1080p240 16.3 → 4.2（3457/4558 → 4544/4554）；4K60 62.2 → 11.3（1015/1147 → 1133/1139）。剩余基本是单帧解码耗时（1080p 3.4 / 1440p 6.0 / 4K 10.3 ms，静态“无信号”画面，真实内容会更高）。reorderDrops=0。
- 画面：与 r6 截图不再逐像素相同（色度垂直滤波不同：最大差 23，均值 0.07，1.8% 像素差 >2，集中在彩色边缘）；放大对比新版彩边略少。未验证运动画面与 4:2:0 的 MJPEG 设备。
- 候选 `candidate-claude-r7`，exe SHA256 `26b9c7143095809968da425044c52abfa221642057fce74b56611eb53725d2f2`；unit 合同 243/0；未提交。
