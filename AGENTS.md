# Veyra 项目 Agent 执行规则

> **2026-10-02 导出优化目标授权（用户：“开启目标模式搞定他”）**：按 `docs/EXPORT_PAGE_OPTIMIZATION_PLAN_2026-10-02.md` 在 `E:/项目/Veyra/worktrees/export-page-20261002` / `codex/export-page-20261002` 实现导出页布局、可编辑顺序队列、MKV/多音轨/内嵌字幕、取消重试及完成提示音，并做真实验收。本条仅解除该方案列出的导出后端、QML/bridge、必要 CMake 与定向测试接点的历史冻结；其他链路不改。每轮先执行本分支 `python -B scripts/acceptance/export-page-control.py`，不可变基线在 `E:/项目/Veyra/archives/export-page-20261002-start/scope-baseline.json`，不得修改旧桌面 guard/baseline。Claude 的其他工作区、主分支、已有候选与用户配置保持原样；专用 E 盘构建/测试/TEMP，不派子 Agent，不 commit/tag/merge/push/Release。单测试≤300秒，构建≤900秒；已有 runtime/patched FFmpeg 身份和源码隔离约束保持。

> **2026-10-01 2.0.0 合并 main 授权（用户：“基本上2.0.0已经没啥问题了，剩下一些小bug和功能我后面再和你对，现在分支有点多……先合并一手，存档一下，更新更新文档，对齐目前进度”）**：整理 2.0.0 各分支并合并到**本地** `main`；先存档到 `E:/项目/Veyra/archives/merge-2.0.0-20261001/`（全引用 git bundle、合并前两处未提交状态、SHA256），打 `checkpoint/pre-merge-2.0.0-main-20261001`、`checkpoint/2.0.0-branch-tip-20261001`、`checkpoint/merged-2.0.0-20261001` 标签，合并后更新文档。授权范围：commit、打标签、本地 `--no-ff` 合并、删除已并入 `main` 且指向同一提交的重复分支名。**不含** push、GitHub Release、Runtime Pack 上传、删除旧 Win32 界面、删除 worktree 或用户文件、改动桌面原目录；这些仍需另行授权。此后 2.0.0 的新工作从 `main` 另开分支；本文件此前条目中的 `codex/*-2026092x/30` 阶段分支名均已并入 `main`，见 `docs/BRANCH_MAP_2026-10-01.md`。

> **2026-09-30 真人反馈修复授权**：先存档，再将 RTX40 NR 改为 SF-v2、RTX50 保留 Lecram，NR 节点提供双版本选择，淘汰新候选中的旧40运行库；随后按用户指定 `FIX_PLAN_1.4.4_FIELD_ISSUES_2026-09-30.md` 排查修复。仅在 E 盘 `p0-p1-r53-20260927`、新分支 `codex/field-issues-20260930`；保留当前 FSR/XeSS 和全部既有改动。必要的 NR 选择/配置迁移、GPU 故障、呈现、采集协商、HDR/比例/运动输入、分辨率/导出反馈修复接点获本轮授权，覆盖这些接点的历史冻结；无关改造仍冻结。范围、存档和验收见 `docs/FIELD_ISSUES_EXECUTION_2026-09-30.md`。原桌面 checkout、main、原 beta/历史发布/下载原件保留；不 commit/tag/merge/push/Release，不派子 Agent。新候选可原样使用已批准 Lecram/SF-v2，不 patch、不入源码 Git。

> **2026-09-30 本轮真人授权 XeSS / FSR 补帧修复（用户：“行，那就开始排查修复，以及增加FSR3 FSR4 写执行方案”）**：在 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` 的独立分支 `codex/fg-fsr-xess-20260930`，按 `docs/FG_FSR_XESS_EXECUTION_2026-09-30.md` 排查 XeSS 2×/4×、修复 FSR 热切换并增加 FSR 3.1 / FSR 4 入口。本条仅覆盖这些功能必要的 UI / bridge / settings / FG / graph / Present / 针对性测试及 CMake 接点的旧冻结；不改采集/解码/音频时钟、NR/SR 算法、旧 Win32、桌面原 checkout、原 beta 和 main。禁止用副显示器或第二隐藏视频窗口解决切换；FSR 4 RX 9000 实卡未验不得称通过。已有 169 个修改/未跟踪源码逐文件存档，范围基线 `E:/项目/Veyra/archives/fg-fsr-xess-20260930-start/scope-baseline.json`，SHA256 `c871f25fe503e30aeae3f63234c920a7574edf65da8d1643ec29e94b173d8a11`。本轮不 commit/tag/merge/push/Release，不派子 Agent；所有产物仍写 E 盘。

> **2026-09-30 补帧运行库升级 310.9.1（用户：“升级，然后文件我下好了……直接搞定掉”）**：`nvngx_dlssg.dll` 由 NVIDIA DLSS SDK 310.7.0 换成官方 310.9.1：SHA256 `FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82`，7,460,976 bytes，310.9.1.0，Authenticode Valid/NVIDIA；来源 `third_party_local/nvidia/DLSS_SDK_310.9.1`（用户自行下载）。310.7 运行库保留为回退，SR (`nvngx_dlss.dll`) 仍固定 310.7，不升级。40 系与 30 系的补帧解锁改为移植 SilyNoMeta/DLSSG-Transfusion（MIT）的 310.9.1 补丁：`src/ngx/DlssgTransfusion.cpp` 在 NGX 初始化前写入 provider（架构门槛 0x1b0→0x190/0x170、`GetGPUArchitecture`/`*_GetFeatureRequirements` 的 Ada 立即数、count/index validator、Blackwell 内核 fatbin 重建与容器就地重定向、DL1/DL2 网络内核替换、输出图像内核替换、valid-warp 质量策略），全部记录并逐字节回滚，NvAPI 只改 provider 自己的 `GetProcAddress` IAT 槽。优化内核以运行时文件随包提供（`runtime/experimental/dlssg-kernels`，29 个 .ptx），不进源码 Git；这两位作者都未授权这批内核，用户 2026-09-30 明确接受该风险并承担。本机 RTX 5070 已验收：补丁应用/回滚、harness 真值测试、产品 60fps 冒烟（2400+ 生成帧、failed=false）通过；**RTX 40/30 实卡未验收**。另有未决项：valid-warp 与优化内核同时生效时合成序列哈希与基线不同（单独任一项与基线逐位一致），需要判断是否为目标内的画质改动。发布、打包上传仍需另行授权。

> **2026-09-29 NR 默认运行库替换（用户：“直接接入，替换原有文件”）**：NR 默认位置（原“NVIDIA 原版”，`runtime/experimental/nvngx_dlssnr.dll`）改用社区 Lecram 构建 310.8.3.0：SHA256 `F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC`，165840496 bytes，Authenticode `HashMismatch`（NVIDIA 证书、内容已修改），来源 RankFTW/rhi-repo `dlssnr-310.8.Lecram`（zip SHA256 `ddb64e55…ca54`，用户自行下载）。依据：RTX 5070 原生 4K 实测 NR 输出与 NVIDIA 310.8.0.0 逐字节相同，耗时 -2.3%、功耗 -4.7W（`docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md` §8）。本机研发源为 gitignore 的 `third_party_local/nvidia/dlssnr-310.8.Lecram/`；`stage-runtime.ps1`、`package-portable.ps1` 和 `stage-ui-migration.ps1 -NrDefaultRuntime` 固定这一身份。NVIDIA 原版 E16BCF15 退为回退备份，社区版 984BEE0F 与 Ampere 版 DCC0DC24 不变。本条取代下文“二进制身份”与“当前 DLSSNR 许可范围”中对默认位置的 310.8.0.0/Valid 要求；1.4.4 发布包、原 checkout 不改；Runtime Pack 的发布/上传仍须另行授权。

> **2026-09-29 RTX30 NR 替换（用户：“30系列之前那个优化版本的NR……给30系列的替换掉”；同时决定 SR/FG 暂不升级 310.9.1，以免 30/40 补帧补丁失效）**：`nr-ampere` 位置由 NeuralScreen 1.8.2（DCC0DC24）改为 ShortFuse SF-v2：SHA256 `6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927`，165830144 bytes，版本字符串 310.8.SF.0（数字 310.8.2.0），`NotSigned`，来源 RankFTW/rhi-repo `dlssnr-310.8.SF-v2`（zip SHA256 `1da35941…955c`，用户自行下载）。SF-v2 除内核外还改写了模型权重区段（约 1.42 亿字节）；在 RTX 5070 上同帧 NR 输出与原版逐字节相同，RTX20/30 的 FP16 路线画面未测。它自带 RTX20/30 的 FP16 路线，因此兼容层对 310.8.2 及以上版本不再把 Ampere 改报为 Blackwell（`nrRuntimeNeedsAmpereRewrite`）；310.8.0/310.8.1 仍保持改写。本机只有 RTX 5070，**30 系实际效果未验证**，需持卡用户验收。DCC0DC24 保留在 1.4.4 包内作回退；`package-portable.ps1` 仅对 2.0.0 及以上版本改用 SF-v2。

> **2026-09-28 P4续推授权（用户：“p4估计快推进完了吧……把p4推完”，接续Codex会话01a0e687）**：Codex后半程把P4-b至P4-g全部按“UI-only冻结”记为partial并停止，但本分支P4授权本已写明“P4所需既有服务/bridge接点获本次功能范围授权”。本轮(Claude Code)按该授权补齐接点：P4-g接既有`ColorLutStore`/`ColorLookStore`；P4-c把既有`SubtitleLoader`/`SubtitleOverlay`搬入QML宿主（CMake仅给`veyra_qml_ui`加该源文件）；P4-d在`WasapiAudioSink`加指定渲染端点ID/回落默认/强制立体声（不改时钟、不丢PCM）；P4-b修`ExportJobManager`冲突项被递归跳过；P4-e/P4-f只接既有服务。扩展基线`E:/项目/Veyra/archives/p4-rest-20260928/scope-baseline.json`（P4-a文件哈希原样继承，仅追加allow）。后续追加`scope-baseline-b.json`（qml/Veyra全部组件）与`scope-baseline-c.json`（EngineController解码选项）、`scope-baseline-d.json`（qml/Veyra/backdrop-strong.png，修首页背景发白）、`scope-baseline-e.json`（2026-09-29 试用 18 条问题：保护区域椭圆的着色器/常量缓冲/分块导出与收尾诊断）、`scope-baseline-f.json`（2026-09-29 设计稿功能补齐：文件源“只用硬解”）、`scope-baseline-g.json`（2026-09-29 P5 逐屏差异修复：缩略图服务封面/队列缩略图、转圈/均衡条/图标按钮组件；用户要求删除全部内置预设）、`scope-baseline-h.json`（同轮：NR 层卡片改名后更新组件测试 tst_components.qml），同样继承哈希。硬件（PS5配对、采集卡实机、拔插）无法由Agent完成的项如实列未验。不commit/tag/merge/push/Release，不派子Agent。

> **2026-09-28 P4 授权（用户“开启目标模式干完P4”“别忘了存档”）**：在隔离工作区 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` 的 `codex/p4-functions-20260928` 分支，按 `docs/UI_V2_0_0_MASTER_PLAN_2026-09-27.md` 的 P4-a 至 P4-g 逐片实现并验证。P4 所需既有服务/bridge 接点获本次功能范围授权；不扩展到 P5 外观、P6 性能、无关采集/解码/音频时钟/算法重写。P4-b 仅列表模式/列表预设离线导出；节点模式与节点预设所有入口明确拒绝。原桌面 checkout、main、旧 Win32、已验候选、运行库和用户配置保持原样；不自动 commit/tag/merge/push/Release/删除旧 UI，不派子 Agent。每片存档并保留失败证据。开工归档 `E:/项目/Veyra/archives/p4-start-20260928/` 包含 104 个文件及哈希清单；独立 P4-a 不可变范围基线 `p4a-scope-baseline.json` SHA256 `5a4e63bdbdaa775db20912becb231a73324a644c109c38413e75967ce52501f7`。旧桌面 UI guard 开工 pass；后续独立 guard 与逐片记录见 `docs/P4_FUNCTIONS_EXECUTION_2026-09-28.md`。

> **2026-09-28 a32授权（用户：节点固定节点不能移动、节点参数必须在节点内调且与列表一致、贴近弹开动效、连线无效、拖到线上不能插入；“是p3就修掉”）**：按设计稿补齐P3节点编辑器：卡片内完整参数（与列表同源组件/接口）、输入/输出/光流/补帧卡可移动并持久化、settle防重叠弹簧、端口点击/拖拽连线（修a30引入的端口点击被画布空白点击清除的回归）、拖到连线插入与拖离断开。扩展基线`E:/项目/Veyra/archives/p3-nodeedit-20260928-a32/scope-baseline.json`（a31哈希继承，新增NrLayerEditor.qml）。不改引擎算法/CMake，不commit/push。

> **2026-09-28 a31授权（用户“把Alt那个问题也修了，同时补帧为什么没有开关？然后XESS补帧无效，DLss不是5x而是6x才对”）**：修无边框窗口单按Alt进入系统键盘菜单模式（`apps/veyra-qml/main.cpp`原生消息过滤SC_KEYMENU）、列表补帧开关、XeSS速率显示与4X上限、DLSS倍率列表按引擎合法集{2,3,4,6}与能力+1上限。扩展基线`E:/项目/Veyra/archives/p3-fgalt-20260928-a31/scope-baseline.json`（a30哈希原样继承，仅新增main.cpp）。不改补帧算法/SDK/Present，不commit/push。

> **2026-09-28 P4前修复批次授权（用户“可以写好直接开干”）**：修复用户手测a29-v2提出的9条问题（暂停无法继续、节点页胶囊/页面状态、极简预设按钮与跳转全屏、无边框窗口移动/缩放/三按钮、列表光流与HDR入口、确认框风格与过时文案、节点画布右键添加/中键平移/工具栏、列表耗时面板溢出、滚动条风格）。计划`docs/UI_FIX_BATCH_2026-09-28.md`；扩展基线`E:/项目/Veyra/archives/p3-fixbatch-20260928-a30/scope-baseline.json`，SHA256=`a1e5c69566875deb4cf167432e38f55ac905d89a891bca744769b76fb9ed3731`（a29哈希原样继承，仅新增QML/文档路径）。不进入P4新功能，不改引擎算法/shader/CMake，不commit/push。

> **2026-09-28 P3收尾授权（用户“全部同意，把p3给我干完”）**：针对Claude Code列出的三项P3剩余缺口全部同意：①逐节点GPU计时（解冻`include/veyra/diagnostics/FrameMetrics.h`阶段枚举并在`EnhanceGraph`逐NR层/逐Color实例打时间戳，bridge/NodePage显示）；②TrueHDR能力查询参数块所有权（`src/ngx/TrueHdrBackend.cpp`改为直接销毁并记录返回值，不经allocate跟踪表）；③原图像素、失焦、HDR/FG实卡生成帧补验。旧Win32 `apps/veyra/TelemetryWindow.cpp`名称表按枚举定长以免越界。新不可变扩展基线`E:/项目/Veyra/archives/p3-finish-20260928-a29/scope-baseline.json`，SHA256=`9eaa733b73432890bbe29a6b55c55fe9ff61474256536be82aa8497c35e18a09`（a8文件哈希原样继承，仅新增3条allow）。其余冻结（采集/解码/音频/Present、防闪、NR/颜色算法、shader、CMake）及不commit/push/Release不变。

> **2026-09-27 当前真人重新授权（“允许，继续吧”）**：针对上一条明确提出的“允许P3所需节点执行、独立配置、保存恢复后端接点；其他冻结范围不变，并恢复目标”，用户已明确同意。仅在`E:/项目/Veyra/worktrees/p0-p1-r53-20260927`、`codex/p3-node-backend-20260927`继续P3限定接点及直接验收，覆盖这些接点的UI-only限制；不改原checkout的冻结规则。现有32路径基线保持不改；不解冻采集/解码/音频/Present、防闪/1080p240、NR/颜色算法、CMake/旧Win32，不派子Agent、不commit/tag/merge/push/Release。按唯一P3剩余计划推进真实合法单链，不以拒绝合法顺序冒充完成。

> **2026-09-27 P3节点后端有限续接授权**：用户对“另开后端分支，仅补齐节点执行、独立配置及保存恢复”明确回复“可以，没问题”。在现有E盘隔离区切入`codex/p3-node-backend-20260927`，82份已有修改已逐文件保全。仅本次节点后端必要接点覆盖下方UI-only冻结；采集、解码、音频、Present、防闪、NR/颜色算法、CMake、旧Win32和发布仍冻结。先做状态隔离/持久化：22路径基线`E:/项目/Veyra/archives/p3-node-backend-20260927/state-scope-baseline.json`，SHA256=`d9943d4c28b305c80b7732558e50926a9c0a49e0a81c97c03df6eadb1e40fb9b`。执行器范围须写前明确最小依赖并另存不可变扩展基线；不得改旧基线洗白。沿用测试≤300秒、构建≤900秒、同因最多两次有据修复、20分钟无新证据停止该路径；不派子Agent、不commit/tag/merge/push/Release。见`docs/P3_NODE_BACKEND_EXECUTION_2026-09-27.md`。

> **P3续接范围澄清（2026-09-27）**：当前用户给出的UI-only冻结规则优先于下文历史P2解冻记录。P3仅在既定18路径白名单内修复QML/bridge接线与控件；不改engine/source/pipeline/shader/NR算法/export/color算法、CMake或旧Win32。已确认现有链转换不能保留任意节点拓扑，独立模式恢复接口也未接入；R5.4/R5.5执行器部分属于已登记的接口缺口，停止该路径，不扩baseline、不自动重试、不用节点预览冒充完成。后端须另行授权、另开分支和验收。P3文档不覆盖用户指定的UI_MIGRATION主方案/施工记录/恢复方案。

> **2026-09-27 P3 开工**：用户明确要求“开始干p3，开目标模式”，并指出调色未生效以及曲线、混色器、颜色分级只有滑条。当前先做 P3-a 调色接线与真实控件，只修改既有颜色合同的 QML/bridge 和直接测试/文档。独立基线 `E:/项目/Veyra/archives/p3-node-color-20260927/scope-baseline.json`，SHA256 `e78c75f6716a7fc59f8a7610b4e11468ec2820bff4dbf631e185f074514b5c9a`；18条白名单以该文件为准，不覆盖旧基线。后续节点执行器若出现接口缺口，先记录证据与范围，再按现有授权边界审查，不得以换界面为由顺手重构底层。防闪与1080p240继续暂缓；保全P2候选、原checkout/main/1.4.4/设计稿/运行库/用户配置。禁止子Agent、commit/tag/merge/push/Release与删除旧UI。每测试≤300秒、构建≤900秒，同因最多两次有依据修复，20分钟无新证据停止该路径。执行记录见 `docs/P3_NODE_COLOR_EXECUTION_2026-09-27.md`；整个P3未闭环不得标Goal完成。

> **2026-09-27 P2非防闪收口授权**：用户明确要求“开目标把P2剩下的推进完，防闪先放放”，包含前两轮指定的逐层NR编辑、全部参数可达及每层内部处理分辨率（默认1080p，1.4.4六档）。允许此P2切片必需的bridge、每层尺寸/实例描述及预设迁移最小修复；不进入P3节点执行，不改防闪shader/算法，也不以防闪未修阻塞本切片。工作区/分支不变。新独立基线 `E:/项目/Veyra/archives/p2-nr-ui-resolution-20260927/scope-baseline.json`，SHA256 `7a3f811468e76b545a6e88284f14953e4a7da75e71f757c3ebfe9e9d39e781ce`；旧基线、已验候选和用户手测程序保持不变。每测试≤300秒、构建≤900秒、同因最多两次有依据修复，禁止子Agent、commit/tag/merge/push/Release与无证据重复测试。

> **2026-09-27 P2 防闪修复解冻与迁移必要依赖授权**：用户明确回复“解除，为了新版的迁移，需要的合理部分都可以解除，不需要再问我”。本轮允许针对已复现的P2防闪网纹缺陷修改`shaders/NrTemporal.hlsl`及其直接回归测试；原定迁移中的必要依赖修复，有证据、有最小边界时不再逐文件询问。每次新增文件范围必须先记录依据，使用新的独立baseline，旧baseline/guard/已验候选永久保留；这不是解除所有技术保护、任意重构或洗白越界改动。当前目标仍仅P2，不自动推进P3–P8、R5.4/R5.5，不动已暂缓1080p240采集问题，不改原桌面checkout/main/1.4.4/设计稿/运行库/用户配置，不派子Agent、不commit/tag/merge/push/Release或删除旧UI。继续执行每测试≤300秒、构建≤900秒、同因一次诊断与至多两次有依据修复、20分钟无新证据停该路径。

> **2026-09-27 P2 接线追加确认**：用户在 NR 列表多层接线阻塞说明后明确回复“允许，这种原本设定中的都允许”。允许在现有 `codex/p2-nr-20260927` 隔离区补齐原定 P2 必需的 settings / EffectChain / GraphDescription / 多层参数应用及直接测试，必要时修正 QML 桥调用；此类计划内修复不再反复请示。旧 baseline 和已验部分存档不变，以新独立接线 baseline 记录新增具体白名单，授权文件更新后重新冻结。仍不授权 P3–P8、R5.4/R5.5、采集/解码/音频/补帧改造、merge/push/Release 或删除旧 UI。

> **2026-09-27 P2 追加授权（仅隔离分支）**：用户要求“现在继续开启目标模式，做 P2”，并暂缓此前讨论的 1080p240 采集问题（未证明原因是电脑性能）。复用 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927`，转入 `codex/p2-nr-20260927`；仅执行总方案 P2 的 NR 多层/单层旧链兼容复验及证据驱动最小修复。下方 P0/P1 限制和 UI-only 冻结只对本条明确范围让位；其他冻结仍有效，不授权 P3–P8、R5.4/R5.5、采集/解码/音频/补帧改造、旧 UI 改造或删除、merge/push/Release。先核验 P1 最终源码快照，P2 使用独立不可变 baseline；不得改旧 guard/baseline 洗白。执行范围、命令限时、失败上限和验收缺口见 `docs/P2_NR_EXECUTION_2026-09-27.md`。禁止子 Agent、修改原 checkout/已验候选/运行库/用户配置/账户或模型。

> **2026-09-27 P0/P1 追加授权（仅隔离分支）**：用户明确要求“先开目标模式把p0和p1解决掉”。只在 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` / `codex/p0-p1-r53-20260927` 执行 `docs/UI_V2_0_0_MASTER_PLAN_2026-09-27.md` 的 P0/P1；本条仅为 R5.3 调色独立 pass 的证据驱动修复、保护和验收解除下条相关冻结，不授权 P2–P8、R5.4/R5.5 或重写播放链路。原 checkout、main、1.4.4 包、原型和运行库保持原样。每轮先运行本分支独立 P0/P1 scope guard；旧 UI guard 和旧基线不能修改或用来证明本分支保护。执行与停止合同见 `docs/P0_P1_R53_EXECUTION_2026-09-27.md`。禁止子 Agent、merge/push/Release、删除旧 UI、调整账户或模型路由。

> **2026-09-27 用户纠偏（覆盖 2026-09-25 的“QML 新界面 + 引擎改造”授权）：当前 2.0.0 只做 QML UI migration。** 现有分支里已经混入的 engine/source/pipeline/shader/NR/export/color 改动保持原样并冻结，不回滚、不继续修改、不计入 UI 完成度。禁止继续修改 `src/engine`、`src/pipeline`、`src/source`、`src/sink`、`src/media`、`src/gfx`、`src/ngx`、`shaders`、`CMakeLists.txt`、旧 Win32 `AppShell.cpp`，也禁止推进采集、解码、音频、导出、补帧、NR、R5.3/R5.4/R5.5。QML 只能消费已有接口；接口缺口必须记录并停止，不能借 UI 迁移之名改链路。每轮无人值守先运行 `scripts/acceptance/ui-migration-scope-guard.ps1`；guard 失败必须停工。活动方案以 `docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md`、`docs/UI_MIGRATION_EXECUTION_2026-09-25.md` 和 `docs/UI_MIGRATION_RECOVERY_PLAN_2026-09-26.md` 为准。后端能力另行授权、另开分支、另做验收；旧 Win32 删除、merge、push、Release 仍需用户明确授权。

> 2026-09-25 用户授权以目标模式实施 QML 新界面迁移与引擎改造：单分支 `codex/ui-qml-migration-20260925`（起点 `df41580`，标签 `checkpoint/pre-ui-qml-migration-20260925`），按 `docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md` 施工，进度记在 `docs/UI_MIGRATION_EXECUTION_2026-09-25.md`。允许引入 Qt 6（装在 `E:\项目\Veyra\deps\qt`，不进源码 Git）；新界面完成后删除旧 Win32 界面；按 Magpie（`SAOG0721/Magpie` `3841698`，GPLv3）移植 NR 多实例结构并逐项标注来源。补帧锁定在链路最后；只显示提交帧率；低延迟不得退化；每步打存档标签。合并 main、推送、发布仍需用户另行授权。

> 2026-09-19 用户授权修复全屏提示后构建并发布 1.4.2 至 Likely7/Veyra-NRVideo，整合本轮隔离修复到 main，更新双语 README 与 Release，重点说明 RTX Video HDR 和帧同步。授权公开本次新交流群二维码与 RTX Video HDR 对比图；赞助图保留，双二维码各 width=220。允许正式包携带已核验 TrueHDR 原件：RTX Video SDK 1.1.0，nvngx_truehdr.dll，3955752 bytes，1.1.0.0，Valid/NVIDIA，SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B，独立 manifest 与 NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf。其余运行组件身份沿用；NVIDIA FSR4 实验保持撤回。执行记录见 docs/RELEASE_1.4.2_EXECUTION.md。本条取代此前仅本地测试包的发布范围限制。

> 2026-09-18 用户授权目标模式实施可关闭帧同步、多模式与实际延迟验收。开工存档 `6e69eeb` / `checkpoint/pre-frame-pacing-20260918`，隔离分支 `codex/frame-pacing-20260918`。按 `docs/FRAME_PACING_EXECUTION_PLAN_2026-09-18.md` 施工，默认关闭、直接 NGX、保留 6X，不推送或发布。报告必须区分进程内排队、系统显示事件与未测量的屏幕端到端延迟。

> 本轮交付状态见 `docs/FRAME_PACING_ACCEPTANCE_2026-09-18.md`：本机软件短测完成，原生 Reflex 仅无补帧路径；FG 下显示回退低排队，XeSS 不叠加外部节奏。不得将软件驻留减少冒充屏幕延迟降低，不得把本机 RTX5070 验收扩展为 30/40、实卡采集、PS5 或 VRR 通过。完整 Reflex+FG 与显示延迟验收仍未完成；现有 beta 包不含本轮改动。

> 2026-09-18 当前用户授权制作 1.4.2beta 内测群便携包，包含 FSR4 回退及保留的 RTX Video HDR。允许携带已核验的 RTX Video SDK 1.1.0 TrueHDR 原件（SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B，3955752 bytes，1.1.0.0，Valid），其余组件沿用 1.4.1 身份，逐文件 manifest 及许可证随包提供。仅本地交付用户自行发群；无 GitHub 推送、Release 或 Agent 代发消息授权。

> 2026-09-18 最新用户决定：停止并回退 NVIDIA FSR 4.1 实验，撤销其 UI、provider 接入及测试/打包入口；不继续画质修复。保留 RTX 超分、原有官方 FSR 和独立 RTX Video HDR 改动。本条覆盖下方 FSR4 本地试验授权；历史研究仅供追溯，不是待办。旧实验预设的 mode 6 读取时迁移到 RTX Video SR 高档，其他设置保留。

> 2026-09-18 用户追加授权：FSR 4.1 增加可保存的界面选择，随本地测试包携带独立 INT8 provider，方便直接试用。该本地实验入口取代上一阶段仅环境变量入口的限制；不代表画质验收或公开发布授权。

> 2026-09-18 当前授权：先更新项目文档，在 `codex/video-hdr-20260918` 隔离实施 RTX Video HDR，再验证 NVIDIA FSR 4.1.1 INT8 研究 provider。此决定扩展 09-16 的 NVIDIA FSR4 观察范围，允许本地构建/接入/测试，不代表正式支持或发布授权。存档 `checkpoint/pre-video-hdr-fsr41-20260918`；当前事实见 `docs/CURRENT_STATUS.md` 与 `docs/VIDEO_HDR_FSR41_EXECUTION_PLAN_2026-09-18.md`。旧计划中“尚未发布/尚未实现”的历史语句不覆盖 1.4.1 发布记录。

## 本机产物目录规则（2026-09-18 用户决定）

- 今后 Agent 创建的 Veyra 构建、测试、下载、打包、解压验证、诊断和临时文件，统一写入 **`E:\项目\Veyra\`**，不得再散落桌面、Downloads、C 盘根目录或系统 Temp，也不再在源码仓库内新建这些产物。源码、受版本控制的脚本和文档仍留在源码仓库；运行库、SDK 和模型仍禁止进入源码 Git。
- 按用途使用子目录：`build/<任务>`、`tests/<任务>`、`logs/<任务>`、`tmp/<任务>`、`downloads/`、`deps/`、`test-packages/<版本>`、`releases/<版本>`、`verify/<版本>`、`archives/<任务>`、`worktrees/<任务>`。任务名包含日期或明确标识；每轮在 WORKLOG 记录实际输出路径。
- 执行旧脚本前检查输出参数和硬编码路径，显式传入新目录；不支持时先做最小路径参数化并验证，再执行。测试/构建子进程需要临时目录时，仅对该进程及其子进程设置 `TEMP`/`TMP` 到上述 `tmp/<任务>`，不得修改全局环境。不得仅修改文档却继续默认向旧位置生成文件。
- 现存的 `C:\veyra-deps`、`C:\veyra-releases\1.4.1`、源码内现有 `runtime_local` / `third_party_local` / 当前构建及验收记录暂时保留，不能为整理目录直接移动或删除。需要迁移时先修正引用、重新配置并验证；后续新建或重新生成的产物遵守新目录规则。历史文档中的旧路径只是历史证据，不作为新任务默认输出路径。
- 此规则针对研发产物，不改变产品保存用户配置、PSN 凭据或用户主动截图/导出的路径，也不要求修改 Windows WER 等系统服务的全局设置。系统自动产生且无法通过子进程配置重定向的残留，应在任务结束时识别、记录并按当前清理授权处理。
- 每轮结束整理本轮中间包和解压副本，保留当前可用构建、最终交付包、对应源码及必要验收证据；不要复制整工程作日常备份，不删除用户文件或其他任务仍使用的产物。若 E 盘不可用或工具不兼容路径，明确报告并处理原因，不擅自退回桌面或 C 盘堆积。

> 2026-09-18 用户决定：今后每个 GitHub Release 的正文必须保留交流群和微信赞助二维码，不能只放 README。固定区块见 `docs/RELEASE_SUPPORT.md`；两图并排、各 `width="220"`，保持用户认可的现有尺寸，未经要求不删、不换、不放大。发布及编辑后核对远端正文中的两个图片地址、尺寸与实际可访问性。用户授权公开的实测截图可作为 Release 图片资产展示，按截图事实标注显卡、设置和软件读数，不将显示提交 FPS 冒充物理屏幕刷新率。

> 2026-09-16 用户决定：**有成熟开源实现就直接搬过来改造，不要重复造轮子**。XeSS MFG 解锁（OptiScaler 的 XeFGUnlock/XeFGPacing，经 Magpie fork 适配）、40 系 DLSS MFG 解锁（RTX40MFG-Unlock、MFGAdaUnlock-RenoDx，MIT）、30 系原生 2X（dlssg_for_sm86）、AMD FSR 帧生成（FidelityFX SDK 的 Frame Interpolation / AMD FSR Frame Generation）等能力，一律**优先移植现成开源实现并接入 Veyra**，不做等价重写、不另起炉灶。搬运要求：逐项记录来源仓库、固定提交、许可证、被改动的文件与改动说明，写入 `THIRD_PARTY_NOTICES.md` 与对应源码；Veyra 自身为 GPLv3，与本批 MIT / GPL-3.0 来源兼容，禁止只改名不标注。对 NVIDIA / Intel 运行库只允许**进程内修改**，不改磁盘文件、不重签名、不伪装身份。移植后仍按本次改动范围做真实验证，未验证项如实报告，不得用上游项目名替代本机证据。计划见 `docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md`。

> 2026-09-16 用户追加决定：FSR 超分按显卡分档——**AMD 卡开放 FSR 4.1（ML）；N 卡只提供 FSR 2 / FSR 3.1，不开放 FSR 4.1**（N 卡跑 FSR4 目前无可用实现，仅列入观察名单，成熟后按"有开源就直接搬"处理）。AMD 补帧**必做且优先用新版**：先接 AMD FSR SDK 2.3.0 的 `AMD FSR Frame Generation 4.0.1`（ML），不支持的显卡回退本地 SDK 1.1.4 的 3.1.x。40 系 DLSS MFG 解锁已获条件授权：**只要确认不影响 50 系即可施工**（50 系走原生路径、不安装任何补丁）。

> 2026-09-16 用户授权合并到 main：隔离分支 `codex/framegen-fsr-dolby-20260916`（tip `ad7442d`）已合并（merge `24e7de7`）。存档 tag：合并前 `checkpoint/pre-merge-framegen-20260916`（main `87ad4cd`）、分支 tip `checkpoint/framegen-fsr-dolby-branch-tip-20260916`、合并后 `checkpoint/merged-framegen-20260916`；分支保留不删。冲突统一取分支原版实现，main 侧 RGB24 移植 `87ad4cd` 被 `015f8a4` 取代（其说明文档保留）。**未推送、未发布；push/Release 仍需当前对话明确授权。** N1–N4 本体未开工；40/30 系、真实位流卡与 AMD/Intel 导出实机验收仍待用户。

> 2026-09-16 用户授权开启采集链路优化（新隔离分支）：从 main `2406f81` 开 `codex/capture-decode-latency-20260916`（tag `checkpoint/pre-capture-decode-latency-20260916`），按 `docs/CAPTURE_DECODE_LATENCY_PLAN_2026-09-16.md` 施工：压缩解码架构（压缩直连 → D3D12VA 硬解 → MJPEG 并行软解）与原生 N1–N4（N4 缓冲协商 → N3 格式排序 → N1 逐像素转 GPU → N2 拷贝合并）。全部在隔离区，人工验收合格前不得合并 main；N1 必须保留 RGB24 方向契约（`015f8a4`）。真机验收用本机 USB3 卡 + 受影响用户；未生效的优化不得计入收益。

> 2026-09-14 用户授权实施 HDR 全增强与文件/采集 5.1，PS5 串流真实多声道不在本次范围。按 docs/HDR_ALL_EFFECTS_MULTICHANNEL_RESEARCH_PLAN_2026-09-14.md 施工，经过显式颜色合同扩展旧 SDR 边界；HDR 基底保留合成不宣称 NR 模型原生 HDR 推理。默认效果全关、共享处理图、直接 NGX、现有运行组件身份及 patched FFmpeg 保持。未授权新发布。

> 2026-09-14 用户授权发布1.1.1到Likely7/Veyra-NRVideo，包含RTX30 NR实验选项和首次效果全关。发布范围新增用户已指定的NeuralScreen1.8.2 NR原件：DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F / 165840496 bytes / 310.8.0.0 / HashMismatch，独立放在Release的runtime/experimental/nr-ampere/，逐文件manifest记录；原七组件与patched FFmpeg沿用。此授权扩展上次仅本地范围，不允许修改DLL或进入源码Git。RTX30实卡尚未验收，不能宣称全型号成功。更新双语README、完整便携包和对应源码；本轮未请求关机。

> 2026-09-14 用户授权实施 RTX30 NR 兼容选项与首次启动全部效果关闭。允许将用户提供的 NeuralScreen 1.8.2 包内 native/nvngx_dlssnr.dll 原样用于隔离本地接入测试：SHA256 DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F，165840496 bytes，310.8.0.0，HashMismatch。独立 runtime_local/nvidia/nr-ampere/，不覆盖原版/RTX40社区版；调用方兼容限定所选NR模块，不修改磁盘驱动或运行库。不授予新Release或将此文件入源码权限。初始NR/SR/FG全部关闭，已有明确保存的设置保留；RTX30实际性能需持卡验收，不能用本机RTX50通过代替。


> 2026-09-14 用户授权发布1.1.0至Likely7/Veyra-NRVideo：包含普通版Smooth Motion开启说明、允许自主叠加的策略及此前采集格式扩展；更新双语README与教程、完整便携包及对应源码。沿用已批准七个增强运行文件与patched FFmpeg，不新增驱动文件或SDK入Git。Smooth Motion由NVIDIA App管理，用户已反馈本机有效稳定，不宣称驱动算法内置、所有设备验收或叠加必然更好。发布完成并核实资产后按用户要求关机。

> 2026-09-14 用户确认 Smooth Motion 在本机实验版有效且稳定，决定普通版仅提供 NVIDIA App 开启方法与注意事项：只用驱动补帧时选择软件“关闭补帧”；换回内部 DLSS/XeSS 时由用户在 NVIDIA App 关闭 AI 插帧。允许用户自行叠加驱动与内部补帧，不检测、不拦截、不强制互斥，也不自动修改驱动配置。叠加效果未验证，不宣称更好；软件统计/截图/导出不冒充含驱动生成帧。本条取代此前 Smooth Motion 实验方案的强制互斥与软件内驱动配置管理要求；保留原实验分支作历史存档。本轮未授权新发布。

> 2026-09-13 用户授权修复持续播放后重连额度不恢复并发布1.0.2。连续30秒有解码进展、帧间无1秒断档后恢复本轮3次重试；会话累计计数/源epoch不回退，短暂出帧不续次数。包含此前手动占用重试、首帧等待和日志保留修复。沿用七个运行组件及patched FFmpeg；不把软件恢复改善宣传成Wi-Fi断流已根治。

> 2026-09-13 用户授权发布1.0.1：采集音频10ms缓冲协商与输入诊断修复合入主线、更新双语说明和完整便携包。沿用1.0.0七文件运行组件，FFmpeg slice补丁和对应源码要求不变。用户日志约半秒音频落后与旧缓冲缺口高度一致；修复后的反馈者实卡结果尚未取得，不宣称所有设备延迟已消除。

> 2026-09-13 用户授权整合已完成修复分支、替换透明 Logo，并向 Likely7/Veyra-NRVideo 发布正式应用版本 1.0.0。沿用七个已批准增强运行文件；保留 GPU DIS 为可选实验功能，NVOF 默认。必须使用带 PS5 H.264 slice 补丁的 FFmpeg，附对应源码、构建记录、补丁与开源许可证。正式应用版本号不改变 NR、DLSS FG、XeSS、GPU DIS、PSN/HDR 等功能的实验边界。SDK、DLL、模型、凭据、配置及测试媒体仍不得新增进入源码 Git。

> 2026-09-13 PS5 H.264 硬解细条修复：本机 FFmpeg n9.0.1 增加 `scripts/ffmpeg/ps5-h264-slices.patch`（32→256 slices），同一真实 PS5 AU 软/硬解已全图比较一致；另修正硬解 SRV 双帧覆盖。后续构建/发布不得无意退回未打补丁的 FFmpeg。对应源码应使用实际 patched tree，并携带 `veyra-local-build.json` 及补丁/重编译说明；原 vcpkg SPDX 仅代表底包。详见 `docs/PS5_HARDWARE_STRIP_REPAIR_2026-09-13.md`。本次只做本地修复，未授权新发布；NVIDIA 运行组件不变。

> 2026-09-12 用户授权合并 PS5 开发版并发布 0.0.5 至 Likely7/Veyra-NRVideo，更新双语 README 和完整便携包。沿用七个已批准增强运行文件，串流开源依赖附对应源码、固定版本、补丁及许可证。SDK、凭据、个人配置、测试媒体不进入源码或便携包。实验功能边界如实说明。

> 2026-09-12 用户授权实施 PS5 HDR、PSN 登录与主机保留方案（docs/PS5_HDR_PSN_HOST_PLAN_2026-09-12.md）。仅对本次经过显式HDR输入/输出契约的PS5路径扩展旧SDR限制；文件/采集/导出不得全局放开HDR防错检查。原生HDR旁路显示与“HDR转SDR后增强”必须区分，不能将实验NR的SDR结果冒充原生HDR增强。PSN凭据及主机密钥只在用户数据目录DPAPI加密保存，不提交Git。当前状态按对应施工记录，不由本授权推断测试通过。

> 2026-09-12 用户授权默认关闭的“低延迟模式”：实时预览可选 NR→SR→FG，取代本文件对该选项的固定SR先行限制。NR在源尺寸（或明确的实时内部尺寸）执行，残差/保护合成后再进入SR；共享现有图、光流和生命周期，不另造播放循环。默认仍SR→NR→FG，图片/视频导出保持原有完整处理。不得保证所有素材延迟下降；提示可能增加拖影/边缘瑕疵，真实屏幕撕裂不能简单归因于算法顺序。见 docs/NR_BEFORE_SR_PREVIEW_2026-09-12.md。

> 2026-09-11 用户授权执行实时调度修复（方案见 `docs/REALTIME_AV_SCHEDULING_REPAIR_PLAN_2026-09-11.md`，开工前存档点 `checkpoint/av-scheduling-2026-09-11`，隔离分支施工）：实时文件/采集**预览**在持续欠速时允许按真实 PTS 跳过已解码源帧的增强/呈现机会（先省补帧工作，再省原帧增强），稳态音频不停机、媒体时间保持一倍速、跳帧均匀覆盖时间线；源 PTS/序号/duration 保持真实，历史按断点 reset。该例外只限实时预览：视频导出、图片处理、暂停单帧查看仍必须完整处理，不得把丢帧扩大到导出，也不得把预览跳帧伪装成未丢帧。不得以停音频/丢 PCM/偷偷放慢声音制造同步。

> 2026-09-11 用户授权发布 0.0.4：当前音画同步与音频连续性修复推送到 `Likely7/Veyra-NRVideo`，更新中英文 README 并发布完整便携包。运行组件沿用 0.0.3 的七文件白名单及双 NR 原件身份，不新增或替换运行时；社区版继续记录 `HashMismatch`。源码 Git 禁止 SDK/DLL/模型，软件允许用户替换 DLL 的决定不变。

> 2026-09-11 用户授权发布当前0.0.3：源码推送到`Likely7/Veyra-NRVideo`，完整便携Release包含当前双NR运行版本和直播实验选项。此前用户指定的RTX40/50社区DLL可原样进入本次Release的`runtime/experimental/nr-community/`，身份沿用下条SHA256、310.8.0.0、165840496字节、`HashMismatch`，必须逐文件列入manifest并明确社区修改版；这是本次发布范围的扩展，不允许自动加入其他版本或修改文件。源码Git仍绝不包含SDK/DLL/模型。软件不恢复运行时哈希锁。OBS捕获说明按用户要求仅补README，不继续改软件提示。

> 2026-09-11 用户指定RTX40/50社区NR运行时接入：允许把用户明确提供的`E:/Ai/mg/DLSSNR-DLL-Options-310.8.0.0/Community-RTX40-RTX50/nvngx_dlssnr.dll`原样复制到忽略的`runtime_local/nvidia/nr-community/`进行本机切换测试。该文件SHA256为`984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`，签名状态`HashMismatch`，必须标注社区修改/实验，不能称为有效NVIDIA签名原版。用户指定此文件取代旧“仅根目录原版可本机加载”的限制；不授予Agent修改/重签名文件的权限，不自动扩大Release默认白名单，不上传DLL到源码Git。

> 2026-09-11 用户发布与替换决定：0.0.2发布到`Likely7/Veyra-NRVideo`，源码与Release二进制继续分离。用户明确要求去掉运行时校验、允许自行替换DLL：不得用固定哈希、签名或manifest拒绝用户替换的运行时；保留绝对路径加载、API存在性及初始化结果检查，替换不保证ABI/硬件兼容。下文相冲突的启动校验/禁止用户替换要求被本条取代。发布者制作的默认Runtime Pack仍按固定来源、哈希、签名与许可证审计；组件清单供用户查看，不作为软件加载锁。禁止将SDK/运行时/模型提交源码Git的规则不变。

> 2026-09-10 用户废弃旧 Loop：`loop/`、`scripts/loop-gate.ps1`、`CONTROL_HASHES.json` 及早期 Phase 排队/控制哈希停工规则全部退出当前执行流程，仅保留作历史记录。不得因旧 Loop 状态、STOP、控制哈希或旧文档顺序阻塞用户明确要求的修复，也不得更新旧清单来制造通过。当前任务以最新用户指令、对应修复计划和 `docs/WORKLOG.md` 为准。构建、针对性回归及必要的 `scripts/gates/delivery.ps1` 直接执行；单次测试最多 300 秒，并非全部测试累计 300 秒。运行时身份、源码/二进制隔离、真实验证与发布授权规则继续生效。

> 2026-09-07 新用户决定：默认明确标注的实时档（4K输入可用1080内部处理），保留原生4K可选；视频导出仍原生4K。不得要求以本机原生4K NR达到60fps作为本次默认档门槛，也不得把实时档冒充native4K。最终统一软件短测为 `scripts/gates/delivery.ps1`（phase5–7本次合同的合并检查），实卡仍由用户验收。其他安全/许可规则不变。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。
> 2026-09-09 用户发布决策：Veyra 采用“完整开箱即用的实验 Runtime Pack”发布策略。用户明确接受该 Pack 可能被 GitHub 下架或被权利方要求移除的风险，并授权将经过身份校验的指定实验运行时作为 **GitHub Release 资产** 随 Veyra 用户包分发。该例外不允许把 NVIDIA SDK、运行时、模型、头文件、样例或压缩包提交进源码仓库、Git LFS、Git 历史或 C++ 源文件；不允许修改、重签名、伪装或从游戏/驱动缓存提取二进制。详情以“Release Runtime Pack 规则”为准。

本文件对在本目录工作的所有 Agent 生效。不要只扫标题；开工前必须完整阅读：

1. `README.md`——当前状态和唯一入口；
2. 当前任务对应的 `docs/*PLAN*.md`——当前施工方案；旧 `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` 仅作技术契约与历史参考；
3. `VEYRA_PRODUCT_SPEC_V1.md`——产品与技术边界；
4. `docs/COMPETITOR_AUDIT_2026-09-03.md`——竞品事实、许可证和可迁移启发；
5. 当前阶段的 `docs/WORKLOG.md`——已完成、失败记录和下一步。

旧 Loop 不再要求阅读、同步或运行。最新用户指令优先于历史方案；仍适用的技术、运行时身份和许可证约束以本文件为准。

## 不可擅自改变的决定

- V1 是 Windows x64、C++20、Win32、D3D12 项目。
- V1 直接调用 NGX；不要同时接入 Streamline。Streamline 仅作未来替换方案和文档参考。
- 固定帧 harness 分两步：Phase 1 直接生成 RGBA8 Proxy → Feature 18 → Raw 抓帧；Phase 2 才加入 Original → Parity Encode → Feature 18 → Parity Decode。不是先做完整播放器。
- `nvngx_dlssnr.dll` 是 DLSSNR 运行时；只从项目根目录的已知文件复制到 `runtime_local`，不得联网寻找“更新偷跑版”、不得修改、重签名或提交 Git。
- NVIDIA 已正式发布 DLSS 5 消费者功能，但公开开发包尚未提供可直接替换当前路径的通用接口。用户于 2026-09-06 决定继续用固定身份的实验 Feature 18 做本机研发；这不证明画质与官方游戏/Magpie 等价，也不授予分发权。不得以“官方已上线”为由从游戏目录或驱动缓存抽取新 DLL。
- `renodx-dlss5-1.addon64` 是未签名的 ReShade/RenoDX 二进制 add-on，不是配置文件。它只准用于隔离的参考/对照环境，最终程序不得加载、注入、链接或随包分发它。
- 最终主线不依赖 ReShade。所谓“类似 ReShade”指独立复现它的前后颜色传递、参数映射和输出处理。
- V1 必须同时完成采集卡实时增强、DLSS 5 播放器、图片/视频增强导出。删掉其中任何一条都属于擅自改产品，不得用“以后做”放行。
- 三个入口共享 `FrameSource -> EnhanceGraph -> FrameSink`；禁止复制三套 NGX、颜色、Guidance 或 reset 逻辑。
- V1 是首发产品，不是最小 MVP。必须保证 4K SDR 全链路：Windows DirectShow/UVC 1080p/2160p 30/60、最高 3840×2160 H.264/HEVC 播放、D3D12 NVENC H.264/HEVC 视频导出、PNG/JPEG 图片导出、DLSSG 2X。HDR、3X/4X、厂商私有采集 SDK、AV1/ProRes 与 VFR 原样输出仍不是本次承诺，不能把它们混同为“4K”。
- V1 的质量主线是 NVOF motion + 置信度门控 + 可选 Depth Anything V2 Small；Zero Guidance 只允许作为诊断/回退，不得作为“高质量完成”。
- 采集卡/普通视频不含游戏引擎原生 depth、motion、exposure、camera matrices 或 HUD-less color。任何 Agent 都不得宣称估算输入“等同原生”。
- DLSS SR（若启用）在 Feature 18 前；DLSSG 在 Feature 18 后；播放器 OSD/字幕在 DLSSG 后合成，避免 UI 被插帧扭曲。
- 采集卡的固有延迟不等于可用 lookahead。若要生成 A 与 B 之间的帧，必须等 B 真正到达；额外用 C 做一致性验证还要再等一个源帧。不得把采集卡内部/驱动缓冲宣称为“免费未来帧”。
- 已接受 Magpie Experimental 对“可调用”的可行性证明，不再重复市场可行性争论。但 Create/Evaluate、真实输出、颜色、guidance、时序、延迟、稳定性及相对画质仍必须工程验证。

## 二进制身份，任何不一致都立即停工

```text
nvngx_dlssnr.dll（默认位置，2026-09-29 起）
  SHA256: F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC
  Size:   165840496 bytes
  Version: 310.8.3.0 (community Lecram)
  Signature: HashMismatch (NVIDIA certificate over modified content)

nvngx_dlssnr.dll（NVIDIA 原版，回退备份）
  SHA256: E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
  Size:   165840496 bytes
  Version: 310.8.0.0
  Signature: Valid, NVIDIA

renodx-dlss5-1.addon64
  SHA256: 837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F
  Size:   359424 bytes
  Signature: NotSigned
```

如果 hash 不同，不要“先试试看”；记录实际值并询问用户。
## Release Runtime Pack 规则（2026-09-09 用户授权）

这是一项面向用户体验的、明确标记为实验性的发布决策，不是 NVIDIA 官方合作、官方支持或通用再分发授权。

- **源码与 Release 必须物理分离。** 所有 NVIDIA SDK/runtime、模型、头文件、`.lib`、样例、SDK ZIP、驱动文件和本机抓帧必须保持在 gitignore 的本地目录；不得提交、vendoring、Git LFS、嵌入资源、编码为 base64 或以任何方式混入源码/提交历史。
- **仅 Release 资产可带二进制。** 用户批准的完整包可从本机受控 staging 目录复制已批准运行时；每个文件必须在 `release-runtime-manifest.json` 中列明文件名、来源类别、版本、大小、SHA-256、签名状态、实验标记及是否可卸载。未列明的 NVIDIA 文件一律拒绝打包。
- **当前 DLSSNR 许可范围。** 仅允许身份与本规则上方完全一致的 `nvngx_dlssnr.dll`（310.8.0.0、指定 SHA-256、NVIDIA 签名有效）进入 `runtime/experimental/`。哈希、版本、签名或来源不一致时停止打包并询问用户；不得联网搜寻“更新偷跑版”。
- **不得篡改或隐藏。** 禁止 patch、重签名、修改 PE、伪装为 Veyra 文件、嵌进资源、从游戏安装目录/驱动缓存复制，或借助 RenoDX/ReShade 注入路径实现发布包。实验 Runtime Pack 必须以清晰目录、版本与哈希呈现。
- **运行时保护。** Veyra 启动时校验 manifest、SHA-256、签名和 GPU/驱动兼容性；校验或初始化失败时只禁用对应增强功能，基础播放器、采集和导出仍可启动。不得静默下载未知二进制，也不得把失败伪装为增强已生效。
- **发布说明。** Release Notes、README、软件内组件页必须写明“实验运行时 / community experimental”，不得宣称 NVIDIA 官方合作、认证、支持或完整官方 DLSS 5 集成；同时保留 Veyra 自身版本、runtime 版本、哈希和卸载说明。
- **范围最小化。** 用户包只包含运行所需 DLL/模型与适用许可证、notice、manifest；绝不包含 SDK 开发文件、样例、私有测试媒体、日志、PDB、LIB 或游戏文件。官方 SDK 中明确可集成分发的组件仍须逐项保留其许可证与来源记录。
- **人工发布。** 任何 push、GitHub Release、Runtime Pack 上传、替换或删除都必须由用户在当前对话明确授权；无人值守 Goal 仍禁止发布。若平台或权利方要求移除，先移除/下架 Runtime Pack，再保留不含该 Pack 的 Veyra 基础包。

## 当前修复验收

当前修复按用户请求和当前方案推进，不再按旧 Phase 0→7 排队。每次交付必须同时满足：

1. 代码可构建；
2. 本次改动相关的自动或手工检查通过，未执行项明确报告；
3. `docs/WORKLOG.md` 写明命令、结果、日志路径、失败与修复；
4. 没有把 proprietary runtime 或本地 SDK 提交进版本控制；
5. 后续改动没有建立在未验证假设上。

历史 Phase/checkpoint 只证明当时的测试结果，不代表当前产品已经验收。当前状态按本次构建、回归、实卡和发布证据分别报告；不得拿早期通过或失败标签替代当前事实。

## 历史 Loop

旧循环状态与 Phase 记录只代表当时证据，不是当前任务队列或通过证明。保留历史文件供追溯，不再据其自动推进、阻塞或发布。工作记录统一更新到 `docs/WORKLOG.md` 和本次修复文档；未经实际独立复核不得声称有 Reviewer 验收。

## 实现纪律

- 先检查现状：`git status`、`rg --files`、hash、工具版本。不要覆盖用户已有改动。
- 每次只实现当前阶段最小闭环，不顺手重构整个工程。
- `tools/*_probe` 只能组装并验证产品库。禁止把核心实现永久堆在超大 `main.cpp` 中；当前约 3600 行的 `player_probe` 必须迁移到共享 engine，不能复制到 UI/Capture/Export。
- 任何 NGX/NVOF 返回值、HRESULT、SEH、资源尺寸/格式和 GPU timestamp 都必须进入日志。
- 所有历史型模块在 open/seek/resize/pause-resume/scene-cut/device-lost 时显式 reset。
- 正常播放、采集和最终视频导出路径禁止 GPU→CPU 像素回读、每 pass CPU fence wait、无界帧队列和多份隐式颜色转换。视频导出必须用 Video Codec SDK 的 D3D12 NVENC input/fence；raw pipe 只准作诊断 fallback，不能通过首发 gate。
- 采集 ingress 使用 latest-frame mailbox（容量 1）并丢弃过期帧；图内部另有严格上限的 A/B/C history window。低延迟 FG 需要 A/B 的一帧 lookahead window；这不是可伪造为固定 `1/f` 的实测显示延迟。高质量实时模式最多再保留 C 做验证。实时文件/采集**预览**在持续欠速时可按真实 PTS 跳过已解码源帧的增强/呈现机会（2026-09-11 修订，仅限预览；跳帧须均匀覆盖时间线且计入诊断）；视频导出与图片/暂停单帧仍不得丢源帧。
- 所有来源先显式解析 range/matrix/transfer，进入统一 linear working texture；所有 sink 只做一次明确的输出转换。
- Guidance motion 固定为 current→previous、单位为 post-SR `workingExtent` 像素；depth 为 R32F 相对深度；confidence 为 R8_UNORM。低置信度区域必须衰减/清零 motion，不能把坏向量硬塞给 NGX。
- seek、scene cut、PTS discontinuity、capture drop、resize、source switch、pause/resume 和 device lost 必须原子 reset SR/NR/FG/depth/flow 的全部历史。
- 创建 Feature 可以等待一次；逐帧执行 1080p 用 3–4 个、4K 用 4–6 个 command slots/fence values 轮转，不能每帧或每 pass `WaitForSingleObject`。
- NGX 参数名和参数类型必须逐项照 Playbook；不要凭名字猜 `int`/`uint32_t`/`float`/resource。
- 所有 DLL 用绝对路径、`LoadLibraryExW` 和受限 search flags 加载；禁止依赖当前工作目录搜索。
- 不允许把 `renodx-dlss5-1.addon64` 改名为 DLL，不允许尝试从中 `GetProcAddress` 当普通库调用。
- 不允许在磁盘上 patch `nvngx_dlssnr.dll`。调用方兼容层必须独立封装、运行时可关闭，并明确标记 local experimental only。
- C++ 层保持 RAII；逆序释放 Feature、parameter block、runtime、NGX Core、D3D12 resources。
- Shader 先独立测试。颜色结果异常时先查 transfer、range、resource format、subrect、state barrier，别先调“画质参数”。

## 许可证与分发红线

- Magpie（含 `SAOG0721/Magpie` experimental fork）与 OptiScaler 均为 GPLv3，Veyra 自身也是 GPLv3，**可以直接移植其源码**；义务是逐项标注来源、固定提交、许可证与改动，并随源码/对应源码包提供。禁止只做机械改名而不标注来源。第三方 MIT 项目（RTX40MFG-Unlock、MFGAdaUnlock-RenoDx、dlssg_for_sm86 等）同样逐项标注。
- `video2dlssnr` 当前仓库未提供许可证；不得复制代码。`DLSS5-Feeder`、`dlss5-infinity-studio` 和 `dlss5-visual-enhancer` 只能按各自许可证与第三方 notices 取用；默认只借鉴公开行为与架构，任何代码复用都要在 `THIRD_PARTY_NOTICES` 逐项归因。
- NVIDIA SDK/runtime 的分发权不得想当然地扩大。除“Release Runtime Pack 规则”列出的、用户明确批准并完成身份校验的 Release 资产外，默认只做本机研发；`runtime_local/`、`third_party_local/`、抓帧和 SDK 压缩包必须 gitignore。
- NVIDIA Video Codec SDK 头文件/sample 同样需要单独接受 EULA；系统 `nvEncodeAPI64.dll` 不复制进用户包。没有完成第三方分发审计前，不得把产品宣传为 NVIDIA 官方认证、合作或支持。
- 已批准的 Runtime Pack 例外只适用于 GitHub Release 用户包，不适用于源码仓库、Git 历史、CI artifact、第三方镜像或私下发送未经 manifest 校验的二进制。
- 任何 Runtime Pack 的新增文件、版本替换、push、GitHub Release、上传或删除都必须先在当前对话得到用户明确授权，并报告来源、SHA-256、签名状态、许可证文件和包内扫描结果。

## 每次交付必须报告

- 当前修复任务与完成门槛；
- 修改的文件；
- 实际运行过的构建/测试命令；
- Create/Evaluate 或失败码的真实日志，不得用“应该可以”代替；
- 已知风险和下一条唯一任务；
- 若没实际在 RTX/NVIDIA runtime 上执行，明确写“未执行”，绝不能声称成功。
- 长任务必须在当前修复文档保留可续接的状态、证据与下一步；不再同步旧 Loop 状态。

## 禁止用假完成糊弄

以下均不算完成：只写接口桩、只增加成功 counter 而实际工作仍由 harness 执行、只编译未运行、用 manifest 代替 provider、重复运行 1080p 冒充 native 4K、仅显示理论 FPS、把 Raw NR 直接展示却声称完成 parity、以 Zero Motion 冒充 NVOF、以重复/线性混合帧冒充 DLSSG、仅枚举采集卡却没持续显示、仅导出图片却声称视频导出、丢音轨/时间戳不报告、或捕获到黑图仍把返回码 0 当成功。
