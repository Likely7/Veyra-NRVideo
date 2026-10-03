# 当前项目状态 / Current Status

## 2026-10-04 VFG / AMD NR 瘦身候选

`codex/runtime-size-20261004` 以黑边已验修复fc2ca716为基线，先存档/写方案。
VFG移除九个当前路径未用NPP，目录497,894,432→210,788,400 bytes；全部档位和插件原字节保持。
720p/4K各463 native checks、331 settings、21 NVENC导出、组合/缺库/GUI/worker冻结通过。
AMD生产噪声/reciprocal表、权重与62 HIP kernel保持；仅ABI/布局/哈希验证，不称实卡推理通过。
最终完整7z523327604 bytes /499.08MiB，备用ZIP943265095 /899.57MiB；比原1098.25MiB ZIP减54.56%。
2076载荷+manifest独立解压SHA、官方7-Zip CRC、包内VFG热切换/预设/High8冻结worker和GPU/软件六页通过。
极简新EXE6个DPR用例通过；本机5070/616.56短测边界保持，原桌面/main/2077旧载荷复核一致。
源码/完整bundle及HDR PR计划、最终索引在E:/项目/Veyra/test-packages/runtime-size-20261004；代码存档2df6dd7。
临时/解压副本清理遭自动审批拒绝（无具体原因），未重试/绕过，E盘副本保留。HDR仍等审批，未merge/push/Release。
完成存档/索引后按用户明确要求安排正常关机，使用延迟helper后shutdown /s /t0，操作结果另记E盘日志。

## 2026-10-04 极简像素缺口已修；HDR PR 等审批

独立分支 `codex/minimal-edge-hdr-review-20261004` 从已验VFG候选存档开工。
修复分数DPI时native child/region少一列与默认极简窗口contain取整黑线，完整输入映射到整数client，保留真实比例/原始像素/全屏黑边。
完整/增量构建通过；30个尺寸/DPR/渲染模式真实GUI用例、12个行为回归、2个确定性跨页回归通过。
RTX5070/616.56，Qt scale factor模拟，未宣称实显示器DPI移动或新HDR/FG画质通过。
只制作基于`2.0.2-vfg-20261003`的EXE小补丁，记录见 `docs/MINIMAL_EDGE_ACCEPTANCE_2026-10-04.md`。

PR #13/#14固定head静态审查完成：P5 offset/拟合/flat映射、#14缺失头文件/半套P5、HDR统计range/array/延迟/reset等问题和当前VFG持久化格式分叉均已记录。
`docs/HDR_DOVI_PR_REVIEW_2026-10-04.md` 提供分拆适配、验收及“输入/处理/输出”界面编排；**HDR产品代码未施工，等用户通过后再修复并入**。
桌面patch/11份未跟踪源码、main、原2077个便携载荷复核一致；未merge/push/Release。

## 2026-10-04 VFG 全档位：独立本地交付

`codex/vfg-integration-20261003` 已接原生 NVIDIA VFG 2X–8X、Low/Medium/High，
共享图、实际预览/NVENC导出、列表/节点、预设/会话和worker冻结均接入。720p/4K
8/10-bit全部21组合、21种真实导出、2K30AVI+最高SR4K+原生NR+VFG8、24/60fps、
取消/缺失SDK、GUI热切换/重启和旧格式回归通过；缺失组件UI仍显示8X的错误已修复。
RTX5070/616.56短测，不代表40系/616.92/HDR色度/物理显示/长稳通过。高档8X实时会降档。
最终本地便携包完成并通过独立解压实跑：2077载荷哈希、包内VFG全GUI/High8 worker、
GPU与软件各六页检查通过。交付在 `E:/项目/Veyra/test-packages/vfg-integration-20261003`，
公开2.0.2保持原状；详情 `VFG_INTEGRATION_ACCEPTANCE_2026-10-03.md`。

## 2026-10-03 五项问题修复：独立本地候选

分支 codex/field-upgrade-20261003，已先存档写方案。修复 Xbox 音频输出线程未
启动，加入按原因的有界重连；修复码率提交/回跳与范围；修复软件 UI 背景透明；
删除 NVIDIA App 模块存在性误判。AMD RX9000 NR 的独立 MIT runtime 接入共享
图，用户 0.39 模型/HIP 完整资产已补齐。真实 RTX5070 NVENC 导出、RTSS 下软件
背景、39 项 QML、本地 Xbox/音频、AMD C ABI 测试通过。真实 Xbox 有声/长稳、
RX9000 推理未验；AMD 当前限 1080p 像素预算，HDR/原生 4K NR 导出未支持。
详见 FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md。未合并 main、推送或发布，
下面 2.0.2 已发布记录仍是公开版本事实。

## 2026-10-03 2.0.2 已发布（最新）

[正式 Release](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.2)，tag/source `0dafc57`，已合并并推送 nrvideo/main，latest/公开/一个便携 ZIP。三版 NR、Claude RTSS 兼容、Codex Xbox/VRR/倍速/启动恢复全部包含；英文首页、中文切换、双语 Release 与支持二维码已远端核对。最终包 1520 文件审计、干净解压实跑和远端 SHA256 一致。完整身份与验证在 WORKLOG 最新节；以下准备/未发布状态均为历史。用户现场显存根因与 Xbox/VRR 实卡边界没有冒称通过。

## 2026-10-03 2.0.2 集成与发布

用户最新明确授权恢复 NVIDIA 原版 NR，与 Lecram、SF-v2 共三版，并合并 Claude `28a440b` 和 Codex `bfafbea` 后发布 2.0.2。两边已合入隔离发布分支，新增原版用 ID 3，旧 ID/默认值保留。已通过编译、11 组回归、真实列表/节点 NR 切换与重启保存、倍速与启动恢复。英文 README 为首页，中文见 README_CN.md；Release 英文在前中文在后，保留 2.0.0 主要更新与双 220px 二维码，资产为单个便携 ZIP。发布和最终包证据见 WORKLOG 与 RELEASE_2.0.2_PLAN_2026-10-03.md；以下“未授权/README 不动/候选不发布”等是此前历史，不覆盖此次授权。

RTSS 冲突按本机复现修复；Xbox 协议/关闭竞态及 VRR 输入时序软件回归通过，但真实 Xbox 长稳、主机 VRR 实卡与 5060 Ti 显存增长根因仍未确认。全屏退窗保护可选且默认关闭。

## 2026-10-03 独立候选：Xbox/采集 VRR、启动恢复与倍速

当前 `codex/player-startup-speed-20261003` 已实现：默认关闭的全屏显存保护开关，启动自动继续视频进度/采集配置，双页面 1/1.5/2/3× 保调倍速；修复 Xbox 关闭时发送异常、回调寿命、空字段并补帧反馈；修复 VRR 可变采集间隔被固定帧率条件反复重置以及补帧相位使用标称间隔。软件回归及实际播放/普通采集测试通过，详情见 `PLAYER_STARTUP_SPEED_PLAN_2026-10-03.md` 和 WORKLOG。Xbox 反馈者连接稳定性、VRR 主机实卡结果、5060 Ti 显存根因仍未确认。D3D12 UI 的 0x887A002B 在新旧候选都可复现，软件 UI 对照通过；不与 Claude 的 RTSS 分支重叠改动。未合并 main、未推送、未发布 2.0.1；下方历史保护“自动退出”现在仅显式启用时执行。


## 2026-10-02 2.0.1 接管候选（尚未发布）

Claude 的 `8f42fd5` 已在 nrvideo/main，2.0.1 tag/Release 未创建。当前修复在 `codex/field-2.0.1-20261002` 的 E 盘隔离 worktree，README 不动。Xbox 空值/等待/错误码/ICE 解析已修，80 项协议/本地 WebRTC 验证通过；显存做持续全屏增长退出全屏的应急保护，补帧开/关故障注入均通过，**5060 Ti 4K 全屏泄漏根因与真实现场结果未确认，不能称为彻底修好**。本地 5070 加载相同插件时仍稳定，不能只凭插件加载归因。用户授权修好后发布；先保持未发布，候选待反馈者验证。完整接管记录 `FIELD_2.0.1_REPAIR_PLAN_2026-10-02.md`，历史状态不覆盖本节。

## 2026-10-02 2.0.0 发布更新

用户已确认 OBS 兼容并授权发布 GitHub 2.0.0、更新中英文 README 和新版操作教程。当前发布内容以 README、RELEASE_NOTES_2.0.0.md、BUILD_2.0.0.md 及 RELEASE_2.0.0_PUBLIC_2026-10-02.md 为准。以下未发布/未授权等旧阶段表述保留作历史，不覆盖本次授权；硬件未验边界仍保留。


## 2.0.0 总览（2026-10-01，合并 `main` 时对齐）

**代码位置与状态**
- 2.0.0（`CMakeLists.txt` `project VERSION 2.0.0`；QML 新界面 `veyra_qml_ui.exe`）已合并进**本地** `main`，在此之前 `main` 停在 1.4.4 发布后的 `df41580`。**未 push、未发布**：`nrvideo/main` 与 GitHub 仍是 1.4.4。push / Release / 删除旧 Win32 界面 / Runtime Pack 上传都还需要单独授权。
- 2.0.0beta 测试包（`docs/TEST_PACKAGE_2.0.0beta.md`）已交内测用户试用，不是正式发布候选。
- 分支整理、存档位置、标签、桌面原目录与 worktree 的处理见 `docs/BRANCH_MAP_2026-10-01.md`。存档 `E:/项目/Veyra/archives/merge-2.0.0-20261001/`。

**总方案进度**（`docs/UI_V2_0_0_MASTER_PLAN_2026-09-27.md` §4）：P0–P5 完成；P6 同一候选的全产品回归、P7 独立 2.0.0 候选包、P8 旧 UI 淘汰/推送/发布**未做**。

**2026-09-30 至 10-01 真人反馈修复**（Codex 实施，Claude Code 复核修正后继续；详见 `docs/FIELD_ISSUES_EXECUTION_2026-09-30.md`，方案与证据在 `E:/项目/Veyra/reports/field-issues-20260930/`）
- **NR 运行库**：新配置 RTX20/30/40 默认 SF-v2、RTX50 默认 Lecram，NR 列表/节点可选两个版本，全链原子切换。40 系卡死与社区 NR 版本相关，只加了诊断，**没有自动恢复**。
- **显示同步**：「自动」= 窗口/全屏都不等垂直同步、也不带撕裂标志（Codex 原来在全屏启用垂直同步，已撤销）；「垂直同步」「允许撕裂」为显式选项。物理屏幕是否还撕裂**尚未由用户确认**。
- **采集**：AVerMedia 色调映射只在 P010/P016 时请求关闭、其它格式只读记录日志（Codex 原来对 8 位格式强制打开）；MJPEG 队列溢出丢帧不再标成不连续（原先每次丢帧重置整个时序历史）；采集色彩选项改成「HDR · PQ / HDR · HLG / SDR · 709」「有限 · Limited / 完全 · Full」并附说明。
- **2.0 界面**：QML 程序加崩溃转储（`veyra-crash-*.dmp`）；全屏时任务栏隐藏；双击画面进出全屏；列表/节点页有全屏按钮（专业模式在画面右下角）；全屏内鼠标移到最上沿唤出顶部胶囊切换极简/专业且保持全屏；Home 键在全屏列表模式弹出左侧快速调节面板（同列表设置页，附性能悬浮球）。
- **采集到显示的内部延迟**（采集回调 → Present 返回，效果全关，KUHAIMI 27P，RTX 5070，窗口模式）：
  - 2K60 NV12：1.4.4 约 1.95 ms → 0.90 ms；4K30 NV12：约 3.2 ms → 1.47 ms。
  - 其它原生格式（YUY2 / I420 / RGB24 / P010 / P016 / UYVY / YVYU / RGB555 / RGB565）统一走「采集回调直接写入显卡上传区」的单次拷贝：YUY2 1080p60 1.04 → 0.81，I420 1440p60 1.50 → 0.95，RGB24 1440p30 2.20 → 1.21 ms。
  - MJPEG：多解码器并行 + 直接写 NV12：1440p144 27.5 → 7.2 ms，1080p240 16.3 → 4.2 ms，4K60 62.2 → 11.3 ms，且不再丢帧。
  - 开效果：NR 单开约 8.4–8.8 ms（1.4.4 9.9），补帧 2X 真实帧约 14 ms，NR+补帧约 20–22 ms（1.4.4 22.3）；GPU 各阶段首尾相接，正常负载下没有可挤的空隙。显卡跟不上（3 层 NR）时原来会多叠一帧排队，已修（53.4 → 30.6 ms）。
  - 全部逐格式画面对比与前一版一致（MJPEG 因色度滤波不同有 1.8% 边缘像素差异）。

**已知未验证 / 未处理**
- 2.0.0beta 在 RTX 5090 D 上 XeSS 补帧时的进程闪退：在 5070 上复现不出，已加崩溃转储，**等用户复测提供日志和 `.dmp`**。
- 5090 补帧「一半无效」：已复现（内容节奏模式下，30fps 内容在 60Hz 采集里出现像素相同的重复帧被丢弃）；改法（跳过重复源帧）**未获授权，未做**；默认「采用源时间戳」不受影响。
- 8K 视频导出红屏未复现；GC573 红屏、Elgato 4K X 只做了保守处理，未验实卡。
- RTX 30 / 40 实卡（NR SF-v2、补帧 310.9.1 + Transfusion）、FSR 4 在 RX 9000 上的实际生成，**均未验**。
- 全屏双击、Home 快速调节、顶部胶囊悬停只经状态/信号驱动测试，没用真实鼠标键盘验过。
- 延迟优化只在 KUHAIMI 27P、静态画面上测过：运动画面、其它采集卡、输出 4:2:0 的 MJPEG 未验。开启 NR/补帧的瞬间画面停约 1.7–2.1 秒（特性创建，1.4.4 同样）未处理；开 NR 约 10 秒后一次 20 ms 处理尖峰原因未查。

> **2026-09-30 反馈修复形成独立本机候选**：修复前存档完成；RTX20/30/40新QML配置默认SF-v2，50默认Lecram，NR列表/节点双版本全链切换、失败恢复及保存重开通过。所有效果关闭时全屏同步不再被低排队开关强制关闭，Automatic全屏实际Present(1,0)；HDR10/scRGB、显示比例、独立motion和5/6/7K已接入并完成针对性软件回归。GTA4K→8K原生NR与无NR各120帧导出/解码正常，本机未复现红屏。40系卡死/TDR自动恢复、GC573/4KX实卡、5090/HDR屏/物理撕裂及G真值画质AB仍未结案，不能称A–K全修好。分支codex/field-issues-20260930，HEAD/main不变，无push/Release，原beta保留。[本轮执行记录与未完成清单](FIELD_ISSUES_EXECUTION_2026-09-30.md)。本机候选E:/项目/Veyra/tests/field-issues-20260930/candidate-final/veyra_qml_ui.exe，非新便携包。


> **2026-09-30 本轮 XeSS / FSR 补帧修复已形成独立本地候选：** 分支 `codex/fg-fsr-xess-20260930`，HEAD 仍为 `53a2d31`。FSR 3.1 改为独立生成纹理，热切换不再依赖 retained swapchain；列表/节点新增独立 FSR 3.1 / FSR 4 ML 入口、2× 上限、真实 provider 显示及预设/session 往返。修复非法 FSR 4× 预设保存和 FSR 4 被拒绝后 UI 未回滚的问题。RTX 5070 与本机 AMD 核显的真实 GPU 生成和同 HWND 连续切换通过；XeSS 2/3/4× 在本机可运行。最终 QML 4K30 产品切换、暂停/seek/resize/resume、预设重开通过。
> **未完成的边界：** FSR 4 ML 只完成接入与不支持负例，RX 9000 实际生成未验；AMD 反馈者的具体 XeSS 2× 限制仍缺型号/日志，不能称已根治。HDR 实屏/物理延迟/长时稳定及 P6 同候选全产品回归、P7 正式候选与升级回退未完成。FSR 离线补帧导出未新增。PS5 “卡死”本次由用户确认是未连上，后已连接，不列入这轮 FG 故障。
> 候选 `E:/项目/Veyra/tests/fg-fsr-xess-20260930/candidate/veyra_qml_ui.exe`，SHA256 `25439d59af47813b7fadad7b4e3b3c839acc7531414a8613d9d86f6730b7d8a8`；这是本地测试 staging，依赖本机运行库路径。原 `2.0.0beta` 保留，未 commit/tag/merge/push/Release。[执行方案与实际证据](FG_FSR_XESS_EXECUTION_2026-09-30.md)。下方为此前各候选的历史结果。

> **2026-09-30 补帧运行库升 310.9.1（DLSSG-Transfusion 移植）：** `nvngx_dlssg.dll` 换成官方 310.9.1（`FF6E90EB…`，Valid/NVIDIA），
> SR 仍为 310.7；40/30 系补帧解锁改由移植的 Transfusion 补丁在 NGX 初始化前写入 provider（记录式写入 + 逐字节回滚，
> NvAPI 仅改 provider 自己的 IAT 槽），优化内核以 29 个 `.ptx` 运行时文件随包提供（不进源码 Git）。本机 5070 已验收：
> 补丁应用/回滚、harness 真值、产品 60fps 冒烟（2400+ 生成帧、failed=false）通过；310.7 与 310.9.1 未打补丁时合成序列哈希一致。
> **未验证**：RTX 40/30 实卡；另有未决项——质量策略与优化内核同时生效时哈希与基线不同（单独任一项一致）。见 `docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md` §2026-09-30。

> **2026-09-29 P5 逐屏差异修复：** 用户圈定的差异项已全部实现并在同一候选实测（清单、方案、结果见 `docs/UI_P5_DESIGN_DIFF_2026-09-29.md`）。候选 `E:/项目/Veyra/tests/p5-fix-20260929/candidate-final`（EXE SHA256 `a553e995ecdbfbb07391747db6f03c9d7ad559f951f70a222b99f774b737b93d`）。内置预设已从新界面删除。未 commit。

> **2026-09-29 设计稿功能补齐：** 用户圈定的 A/B/C/D/E/F/G 项已全部实现并在同一候选实测（清单与结果见 `docs/UI_DESIGN_GAP_2026-09-29.md`）。候选 `E:/项目/Veyra/tests/design-gap-20260929/candidate-final2`（EXE SHA256 `65f86c3a9a3fa199f6731df0e095ebdacc2fd6540af85c3776da8766be651e95`）。

> **2026-09-29 试用 18 条问题：** 17 条已修并在同一候选实测（见 `docs/UI_TRIAL_FIXES_2026-09-29.md` 末节与 WORKLOG 同日条），#18 PS5 卡死已加收尾诊断日志、需用户在场联调。候选 `E:/项目/Veyra/tests/trial-fixes-20260929/candidate-final`（EXE SHA256 `56b22f997a10efc461d38b4e511e81a2431f84a5970780b0a4386b2ad0e08287`）。本轮未重跑 P4-d 真机音频端点切换与 P4-e 采集卡连接（避免干扰用户正在使用的采集）。

> **2026-09-28 P4（Claude Code续推）：** P4-b至P4-g接点补齐并在同一候选实测：LUT/独立颜色预设、字幕（轨道/外挂/偏移/样式/真实叠层像素）、音频输出设备选择/回落/下混、导出队列冲突修复+图片导出+MF、设置页全部项与可重绑快捷键、采集卡/屏幕捕获/PS5完整连接（此前对话框只切页面）。候选`E:/项目/Veyra/tests/p4-rest-20260928/candidate-p4`（EXE SHA256 `9eac448ade392142c1dd13cb51524a29b9f13580fd902b968356454e79c45776`）。未验/未做：任意导出宽高（需新管线步骤）、PS5实机串流与手柄、多声道设备下混差异、设备拔插事件。
> **同日试用修复：** 首页背景发白（P4-f背景强度改动引入）已修；另修两处既有缺陷（极简页离开后下一页被压成画面高度、无副标题对话框关闭按钮错位）。修后回归全通过，EXE不变，增补存档`archives/p4-rest-20260928/p4-final-r2`。

> **2026-09-28 a32：** 节点编辑器按设计稿补齐：卡内完整参数（与列表同源）、固定节点可移动并保存、防重叠弹簧、端口点击/拖拽连线（修复a30引入的连线回归）、拖到连线插入/拖远断开。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a32`（EXE SHA256 `b8b356a3403dc3cff7dc253cc82a58260c6153b3c2a0b41b2fbfbd4e7885e34b`）。

> **2026-09-28 a31：** 补帧开关、DLSS倍率列表{2,3,4,6}（本机6X实测）、XeSS显示SDK提交率并支持4X、DLSS高倍率切XeSS自动夹倍率、单按Alt系统菜单拦截已完成。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a31-v2`（EXE SHA256 `91451380d4891040ebfe6e71ed733e0e57bc4dcd004bafa4f86f7ce41aa627ff`）。Alt卡顿原现象未能在应用内复现，修复为防护。

> **2026-09-28 a30：P4前修复批次完成。** 用户手测提出的9条（暂停无法继续、节点页胶囊状态、极简预设跳转全屏、窗口移动/缩放/三按钮、列表光流与HDR、确认框风格、节点画布右键添加/中键平移/工具栏、耗时面板溢出、滚动条风格）全部修复并在真实app验证。最终候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a30-v2`（EXE SHA256 `163eb9ea9feed1172b9daf3e8f39c5cea120880ef6fbdc759a93da0c20f82ae6`）。未commit/push，P4待用户开工。

> **2026-09-28 a29：P3完成。** 逐节点GPU计时（NR每层/Color每实例，节点卡显示P95或原因）、TrueHDR能力参数块所有权修复、原图像素/真实失焦/HDR+DLSS FG实卡生成帧补验全部通过。最终候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a29-v2`（EXE SHA256 `cf761b566542f6c5ca5ac6660f56404b20f4eb2170debbf62d6d97259028a01e`）。本机SDR显示器，HDR实时上屏未验；其他显卡未验。未commit/push，下一阶段P4待用户开工。

> **2026-09-28 a28 131–143（Claude Code接续）：** Codex因429额度耗尽停在136收取前；136列表保护实际已通过。新增138列表保护SR前/后×1/4层与负对照通过，S1关闭。最终候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a28-p3-final`（EXE SHA256 `8d4660075191c71277551768649fd6e4a8deb5523b6e33cde5c3c5f6ae256d39`，QML已显式同步源码，旧stage会带旧QML）回归通过。P3剩余仅冻结接口类缺口（逐节点GPU计时、TrueHDR warning）及原图像素/失焦/HDR-FG实卡未验，需用户裁定；不标完成、未关机。

> **2026-09-28 a28 128–130：** 已修复NR滑条点击穿透重叠Color卡、意外打开参数页的问题；组件21 passed及原重叠布局真实app三滑条回归通过。未改引擎，未永久替换旧候选；画面/运行重启及冻结诊断边界仍待收口，P3不冒称完成。

> **2026-09-28 a28 122–127：** 绑定修复存档已落盘；NR紧凑卡3滑条在不重叠布局下验证通过。原夹具覆盖Color后出现一次点击同时改NR并进入Color页，已定位而未掩盖；正在限定修复共享滑条命中，不改引擎。P3仍未完成。

> **2026-09-28 a28 117–121：** 已修复NR/调色参数滑条操作后断开绑定、组/节点还原时显示陈旧的问题。模型35而滑条75的红测已转绿；组件20 passed（含生命周期2项），真实调色页38及NR完整参数页9个滑条通过鼠标改值/复位/实例隔离。只改5个允许QML及1个QML测试；引擎、算法、旧Win32、构建定义未改。旧候选字节恢复，新QML待最终独立候选同步。未运行媒体或像素验证，P3仍未完成，未关机。

> **2026-09-28 a28 114–116：** 当前Goal active，P3未完成、不关机。114固定光流/互斥FG/单SR及两进程重启合同通过，候选Main恢复，无媒体运行；这是已有合同的复核，不是新增功能。115核验历史存档：合法旧Protection仅已知线性v1，编辑侧车初版就拒绝Protection；不再将人工非法侧车成功迁移列为待办。100–104合法迁移和损坏数据原子拒绝继续有效。116双守卫pass；本片没有产品代码修改。下方paused/blocked及旧侧车缺口语句均为历史。

> 2026-09-28 a28 111–112：真实ExportJobManager配置值隔离CPU测试通过（4.099秒，10断言）：开始/排队快照不随调用方节点参数或返回快照修改而变化。子进程为非编码生命周期替身，不能冒称完整导出/GUI端到端通过。四NR/三Color完整settings比较；冻结后端未改，P3仍未完成，不关机。

> **a28 105–109：** ColourPanel补齐按住看原图，复用原holdOriginal接口；107真实鼠标按压/松手、页面/标签隐藏复位、组旁路保参/组还原隔离通过，EXE未改，候选`candidate-a28-original`。106失败为stage取build旧QML，显式同步源码后通过。108/109焦点辅助窗口未产生可验证的主窗失焦事件，虽有off回调仍不算失焦验收；此路径停止，不再重试。未做像素对比，不勾P3整体完成。

> **a28 100–104：** 旧线性预设Protection在SR前/后、NR层间/全栈后且启用/禁用的8组合迁移通过，剩余完整链字段/布局及原文件字节保留；迁移侧车新reader一致。编辑侧车重复ID/悬空边/旁置自环/合流4类磁盘加载原子拒绝通过，原文件与有效状态保留；旁置Protection只证明新保存拒绝，不能称旧侧车迁移成功。103构建、104完整PresetLibrary 187断言通过。产品候选不变，P3-01及P3整体仍不勾完成。

> **a28 095–098最新状态：** 节点离线导出四入口先行模式门禁已修；095红测复现空字段遮盖模式提示，098真实QML/鼠标绿测通过，节点模式不打开选择框，列表空字段校验及列表/节点预设过滤保留。新候选`candidate-a28-export-gates`，EXE SHA256 `8d4660075191c71277551768649fd6e4a8deb5523b6e33cde5c3c5f6ae256d39`。未运行编码，不冒充正向导出或冻结作业配置隔离通过；P3仍未完成。下一片S1旧Protection迁移边界；不重跑200链和50切换。

> **a28 084–093最新状态：** 200条固定种子真实生产建图通过，六档NR/1–4NR/1–6Color/禁用槽/单SR及HDR、DLSS FG均覆盖；363个NR参数块创建/显式销毁对应。最长批次44.537秒，不代表200条逐链播放或像素。133条既存TrueHDR能力查询untracked警告已定位并保留，销毁返回未测，冻结NGX后端未改。候选仍为`candidate-a28-lifetime`；仅外部验收与文档变化。下一片S1迁移、S2导出边界，不重跑200链/50切换；完整S4/S5、逐节点计时及最终交付仍未完成，不关机。证据见WORKLOG和`production-plans/result.json`。

> **a28 075–082接续（2026-09-28）：** 阶段诊断/队列/输出尺寸已纠错，NR逐层参数块退出遗漏已修；单层/四层真实GPU退出无兜底告警。082同进程6次预热+50切换、925条系统采样满足预定资源上限，57组创建/销毁对应，无QML/参数块告警；内存仍增长，不宣称长期无泄漏。候选`candidate-a28-lifetime`。200链生产建图、完整迁移/像素与冻结的逐节点计时缺口仍在，S6/P3未完成，不关机。

> **a28 061–074最新状态（2026-09-28）：** 断线/旁置SR、FG、光流全局草稿与最后有效运行参数已分离；session v3/preset v5独立sidecar及旧格式读取、运行DLSS6X/草稿XeSS2X保存重启通过。069/070构建和CPU、072–074真实bridge/菜单/复杂复制回归通过。最终候选`candidate-a28-globals-final`；成功request计数不代表GPU零影响。S1/S2剩余边界、S3全控件与GPU、S4生产矩阵、S5完整迁移和S6/S7仍未验齐；P3未完成，不关机。失败及证据见WORKLOG。

> **a28 053–060当前切片：** 整页reset导致多节点(0,0)叠放已先复现后在QML桥接层修复；059真实右键重置/连接及旁置删除/4NR与6Color上限，060完整曲线/HSL/分级/LUT强度与NR六档参数独立复制及重启通过。当前候选`candidate-a28-reset-layout`，不是P3交付版。仍缺断线全局参数隔离、GPU资源/生产验收等，不关机；证据及已保留失败见WORKLOG。

> **a28 045–052已核实：** 部分预设拓扑、SR完整复位、断线草稿显示、页面reset保护遗留已补修；完整旁置/断线预设v4独立保存及真实QML重启恢复通过。两进程无QML错误，非GPU/资源趋势验收。S3/S5及P3仍未完成，不关机；详情见WORKLOG。下一片右键删除/重置及目标/容量边界。

> **a28 037–044编辑图接线：** 三项右键/旁置复制/端口连线/独立编辑文档与最后运行链隔离已接入，CPU及真实鼠标/双进程重启通过；没有GPU/资源趋势验收。部分预设临时拒绝、SR完整复位、断线草稿显示和页面reset仍须修验，不能称S3/S5或P3完成。详情及失败记录见WORKLOG；恢复点`s3-editor-wired-slice`。

> **a28 032–036属性提交修复：** 已复现并修复SR/HDR/FG设置被旧链覆盖、列表开关未提交的问题；双模式API/倍率边界/保存重启通过，非GPU或逐控件验收。恢复点`s2-setters-verified-slice`；S3旁置选择/右键/接线、S4–S7及S1/S2待验项仍未完成。P3未完成，不关机；详情见WORKLOG最新节。

> **a28 S3参数复位切片027–031：** NR/颜色参数单项复位已接真实控件，19项组件鼠标检查与实际bridge保存重启通过。S3右键/旁置/完整编辑图接线仍未做，S1/S2剩余边界及S4–S7保留；P3未完成、不关机。下一片编辑文档与运行链隔离。

> **a28 S3基础接续：** 稳定ID/旁置编辑图与独立schema2存储已构建并通过CPU测试（022–026），尚未接bridge/QML，不是画布交付。S1/S2仍有待验项，S3–S7未完成；继续逐参数复位及编辑图接线，不关机。证据/外部恢复点见WORKLOG顶部。

> **a28接续：** S2入口/保存隔离双进程测试通过，候选candidate-a28-s2；尚非光流/FG GPU全验收。S1/S2仍有待验项，S3–S7未完成，不关机。下一片实现旁置复制所需编辑图身份，证据与恢复点见WORKLOG；下方历史“最新”不覆盖本条。

> **a28切片进展：** 节点保护移除/旧配置非覆盖迁移、列表全局保护UI已实现；CPU迁移及四项GPU短测、真实QML双进程检查通过（日志001–017）。S1完整边界验收仍待补，S2–S7未完成；不称P3交付、不提前关机。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a28-s1`；阶段切片存档`E:/项目/Veyra/archives/p3-execution-20260928-a28/s1-verified-slice/`。

> **2026-09-28 a28当前状态：Goal active，P3未完成。** 用户授权开工、每阶段存档及P3完成后关机。新目标已创建，原checkout及隔离32路径守卫均pass，先推进S1保护遗留清理，再按S2–S7执行。工作区仍为E盘`p0-p1-r53-20260927`、分支`codex/p3-node-backend-20260927`。本次不改变冻结项，不自动进入P4/P5；未全部验收不关机。开工日志/存档分别为`E:/项目/Veyra/logs/p3-execution-20260928-a28/`、`E:/项目/Veyra/archives/p3-execution-20260928-a28/`。下方paused等均为历史。

> **2026-09-28 a27 当前状态：P3未完成；最新读取Goal为paused。本轮只修订方案，没有启动/恢复无人值守，也没有修改产品代码。** 用户确认继续NR跨SR和多调色合法穿插；编辑改为节点右键删除/复制/重置、每个滑条单项重置；复制为旁边的独立未连接副本，数量/单SR/互斥FG约束不可绕过。2.0仅列表模式/列表预设可离线导出，节点导出延期，不再阻塞交付；其他节点工作台、诊断、字幕/屏幕及设计需求保留。
> **唯一下一次执行顺序：** `UI_P3_REMAINING_EXECUTION_2026-09-27.md`已原位重写为S0守卫→S1节点Protection遗留/列表全栈保护→S2光流/补帧/单SR/导出门禁→S3右键与滑条重置→S4合法交错链→S5编辑运行保存恢复→S6真实诊断/200链/50切换/资源警告→S7验收交付。旧A–E、a21/a23“下一步”及下方active等文字是历史，不覆盖本条。a24撤回后仍待构建/定向回归，合入下一必要代码候选；不再进行保护节点或第二SR探索。

> **2026-09-28 最新有效范围：节点模式取消NR保护区域；列表模式保留一个全局保护配置，作用于全部NR叠层，并保留应有非NR效果。** 节点层间/任意位置/跨SR保护原图探索和相应验收从P3删除，不再作为阻塞项；下方a22/a23等保护记录仅是历史，不覆盖本决定。节点设计改为7类（光流配置、调色、超分、NR、RTX Video HDR、DLSS补帧、XeSS补帧；输入/输出另计），光流固定输入后、补帧互斥、超分单实例。唯一执行入口为`UI_P3_REMAINING_EXECUTION_2026-09-27.md`第1.1/1.2节。**本轮仅改方案；入口移除、旧配置兼容、列表全NR栈保护及a24撤回后的定向验证尚未执行，P3未完成。**

> **当前最新：P3 C层间Protection切片a22通过**（承接2026-09-27任务，主机时钟2026-09-28凌晨）。`NR→Protection→NR`、SR后同尺寸层间保护及SR前多NR层间保护均实际执行；六个GPU新场景+一个受影响回归通过，保护和下游输入参考max FP16 ULP=0、最终≤1 code，未重复保护。产品真实顺序`SR→NR→Protection→Color→NR`的live/独立模式/保存重启通过，Main恢复。当前候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a22`，EXE SHA256=`788aa7b5ca5e862b2a4e94e0dc869a4cf75dab32aa6316bbbe02cf1f39514cfd`；证据`internr-protection-order-a22-v1/result.json`及`E:/项目/Veyra/logs/p3-internr-protection-20260927-a22/001–006*.result.json`。跨SR后全栈Protection原图及其他合法位置仍未完成，D/E及A整页接口边界仍在；Goal active、完整P3未完成。下方“最新”均为历史记录。

> **最新：P3 C源尺寸Protection切片a21通过（2026-09-27）**。真实执行`NR(s)→Protection→Color(s)→SR→Color(s)→NR(s)→Color(s)`；四场景保护FP16参考max ULP=0、SR输入/最终输出误差≤1 code、下游NR实际生效。产品edit/reload、live、模式隔离及保存重启通过，Main恢复。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a21-v2`，EXE SHA256=`31e5189430f0c27a627bec532b37003d6733510e59d74c208c2414ccccf05c90`；证据`protection-order-a21-v2/result.json`及`E:/项目/Veyra/logs/p3-protection-boundary-20260927-a21/`。005首次8位敏感性断言失败已留档，010强化为FP16独立参考后通过。只证明该保护位置，不代表SR后全栈保护、任意合法位置、完整鼠标编辑或资源稳定性已完成；Goal active，完整P3未完成。

> **最新：P3 C跨SR的NR分段执行a20通过RTX SDR定向验收（2026-09-27）**。NR→SR→NR及四NR跨SR已实际执行，两侧Color/live改值、两模式隔离/切换、保存重启通过；最新v2另通过live非法改值不污染状态及拒绝后继续真实渲染。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a20-v2`，EXE SHA256=`ba19b6102e7be43d215cca5ac54d29a84dfd9a0a80ffe298218aebf68ee16b68`；证据`split-nr-order-a20-v2/result.json`与`E:/项目/Veyra/logs/p3-split-nr-20260927-a20/007、013、014*.result.json`。SR输入参考误差≤1 code，后NR输入/最终输出max code=0。合法Protection跨SR原图引用仍待接入，DLSS/FSR/HDR分段未实机验收；节点完整编辑、资源趋势、退出参数块警告及整页接口边界仍未完成。Goal active，完整P3未完成；下方均为历史进展。

> **最新：P3 C预SR调色切片a19通过RTX SDR直接验收（2026-09-27）**。NR→Color(s)→SR及多NR/Protection后的调色已实际执行，SR输入与独立参考max code=0，最终输出与真实SR结果max code=0；live、模式隔离/切换及重启恢复通过。候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a19`，证据`pre-sr-order-a19-v1/result.json`及`E:/项目/Veyra/logs/p3-pre-sr-color-20260927-a19/007–008*.result.json`。DLSS/FSR/HDR接点已连接但不冒充实机验收。下一项NR→SR→NR；退出参数块警告、完整节点编辑、资源趋势及整页接口边界仍未完成。Goal active，完整P3未完成；下方全部为历史进展。

> **最新：P3 C层间Color切片a18直接验收通过（2026-09-27）**。连续NR→Color(s)→NR、四NR三个调色边界及SR前源尺寸链已实际执行；输入max ULP=0，最终误差≤1 code。产品edit/reload、live不重建、两模式隔离/恢复和导出保护通过。当前候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a18-v2`；证据`inter-nr-order-a18-v1/result.json`、a18日志010的普通多层通过场景、012的preview补测及013产品验收。010整体exit1已记录，不掩盖失败。下一项NR→Color→SR；NR跨SR、完整节点编辑、资源趋势、整页接口边界仍待完成，参数块警告未定位。Goal active，完整P3未完成；下方为历史记录。

> **当前产品证据：P3 C中间Color切片a17通过（2026-09-27）**。SR→Color→NR→Color已连接原GPU算法：真实640→1440 SR，NR输入逐通道max half ULP=0，最终输出误差≤1 code；尾部受影响回归通过。产品edit/reload均exit0，live不重建、两模式隔离/切换/重启、单项及批量导出拒绝通过，qml_errors=[]，测试Main已恢复。当前候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a17`；证据`prenr-order-a17-v1/result.json`及`E:/项目/Veyra/logs/p3-prenr-color-20260927-a17/001–007*.result.json`。下一项是NR之间Color；NR跨SR、完整节点编辑、资源趋势和调色整页仍未完成，退出参数块警告尚未定位。Goal继续active，完整P3未完成，下方为历史记录。

> **当前产品证据：P3 C尾部Color切片a16已通过（2026-09-27）**。NR→Color真实GPU逐像素、live改值、禁用槽/多Color及QML切换/重启通过；批量导出防摊平保护已补。曾发现live输出绕过tail的真实接线bug，已修复并复测。当前候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a16`，证据`tail-order-a16-v1/result.json`与`logs/p3-tail-color-20260927-a15/011-tail-gpu-pixels.*`。C仍是部分支持，A/C/D/E未全部完成；Goal继续active，旧blocked段落是历史。

> **最新真人授权恢复（2026-09-27）**：用户“允许，继续吧”明确恢复P3限定节点后端接点，Goal已active；其余冻结不变。开始C真实合法顺序执行，P3仍未完成。下方blocked仅为此前停止记录，当前唯一队列仍为`UI_P3_REMAINING_EXECUTION_2026-09-27.md`。

> **最新状态（2026-09-27）：Goal工具已确认blocked，不再active。** 同一UI-only/后端接点授权冲突持续三个实际目标轮次，安全UI切片已验证，无新授权可继续。完整P3未完成：A局部通过、B通过、C–E未完成；a14及全部旧证据保留。未新增产品修改/测试。下方active描述为此前记录；恢复条件及唯一队列见`UI_P3_REMAINING_EXECUTION_2026-09-27.md`。

## 最新：P3 a14安全UI切片已验证，完整P3仍受阻（2026-09-27）

- A混色器控件同状态对照通过：实际1280×800/DPR1、318px内宽、150px滑轨列对齐、38px行距、中心填色/正数显示；7定向用例+init/cleanup共9 passed，真实bridge参数/旁路/还原通过。整页片源/工具栏有接口边界，A不整体勾选。
- B保存重启沿用a9-v3通过；C–E不越过最新UI-only冻结。没有新的后端授权，不进入合法交错执行、节点编辑后端或资源趋势施工。Goal仍active/未完成；后续按真实目标轮次执行三轮阻塞停止条件，不重复A/B证据。
- 候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a14`；证据`E:/项目/Veyra/logs/p3-colour-controls-20260927-a14/final-colour-comparison.json`。原生exe未变，仅QML及定向测试修改，未提交/合并/推送/发布。唯一剩余队列见`UI_P3_REMAINING_EXECUTION_2026-09-27.md`，下方旧记录不覆盖本条。

## 当前推进：P3调色控件续接，后端停在最新冻结边界（2026-09-27）

- Goal active，完整P3未完成。用户本轮重新提供的UI-only规则优先于此前节点后端授权；C–E后端接点不继续修改或回滚，也不靠guard放行替代授权。原checkout/main/1.4.4和既有候选不变。
- A的a11补齐紧凑图标组头、150px对齐滑条、校准渐变；5个定向鼠标用例通过，真实bridge页面检查exit0且无QML错误。同全部状态设计对照尚未通过，A不整体勾选。B保存重启已在`runtime-order-a9-v3`通过，不再沿用a7失败作为最新结论。剩余唯一队列与证据见`UI_P3_REMAINING_EXECUTION_2026-09-27.md`。下方blocked为旧Goal历史，不代表当前工具状态。

## 当前结果：P3-a调色通过定向验收，P3未完成（2026-09-27）

- **目标状态：blocked。** 同一后端接口缺口与UI-only冻结冲突已连续三轮复核，update_goal已确认阻塞；不再自动续跑重复测试，完整P3未缩减或标完成。继续须用户明确授权节点后端范围，并遵循另开分支/另做验收的现行规则。

- 调色setter/总开关改为写实际选中Color节点，消除fromChain覆盖；补真实点曲线、八色HSL/黑白混色器、四区H/S/L色轮、分组旁路/重置。沿用1.4.4参数合同和既有ColorGradeTables采样，不改颜色算法。混合默认50、LUT强度默认100，拒绝非有限/越界输入。
- 验收：Qt控件14/0；真实播放器鼠标操作及第二进程预设恢复通过；原生D3D窗口的固定SDR图像像素对照通过，曝光/曲线/混色器/色轮均改变实际画面，关闭调色与中性基准逐像素一致。证据为本轮`real-a3`、`pixels-a1`，不是Qt空白视频截图。
- 节点页只修复已有共享链视图的入口、实例参数与布局问题，不冒充独立节点系统。`toChain`重建固定列表链，`fromChain`写回固定字段，尚无自由连线/任意拓扑执行和独立模式恢复闭环；R5.4/R5.5受当前UI-only冻结限制，不继续修改后端或重复测试尝试绕过。
- 最终节点交互013有/无视频均通过：确认/取消、返回列表、两层NR快捷参数不串改、NR/Color完整参数定位；实拍节点布局已核对。最终控件014仍14/0。候选在`E:/项目/Veyra/tests/p3-node-color-20260927/candidate-a1/`；45份QML与源码/build逐文件一致，旧P2候选不替换。节点独立配置、任意执行及完整设计动效仍未验收。
- 在原E盘隔离区继续，P2成果和候选保持不变；本切片只改QML/bridge/直接测试和文档，不动播放链路。执行和验收见 `P3_NODE_COLOR_EXECUTION_2026-09-27.md`。下方“不自动启动P3”等条目为此前阶段历史状态。

## 当前结果：P2非防闪功能收口，防闪明确延期（2026-09-27）

- 用户本轮指定的NR逐层识别/独立编辑、完整参数入口和每层独立内部处理尺寸已完成定向验收；新建默认1080p，六档与1.4.4一致。复制继承、保存重启、旧明确配置迁移、图片/视频离线完整尺寸合同均有对应测试。
- 最新可运行候选：`E:/项目/Veyra/tests/p2-nr-ui-resolution-20260927/final-candidate/veyra_qml_ui.exe`，SHA256=`967e2f0cf3e18f2dbd010f99dba5f2d4e81f39eca2dd0e559368e2a16de7f8d7`。在`codex/p2-nr-20260927`隔离区施工；原checkout/main/1.4.4和原用户手测候选未替换。
- 3组数据测试、Qt真实控件11项、真实播放器鼠标修改与第二进程恢复、3组混合尺寸GPU、离线export/still及单层短测通过。最终staging发现旧QML拷贝后按源码同步并重新验hash/测试；详见[P2执行记录](P2_NR_EXECUTION_2026-09-27.md)。
- **完成的是本轮非防闪目标，不是完整P2画质或2.0.0。** 防闪残留轮廓延期；节点入口/模式隔离与执行仍待P3，完整设计/动效待P5。保留旧退出参数块warning待后续定向调查；无长稳/其他显卡/实屏HDR/完整NVENC导出或四层60fps新承诺。
- 不自动启动下一阶段，不提交/合并/推送/发布/删除旧UI。以下为历史状态，以本节和P2最新验收表为准。

## 历史阶段：P2防闪已部分修复，画质仍未出关（2026-09-27）

用户已追加授权原定 P2 接线，复用 E 盘隔离区，分支 `codex/p2-nr-20260927`。1080p240 采集问题明确暂缓，不推断为电脑性能。1–4 层 NR 已经通过真实 QML→EffectChain→settings→GraphDescription→图执行，不再是只传第一层。独立参数、启停、添加/复制/删除、重置、共享光流、尺寸和九轮显存短测已有证据；先前单层九场景及 P1/R5.3 证据保留，不重复整套测试。

**P2 尚未出关，当前阻塞是防闪画质，不再是接线授权。** 保护区原图被前层 NR 改写、QML 保护开关被链映射覆盖，以及全栈保护误裁区外负残差均已定点修复。最终候选严格羽化 GPU 逐字节参照、原图/全关旁路、调色保护、带实证 PQ 标记的合成 HDR 和真实 QML 6步/17步交互通过。最后一轮 QML 测试曾读到应用完成前的缓存快照；引擎已关闭 NR，测试改为等待应用后的新快照，保留原断言和超时后通过，未为此修改产品。

用户现已授权迁移必需的合理解冻，不再存在shader授权阻塞。独立新baseline下，仅修`NrTemporal.hlsl`并扩展现有GPU测试：修复历史残差造成非负通道越零，以及线性暗部guide误接受明显亮度变化；先红后绿，保留signed/HDR，不降NR强度。75秒四层暗墙对照已明显改善；125秒亮粒子及渐变仍有细轮廓，**视觉门槛仍失败，P2不标完成**。本轮两次有证据修复后停止继续试调，不启动P3，不重复整套测试。

当前独立候选`E:/项目/Veyra/tests/p2-nr-temporal-repair-20260927/candidate-v2/`：EXE哈希仍为`1a850fe4590129edb17879c454c4ca694331dbd9be5d5cf7b0bd9464ceee42c0`，外置`NrTemporal.dxil`哈希为`f6fe501ca236eea3a7fdc5a086072750d1ebd528c0b6810f2d73bca7e8cc28fb`。真实QML17步、保护羽化严格GPU参照、PQ HDR、两种SR/NR顺序及四组游戏和两组文字捕获通过对应结构/数值断言；不把它们称为视觉通过。29项命令和失败归因见[P2执行记录](P2_NR_EXECUTION_2026-09-27.md)最新章节。旧候选、原checkout、main、1.4.4与既有P0/P1证据保留。

P0/P1 成果、原桌面 checkout/main/1.4.4 保留。当前真实 QML 候选：`E:/项目/Veyra/tests/p2-nr-wiring-20260927/final-candidate/veyra_qml_ui.exe`，SHA256 `1a850fe4590129edb17879c454c4ca694331dbd9be5d5cf7b0bd9464ceee42c0`，不是验收完成品。详情见 `P2_NR_EXECUTION_2026-09-27.md`；本轮恢复点为 `E:/项目/Veyra/archives/p2-nr-wiring-20260927/review-checkpoint/`。Goal 不标完成，P3–P8 不启动，未提交/合并/推送/发布/删除旧 UI。下方为历史状态。

## 当前活动：仅 P0/P1 隔离修复（2026-09-27）

用户追加授权“先开目标模式把p0和p1解决掉”。本轮只在 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` / `codex/p0-p1-r53-20260927` 工作；原桌面 checkout、main、1.4.4 包及原型均保留。P2–P8、R5.4/R5.5、合并/推送/发布和旧 UI 删除未授权。下方“只改方案/未开工”是历史状态。

P0保护已完成。P1发现并定点恢复默认融合调色的一处曝光舍入回归：严格全图对照从999字节不同恢复为344064字节完全一致；未重写播放器。多实例GPU、HDR、生命周期、设备资源释放/新设备重建、平铺图像和11场景视频颜色导出通过；修复后QML三项测试及八入口/真实播放短测通过。设备资源测试不等于整机TDR恢复，QML短测不等于全部交互验收。

**P0/P1（R5.3限定范围）已收口**：四种底层性能及同修复后端QML/Win32外壳对照全部在原门槛内；45次正式采样复核通过，无替代重测。1.4.4独立行为锚点出帧；旧快照/修复候选两种固定MJPEG采集模式都真实出帧，历史零帧本次未复现。1080p240共有丢帧/频繁PTS重置按用户最新决定暂缓，不再列为本轮P2专项，仍不宣称全播放器全绿。新QML的17屏、42动效与设计稿1:1验收仍未完成，不用旧窗口代替。完整命令、最终候选哈希、失败归因、外部checkpoint及边界见 [P0/P1执行记录](P0_P1_R53_EXECUTION_2026-09-27.md)。本段描述P0/P1目标的结束，当前追加P2状态以上文为准。

## 当前活动：Claude + Codex 需求核对、设计动效方案（2026-09-27）

已补核三段后续 Codex 对话 `01a0dbab… / 01a0dd72… / 01a0de1a…` 的真实用户消息和 Git 方案修订，与 Claude 原始需求合并到 [完整升级与恢复总方案](UI_V2_0_0_MASTER_PLAN_2026-09-27.md)。完整历史需求和 R5.3 债务保留，但计划不是新授权：当前仍为 AGENTS 规定的 UI migration，后端另行授权、另开分支、另做验收。

恢复完整升级获明确授权后的顺序：`P0 授权/保护合同 → P1 R5.3 → P2 NR/旧链 → P3 R5.4/R5.5 → P4 全部功能 → P5 设计动效 → P6 总回归 → P7 候选包 → P8 授权交付`。进度只记在 [统一执行记录](UI_MIGRATION_EXECUTION_2026-09-25.md)，不自动重启历史 Goal。

**设计稿仍在**：包括入口 `prototypes/ui-redesign-2026-09-25/index.html` 在内的16个核心设计文件相对 `5f307fc` 内容一致（仅换行差异）；实质变化是截图辅助文件。P5 已明确尽量1:1还原17屏、42动效及 Codex 延期 UI 缺陷，要求实际查看对照图、验证时序/曲线和真实交互；字体/模糊/播放条位置的默认折中不算用户批准。以上均是待验收标准，不是已还原/已测试结论。

本轮仅修订文档，未修 R5.3、未重新构建/功能测试、未启动无人值守。现有 AGENTS、scope guard、baseline 与后端冻结保持原样；必须先明确代码开工范围并完成 P0，不能用新计划暗中绕过保护。早前 QML 构建/入口/自动 smoke 通过不代表底层、全部交互或完整 2.0.0 验收。

分支为 `codex/ui-qml-migration-20260925`，起点 main `df41580`（标签 `checkpoint/pre-ui-qml-migration-20260925`）。
当前目录是该分支的主 checkout，不是 managed linked worktree；未合并 main、未推送、未发布，旧 Win32 UI 未删除。
R5.3 旧性能、采集、导出及 unit 记录保留用于归因，不冒充新候选通过；旧程序在复跑中也存在性能漂移，尚不能把历史差值全部归因于新代码。1.4.4 正式包和 main 保留为回归锚点，当前开发版正确性仍需实证。

## 正式版 1.4.4 已发布（2026-09-22）

`v1.4.4` 已推送到 GitHub 并作为 latest 发布：源码提交 `6851c27`，
https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.4 。
便携包 472478966 字节 / SHA256 `BFF5149B55F8665FEBEAECEEE421956DF72370FA56515A013EE6ABBD45B8878B`，
对应源码 215437386 字节 / SHA256 `7D3A8EFED76422EAB536D714221F579F1478921B927149FBB5ADFFA8635C53BA`；
发布 EXE SHA256 `92AC81B14AB050895B33FD2A822C179F65C4A42BD89C6C00772B89C5684D7121`，
与用户已验收的 test6 包一致。便携冒烟 7/7 PASS，受影响 UI 门槛全过；README 更新日志只保留 1.4.4
（介绍/功能表/使用教程保留；首次改写误删，已在 `b5821cc` 找回）。
完整步骤、失败记录与遗留见 [1.4.4 发布执行记录](RELEASE_1.4.4_EXECUTION.md)。

## 1.4.4 test6：UI 修复重构建（2026-09-22）

test5 反馈的两处 UI 问题已修复并重构建：`严格补帧节奏` 之前不在帧生成页的 final layout 里
（保留创建坐标 `y=190`，与上下两行实测重叠 4/12 dip），现已按 8 dip 节奏独占 `y=370` 一行；
面板滚动改为在输入消息内提交整帧重绘（`RDW_UPDATENOW`）+ 视口外行虚拟化，滚动瞬间抓屏与
稳定帧差值 4263→0 像素。补帧调度逻辑未改。新增 `veyra_settings_layout_tests`；受影响
UI 门槛全过。产物 `E:\项目\Veyra\test-packages\1.4.4\Veyra-1.4.4-test6`
（`veyra.exe` SHA256 `92AC81B14AB050895B33FD2A822C179F65C4A42BD89C6C00772B89C5684D7121`）。
**未推送、未发布。** 详见 [WORKLOG](WORKLOG.md) 顶部。

## 最新入口：统一修复计划（2026-09-22）

分支 `codex/fg-independent-repair-20260922`（存档 `checkpoint/pre-fg-independent-repair-20260922`）。
已实施并短测的 FG 修复见 [执行记录](FG_INDEPENDENT_REPAIR_EXECUTION_2026-09-22.md)：
XeSS 真实源周期提示、有界跳帧保留历史、DLSS 超预算对改 2X 组、显示帧统计；
X2/X3 已撤回。[统一修复计划](UNIFIED_REPAIR_PLAN_2026-09-22.md) 第 1–9 批已实施（标签
`checkpoint/plan-b9-done-20260922`），结果、XeSS 复跑排查（时段差异，非回归）、
直写 upload 堆的实测否决与专业模式延迟实测见
[执行记录](UNIFIED_REPAIR_EXECUTION_2026-09-22.md)；
依据为 [全软件清扫](WHOLE_SOFTWARE_SWEEP_2026-09-22.md) 与
[采集延迟复查](CAPTURE_LATENCY_REVIEW_2026-09-22.md)。运行入口
`E:/项目/Veyra/tests/fg-independent-repair-20260922/app/veyra.exe`（staging，非便携包）。
长测、实卡、肉眼验收由用户执行；未合并 main、未推送、未发布。下文为历史状态。

## 最新结果：有界修复收尾（2026-09-22）

本轮已完成有限范围的实施与验收；下节“只出方案”为历史记录。
开工存档 `7724ea8` / `checkpoint/pre-bounded-repair-20260922`，
当前分支 `codex/bounded-full-chain-20260922`。
保留修复：HDR 查询状态按显示器隔离 `f718c02`，采集格式身份恢复及协商核对
`7fe6201`，分别有 `checkpoint/hdr-target-state-20260922` 和
`checkpoint/capture-format-contract-20260922`；此前 P010 上传优化继续保留。
完整证据见 [有界修复执行记录](BOUNDED_REPAIR_EXECUTION_2026-09-22.md)。

产品构建、相关 UI/字幕/颜色/预设、后端切换、暂停 seek、实卡格式重连通过。
独立的运动矩形诊断在 DLSS 6X 位置误差门槛失败，已记录，不能列入通过项。
RTX5070 原生视频+NR：DLSS2 末段约120提交/s，DLSS6约287但P99仍16.79ms；
真超分+NR：DLSS6约148提交/s，XeSS4约40源提交/s。均为有界追踪末段，
不是整段均值或物理屏幕FPS，也没有同期旧版A/B证明本轮性能收益。

高倍率不均匀、欠速闪烁、间歇卡顿及历史 XeSS 重负载倒退仍未解决。
本轮没有新的产品调度改动；查过 Intel/Magpie 合同后未确认可靠的新修复点，
按用户要求停止无依据的性能尝试，既不宣称全是硬件瓶颈，也不宣称全部修好。
未重新验收的设备/功能及未交付项见总台账。本机开发运行入口：
`E:/项目/Veyra/tests/bounded-repair-20260922/app/veyra.exe`，依赖本机链接，非便携包。
本轮没有打包、合并 main、推送或发布；关机由最终交接执行。

## 当前任务：全链路回归复核，只出方案（2026-09-22）

完整问题入口：[全软件问题台账及历史 DLSS/XeSS 专项](WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22.md)。
历史 DLSS 6X 的高提交率/长空档、真超分+NR 下 XeSS 4X 相对 1.4.3 的源连续性倒退
仍未解决，列为第一优先级；最新采集日志不取代这些问题。台账同时列出其他模块的
未解决项、代码/功能缺口、已有修复的实机验收边界和暂缓项，不把它们一概标成新 bug。

最新入口：[卡顿、闪烁与采集清晰度全链路方案](FULL_CHAIN_REGRESSION_REPAIR_PLAN_2026-09-22.md)。
当前开发分支 `codex/5090-capture-fg-20260921`，HEAD `edfd886`，仍在
`E:/项目/Veyra/worktrees/playback-nr-20260920`。下方分支名和“下一步”属于历史状态。

新用户日志的 8 次图初始化均关闭 SR/NR/FG/NVOF；4K30 P010 有约 1.18/1.31 秒
真实回调来帧间隔。上游停供、应用回调阻塞与线程调度仍未区分，不能归因于补帧。
另确认采集最近打开只持有数字格式索引、默认重连帧率核对缺失的合同风险；
日志中有手动改模式，不能称用户已遭静默降级。采集色度/最终缩放路径差异单独验清晰度。

用户要求先排查和写方案，本轮未新增产品代码或运行测试。既有未提交 HDR 查询
三态候选仅构建未运行，SDR 呈现资源测试通过不等于用户欠速闪烁通过。
“约 15 秒”不作为固定计时器假设；DLSS/XeSS 从 2X 到各自最高倍率一起覆盖。
已失败的扩队列、固定等待、拆 owner、重叠、全局取消 admission 等未列为新施工项。
没有新增包、提交、合并、推送或发布；用户实际闪烁和间歇卡顿仍未解决。

## 最新实修：5090 P010 采集反馈（2026-09-21）

工作分支 `codex/5090-capture-fg-20260921`，开工存档 `3953bff`。
已去掉 CPU P010/P016 上传前的冗余转换/复制；本机两轮 P010 上传中位耗时
约 2.4ms 降至 0.93–0.98ms，18 项实际 GPU 像素输出与基线完全一致，HDR
颜色测试及 DLSS 呈现资源回归通过。不改变画质、倍率或增加等待。
这不是 5090 整链路帧率验收；15 秒卡顿、欠速闪烁、高倍率不均匀仍未解决。
实测工况、范围和证据见 [5090 采集修复记录](RTX5090_CAPTURE_FG_REPAIR_2026-09-21.md)。
未更新既有便携包；本轮仅本地构建，无合并、推送或发布。

## 当前执行入口（2026-09-21，链路复核后）

1.4.3 之后的完整修复、回退、诊断和未完成项总账见
[POST_1_4_3_REPAIR_LEDGER_2026-09-21](POST_1_4_3_REPAIR_LEDGER_2026-09-21.md)。

此前研究方案：[DLSS / XeSS 补帧执行与呈现重构](FG_RUNTIME_REPAIR_PLAN_2026-09-21.md)。
基于 `b69d22c`、固定 Magpie 源码、Intel SDK 合同和既有测试；本轮仅更新文档，
未实施重构、未运行新性能测试。方案覆盖 2X、DLSS 最高 6X 与 XeSS 最高 4X，
先核对资源和线程合同，只修有证据的时间/准入或资源错误；不再把关键路径重叠列为默认待办。当前不能承诺
所有硬件与负载有 90% 成功率；高倍率稳定性和 30/40 本轮实卡验收仍未完成。

本轮另生成两个 1.4.4 XeSS A/B 测试包：当前 XeSS 与仅恢复 1.4.3 XeSS
实现的对照包。两包都保留同一 1.4.4 其他修复，短测均能进入 XeSS 4X；
路径、差异和限制见 [1.4.4 XeSS A/B 对照](XESS_1_4_4_AB_COMPARE_2026-09-21.md)。

继续优化前先查 [实验索引与重试约束](FG_EXPERIMENT_INDEX_2026-09-21.md)：
区分已删除的失败实验、默认关闭的未验收候选和只读诊断，禁止无新证据重复已失败方案。
已同步并复核 [Magpie 最新 experimental 源码](MAGPIE_SCHEDULING_SOURCE_AUDIT_2026-09-21.md)：
上游仍为3841698；其DLSS最高4X且逐生成帧等待，XeSS包含时间戳改写和独立队列交接，不能直接推断性能更好。

既有执行与其他功能收尾：[补帧稳定性与现有功能收尾](FG_STABILITY_COMPLETION_PLAN_2026-09-21.md)。
用户已授权执行；源帧关联诊断已构建并运行，XeSS真实源时间提示候选在120秒测试中保持吞吐收益，
整段统计仍发现约四分之一的输出间隔短于1ms及约46ms的偶发长间隔；后端/倍率切换回归通过。
尚未通过完整节奏/画质验收，默认仍关闭。详见
[本轮诊断与候选](FG_STABILITY_PROGRESS_2026-09-21.md)。
新增只读 fence 追踪：重负载保留的351次首帧调度全部在进入时有未完成GPU依赖，
不能把约8.5ms的前段耗时全部当作无效等待删除；仍需区分队列积压、GPU工作与唤醒。
诊断版后端切换回归通过，尚无新增已接受的调度优化。
后续“XeSS仅保留一组待处理”实验降低了突发与帧龄，但源帧吞吐退步，已撤回并重新构建。
DLSS旧证据复核确认重负载整组空档主要伴随预算拒绝/历史播种；同组GPU耗时也超过60fps预算，
尚不能归结为纯调度故障或直接取消预算检查。具体数字见WORKLOG最新条目。
开发工作区为 `E:/项目/Veyra/worktrees/playback-nr-20260920`，
分支 `codex/fg-stability-20260921`；开工存档 `d2ae8ee`，诊断存档 `0143baf`。
正式版仍为1.4.3，最新本地交付仍为下述1.4.4beta。

- **XeSS**：真实1080p到4K超分＋NR＋4X的源帧连续性比1.4.3差；旧版反复
  关闭补帧，不能直接恢复它并宣称4X修好。果冻感、功耗波动尚未解决。
- **DLSS**：4X/6X重负载均匀呈现仍未通过；不能将原生4K输入的高FPS套用到真实超分。
- **输出限制**：默认不限制；XeSS/FSR输出限帧尚未完成。
- **NR**：防闪默认关闭、部分样本验证通过，快速游戏画质及重负载有待验收；
  独立降噪未接入，模型风格仍为0/1/2，NR叠层继续暂缓。
- **UI/字幕/采集**：已有修复和局部回归不撤销；快速滚动、物理跨屏、受影响
  设备及HDR浮窗观感不得算全面通过。杜比视界新处理继续暂缓。

最新同条件证据见[版本对照](RELEASE_143_SCHEDULING_COMPARISON_2026-09-21.md)，
原因与未知项见[链路审查](FG_PIPELINE_REASSESSMENT_2026-09-21.md)。
下文按历史顺序保留；“未发现回退”“通过”“最新”等只适用于对应日期和工况，
与本节冲突时以本节及其引用证据为准。

## 既有交付与历史记录

2026-09-21 local test package delivered from source checkpoint `f029707`:
`E:/项目/Veyra/test-packages/1.4.4beta-20260921-nr-followup/`.
Seven portable smokes, packaged DLSS/XeSS switching and 124-file ZIP
verification pass. This is partial capability acceptance; the true-SR
high-multiplier cadence and independent denoise limitations below remain.

2026-09-21 follow-up: retained temporal mask/time corrections and GPU
neighborhood reuse; fixed repeated Present-deviation sampling and added
separate entry/return diagnostics. NR temporal remains default-off.
Local 120-second DLSS/XeSS 4X tests with original 4K (SR bypass) pass;
true 1080p-to-4K SR plus NR fails 4X/6X cadence acceptance. Independent
VFX denoise cannot create its effect and is not integrated. No fixed delay,
hidden downshift, new runtime, main merge or publication. Detailed results:
[current follow-up acceptance](NR_FG_FOLLOWUP_ACCEPTANCE_2026-09-21.md).
This supersedes the timing interpretation and pending tests below.

2026-09-21 local bounded NR/performance review: temporal protection/time fixes
and shared-neighborhood GPU optimization tested on RTX 5070. Default remains
off; natural-video visual acceptance and independent VFX denoise are not
delivered. No general 1.4.3 performance regression established. See
[evidence and limits](NR_QUALITY_PERFORMANCE_ACCEPTANCE_2026-09-21.md).
No new release or main merge.

**2026-09-20：1.4.3 已正式发布为 [GitHub Latest](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.3)。** 发布源码/标签对应 `3b4570e`，正确修复已整合至 main；正式便携包七项检查、120 个载荷校验、对应源码 856 个文件校验及四项远端资产哈希核对通过。用户最新测试包与正式 EXE 完全相同。固定 6X 均匀呈现及未定因设备反馈仍按下方边界处理。发布记录见 [RELEASE_1.4.3_EXECUTION.md](RELEASE_1.4.3_EXECUTION.md)。

2026-09-20：用户验收最新 1.4.3 测试包并授权正式发布。当前发布工作包含截至 `139db25` 的产品修复及远端中文 README 精简，分支审计与正式构建/发布结果统一记录在 [1.4.3 发布执行记录](RELEASE_1.4.3_EXECUTION.md)。下方“仅本地/未发布”语句均为历史阶段记录；固定 6X 均匀呈现仍未解决，不随正式版发布改为通过。

2026-09-20 后续修复：设置数值草稿、羽化刷新递归通知和单项还原已修正；跨屏 DPI 字体/布局刷新合并到异步 UI 消息，避免 SetWindowPos 内重复布局。DLSS 6X / XeSS 4X 合成 DPI 切换与设置回归通过。本机仅一个物理显示器，不能宣称真实双屏卡死已验证消除。新本地包目录 `E:/项目/Veyra/test-packages/1.4.3-20260920-display-fix/`，验收见窗口修复文档。本轮不改变补帧调度。

2026-09-20 窗口边角拖动崩溃已定位为 UI 回调栈耗尽，拆分 AppShell / SettingsWindow 的重型消息处理后，本机真实鼠标拖动、采集、DLSS 6X / XeSS 4X 缩放和合成 DPI 回归通过。新版测试包输出到 `E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix/`；下方 final 目录为旧包。本轮不修改补帧调度，固定 6X 均匀呈现仍未解决。见 [窗口缩放修复](WINDOW_RESIZE_REPAIR_2026-09-20.md)。

2026-09-20 用户决定暂止优化，交付重新构建的 1.4.3 本地测试包。基于 `d60ec88`，产品代码保持 `7a3dfae`，失败实验均回退；固定 6X 均匀呈现问题仍未解决。最新包位于 `E:/项目/Veyra/test-packages/1.4.3-20260920-final/`，构建、CPU/UI 回归及便携包七项运行检查通过。未推送或发布 GitHub；此前“目标继续”和“未打包”是历史状态。

2026-09-20 最新约束与实验：跳过 NR 优化，不增加等待，不自动降档。`7a3dfae` 保留用户所选倍率，`55e0bdd` 撤回不安全的共享状态回读。新增整组耗时预测、取消重复清屏、光流历史直接复制三项对照均未证明显著改善，已全部回退；固定 6X 本轮留存窗口约 298–303 提交/s，仍有约 17ms 空档，**尚未修好**。详见 [非 NR 实验记录](FG_NON_NR_EXPERIMENTS_2026-09-20.md)。下文自动 4X 结果仅是历史，已不代表当前行为。

最新恢复修正：预算不足时在当前原帧完成可承受的单次预热，避免下一组再次只出原帧。固定 6X 同参数约 280 提交/s，仍有约 15ms 空档，尚未完成均匀呈现修复。126 项 CPU 检查、2X/6X GPU 恢复检查、生命周期、主程序启动和 XeSS 2X 短测通过。详情见本轮部分验收文档，不能用降档结果替代固定 6X 验收。

更新：2026-09-20。发布状态与开发状态分别记录，历史计划不代表当前验收。

最新固定 6X 调查：同一 p001.mp4、RTX5070、NR 开启、4K 目标，首帧截止时间与排队成本修正后，固定 6X 末尾留存窗口约 265 提交/s，但仍有 15-17ms 空档，**尚未通过均匀呈现验收**。2X 仍稳定 120；请求 6X 自动回退实际 4X 时稳定 240，此结果不能代替固定 6X 修复。关闭 NR 的诊断对照固定 6X 可达稳定 360，不作为用户解决方案。121 项 CPU 检查及 GPU 恢复/倍率检查通过，详见 [本轮部分验收](FG_CADENCE_REPAIR_ACCEPTANCE_2026-09-20.md)。仅本地构建，未打包或发布。

最新节奏调查：已存档 `b2c3d0e` / `checkpoint/pre-fg-cadence-audit-20260920`，在 `codex/fg-cadence-audit-20260920` 复测相同实卡参数。关闭逐帧文件日志后仍约 194 提交/s，末尾 8.07 秒内 90 次间隔超过 16.667ms，478 批中 233 批只呈现原帧，直接记录到 114 次拒绝后预热。确认提交节奏断续，尚未修复；PresentMon 权限不足，未测物理显示。见 [节奏调查](FG_CADENCE_AUDIT_2026-09-20.md)。

最新 DLSS 恢复修复：`codex/dlss-recovery-20260920` 从 `c8a3828` 隔离，将无有效 A/B 历史时的整组 MFG 重置改为单次完整预热，按实际预热成本判定预算，新增未执行重置候选计数。主程序构建、105 项 CPU 调度检查和 RTX5070 的 1080p NR+DLSS 2X/4X/6X 恢复检查通过，D3D12 错误为 0。用户关闭程序后，实卡 1440p60 NV12、NR＋DLSS SR 到 4K＋6X 各 120 秒对照：提交率 202.57→204.10/s，预热调用减少但主要吞吐限制未解决；关闭测试预算拒绝反而降到 184.76/s，采集丢帧及生成过期增加。未达到稳定 6X，不能宣称已修好。未覆盖用户便携包、未发布。见 [恢复修复记录](DLSS_RECOVERY_REPAIR_2026-09-20.md)。

最新切换修复（09-20 最终回归）：`codex/fg-backend-switch-20260919` 修正 XeSS 旧倍率缓存、UI 请求拒绝、失败回退及异步准备帧的 XeLL 标记周期。RTX5070 引擎/真实下拉切换、失败注入、拖动/重置/缩放回归通过。YUY2 50fps 合成输入 2X/4X 有生成输出，1:1 单像素测试未发现额外模糊；用户所述观感及与 PotPlayer 的差异仍缺同条件对照，不能宣称解决。详见 [切换修复记录](FG_BACKEND_SWITCH_REPAIR_2026-09-19.md)。仅本地编译，未更新用户便携包或发布。

最新补充修复：`codex/seven-audit-20260919` 基于 `80f7dc4` 完成七项独立审查修复，涵盖压缩采集参考链/帧身份/PTS、FFmpeg EAGAIN 重送、D3D11 引用释放及字幕布局/缓存；同时修正构建依赖编码和字幕设置编译类型问题。干净构建、H.264/HEVC 软件及硬解、4K 文件、字幕像素、时序回归和主程序启动检查通过。实际采集卡队列与 GC573/GC551 未验收，详见 [七项修复报告](SEVEN_AUDIT_REPAIR_2026-09-19.md)。此前 DLSS/XeSS 调度修复保留；本轮仅本地，未打包或发布。

最新本地修复：`codex/fg-scheduling-repair-20260919` 从存档 `5a1931b` 修复 DLSS 暖机预算污染、CPU Present 重复计费、窗口采集期限抖动，以及 XeSS 实时呈现前的额外相位等待。构建和本机持续生成/拥堵恢复测试结果见 [补帧调度验收](FG_SCHEDULING_ACCEPTANCE_2026-09-19.md)。GC573 53fps 只新增定位诊断，尚无实卡根因与修复证明；5080 功耗、30/40 系和物理显示验收未覆盖。本轮未打包、合并 main、推送或发布。

**1.4.2 已正式发布并设为最新版**：[GitHub Release](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.2)，标签提交274d826。正式包、对应源码和SHA256共四资产已核对远端digest；软件短测、HDR组合与便携包七组检查通过。具体边界与发布后证据见WORKLOG。以下为历史开发记录。

当前任务：用户已授权正式发布 **1.4.2**，整合 RTX Video HDR、帧同步及后续采集/字幕/UI 修复，并修复全屏操作提示残留。新群二维码与用户 HDR 对比照片纳入 README/Release，赞助图保留。正式发布进度、验收与资产以 [1.4.2 发布记录](RELEASE_1.4.2_EXECUTION.md) 为准，下方“仅本地”“未合并”等均是先前阶段记录。

最新本地交付准备：`codex/screen-capture-20260919` 汇总此前隔离修复，构建显示版本 1.4.2，新增设备帧率手动协商、恢复专业模式 PS5 入口、修复设置滚动及选框重绘。下方“未打包”等描述为各阶段当时状态，本轮以 [1.4.2 验收记录](UI_CAPTURE_RATE_1.4.2_2026-09-19.md) 和 WORKLOG 为准。仅本地测试包，无 main 合并、推送或 GitHub Release。

## 已发布 1.4.1

- RTX 30/40 DLSS 开启、6X 解锁及 3060 初始化/执行卡死修复；用户已反馈测试可用，不代表所有型号和驱动均覆盖。
- 修复持续播放后的补帧受限、源帧与生成帧调度、采集瞬时停顿；软件 FPS 是提交读数，不是屏幕物理刷新率。
- MKV 字幕与音轨选择、打开/拖动、设置保存、全屏播放阻止休眠等修复已发布。
- 导出保留实际编码/封装错误处理，已取消资格门禁与结束逐帧扫描。
- 原生 HDR 输入保留、HDR 基底增强和 HEVC Main10 导出已存在；这不等于 SDR 转 RTX Video HDR。
- Dolby/DTS 采集可解码为 PCM；不等于压缩码流直通或 Atmos 对象渲染。圆刚专项探索已收尾；独立诊断工具留在存档分支。
- Smooth Motion 由 NVIDIA App 管理，用户确认本机可用。Veyra 不修改驱动配置，不把驱动生成帧计入导出。

发布证据见 [1.4.1 执行记录](RELEASE_1.4.1_EXECUTION.md)、[更新说明](RELEASE_NOTES_1.4.1.md)。

## 本轮开发

新增本地修复（2026-09-19）：[5060、命名调色预设与图形字幕报告](5060_PRESETS_BITMAP_PLAN_2026-09-18.md)。从 `050811b` 存档后在 `codex/5060-presets-subtitles-20260918` 隔离完成预设按钮重叠/窄窗口布局、命名保存反馈、PGS/DVD/DVB图形字幕显示及状态误分类修正；CPU、实际UI、5070 DLSS2/4/6X和VC-007PRO NR+DLSS4X短测通过。5060日志实际效果全关且输入时间线反复重置，新增原因诊断，**尚未根治或通过该用户实卡验收**。保留可运行构建 `E:/项目/Veyra/build/frame-pacing-20260918/veyra.exe`，未打新包、合并main、推送或发布。

VC-007PRO 1440p60 / 4K30、NR实时+DLSS4X 的同构建对照已完成，各120秒中位软件延迟25.36/40.09ms。追加20秒逐帧短测：原帧观察就绪后等待呈现15.92/29.65ms；2K60自动/最小/驱动默认均实际分配10个allocator缓冲，没有测得缓冲选项带来的有效差异。详见[1440p60与设备缓冲实测](CAPTURE_1440P60_COMPARISON_2026-09-18.md)，非HDMI到屏幕端延迟，未修改产品调度。

帧同步已在隔离分支 `codex/frame-pacing-20260918` 实现并完成本机短测，基线存档 `6e69eeb`。入口：专业模式 → 运动 → 帧同步，默认关闭；低排队、均匀呈现、Reflex 实验和独立显示同步偏好可保存。无补帧文件测试减少了一帧提前缓存驻留；6X 提交吞吐保持 144fps，未证明显著降延迟或更平滑。Reflex 无补帧时真实调用 NVAPI，开启补帧时明确回退低排队并保留倍率；XeSS 继续由提供方调度。VC-007PRO 4K30 NV12、NR+DLSS4X 已完成四模式各 120 秒实卡测试，详见[采集阶段计时](CAPTURE_LATENCY_DLSS4X_2026-09-18.md)。屏幕端到端延迟、30/40 实卡、PS5、VRR 未验证，完整原计划尚未全部验收。其他验收见[帧同步报告](FRAME_PACING_ACCEPTANCE_2026-09-18.md)。未合并 main、推送、发布或更新现有 1.4.2beta 包。

采集呈现相位优化及原始 1.4.2beta 引擎同参数对照见[延迟优化与历史对比](CAPTURE_LATENCY_REDUCTION_2026-09-18.md)。保持 4K30 NV12、NR、DLSS4X，不使用换格式或降低倍率解释收益。

DLSS / XeSS 双侧长帧调查及后续修复见[调查](FRAME_STALL_INVESTIGATION_2026-09-18.md)与[修复实测](FRAME_STALL_REPAIR_2026-09-18.md)。XeSS 改为保留呈现缓冲，三次缩放从丢 9 帧降到 0，代价是小窗口原生 NR 测得约多 2–4ms；另修复过期生成帧先等 GPU、阻挡就绪原帧的问题。DLSS4X 与 XeSS2X 后续各 120 秒均零采集丢帧，DLSS 注入阻塞恢复及 2X/6X/4X 切换通过。不代表全部卡顿已修好：两后端均抓到第 304 帧 NR Evaluate 约 49ms；另一次 DLSS CPU 尾部约 388ms、粉丝 HDR 正常播放约 1.4 秒呈现阻塞仍未根治。保留细分诊断，不更新既有 beta 包。

RTX Video HDR 独立改动保留。用户已明确终止 NVIDIA FSR 4.1 实验并要求回退，不再执行此前 FSR4 施工和画质修复计划。

**最新决定：FSR4 画质不接受，已撤掉实验入口和接入。** 原有 DLSS SR、RTX Video SR、官方 AMD FSR 保留；旧实验预设 mode 6 读取时转为 RTX Video SR 高档，保留其他参数。HDR 在 SDR 显示器预览不执行转换，保留开关旁的真实状态提示。回退构建与检查结果见 WORKLOG。

- 存档提交 `0e3d4ac`，标签 `checkpoint/pre-video-hdr-fsr41-20260918`。
- 隔离分支 `codex/video-hdr-20260918`；工作区 `E:/项目/Veyra/worktrees/video-hdr-20260918`。
- RTX Video HDR 已接入共享图、设置和 Main10 导出；RTX 5070 / 616.56 上 Create/Evaluate、2X/4X/6X、NR+SR+6X、原生 HDR 回归、HDR 导出与缺库 SDR 回退通过。显示器 HDR 当前未开启，实际 HDR 显示、采集卡、PS5 与 RTX 30/40 未执行。
- HDR 里程碑提交 `3477ed2`；后续隔离分支 `codex/fsr41-nvidia-20260918`，同名外部工作区。
- NVIDIA FSR 4.1.1 INT8 的 UI、环境变量接入、后端适配、专用测试及构建/打包脚本已回退到 HDR 里程碑前的 FSR 实现。旧日志和外部研究产物只作失败实验记录，不作为当前可用功能。
- HDR 参数页在 1280x800 窗口已目视检查，滑块数值即时更新。全部测试、限制及本地包说明见 [本地测试记录](LOCAL_HDR_FSR41_TEST_2026-09-18.md)。
- 用户授权本地 `1.4.2beta` 内测群包，新增已核验的官方 TrueHDR 原件；未授权 GitHub push/Release 或 Agent 代发。便携包路径、哈希及解压后实测见 WORKLOG。实际 HDR 屏幕和 RTX30/40 HDR 仍待内测。

所有新产物放 `E:/项目/Veyra/`，源码/文档留在隔离工作区；逐项结果以 [WORKLOG](WORKLOG.md) 为准。
