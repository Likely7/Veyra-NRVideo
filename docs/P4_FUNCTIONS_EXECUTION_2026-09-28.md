# Veyra 2.0.0 P4 功能闭环施工记录

## 合同与边界

范围以 `UI_V2_0_0_MASTER_PLAN_2026-09-27.md` 的 P4-a 至 P4-g 为准。隔离区 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927`，分支 `codex/p4-functions-20260928`，HEAD 起点 `53a2d31`。开工归档 `E:/项目/Veyra/archives/p4-start-20260928/` 保存 104 个脏/未跟踪文件及 SHA-256 清单；不覆盖原桌面 checkout 或 P3 候选。

桌面原区 `ui-migration-scope-guard.ps1` 开工通过，证据 `E:/项目/Veyra/logs/p4-functions-20260928/preflight-origin-scope.json`。P4-a 基线为 `E:/项目/Veyra/archives/p4-start-20260928/p4a-scope-baseline.json`，SHA256 `5a4e63bdbdaa775db20912becb231a73324a644c109c38413e75967ce52501f7`；保护原区 1457、隔离区 1066 文件。旧 P3 基线及其哈希不修改。新增切片范围前，先存档当前源码并创建新的不可变基线，不能把扩大白名单写回旧基线。

构建/测试/日志/临时目录统一放 `E:/项目/Veyra/{build,tests,logs,tmp,archives}/p4-*`，不在源码区或桌面创建产物。单次测试不超过 300 秒，构建不超过 900 秒。失败保留输出，同一原因最多做一次诊断与两次有据修复。每片记录实际运行与未运行项；不自动 commit、tag、merge、push、Release 或删除旧 UI。

## 顺序与验收

1. **P4-a 预设与状态**：已有 a34 保存 UI 和 a35 管理 UI 属局部实现。启动时有显式默认预设则按其模式与内容覆盖恢复会话；未选内容及另一模式的会话保留。默认无效或引擎拒绝时恢复会话保持原样并提示。验证 v1-v21、损坏/空/超限文件、重名/容量、部分应用、内置只读、列表/节点隔离、管理弹窗重启、最终 QML 实跑和原文件保全；存档。
2. **P4-b 列表导出**：单项/批量/图片入口只接受列表配置与列表预设，启动即冻结作业配置；节点模式和节点预设全入口拒绝。尺寸对齐与能力逐项校验、trim/音轨、顺序队列、失败/取消/暂停恢复/ETA及 NVENC/MF 能力内模式，检查解码成品而非仅状态；存档。
3. **P4-c 字幕**：复用既有渲染器，验证轨道/外挂、样式四档描边、偏移位置、双行、全屏/DPI/视频覆盖及高级项；存档。
4. **P4-d 音频**：真实设备 ID、切换与拔插错误、保存恢复、音轨/下混/布局及偏移口径；不改时钟或丢 PCM；存档。
5. **P4-e 四片源**：文件/图片、采集、PS5、屏幕各自真实打开及会话恢复，设备枚举与配置隔离，屏幕缩略图/裁剪/指针按实际接口验证；存档。
6. **P4-f 设置与诊断**：偏好持久化、快捷键、日志/组件/链接/更新与支持入口逐项实测；存档。
7. **P4-g 调色补缺**：保留已有七组、曲线/混色器/色轮与局部证据；补 LUT 文件导入和独立颜色预设，核实列表/节点实例、回滚、重启、真实画面。P5 才处理外观动效；存档。

P4 整体只在上述七片均有对应功能、运行和持久化证据后标完成。缺硬件或未测路径逐项列明，不以历史候选或已有静态 UI 替代。

## P4-a 预设与状态：已完成本片验证并封存

- 实施：默认预设在启动恢复后按保存模式应用；未指定默认时保留恢复会话。默认应用失败不覆盖恢复会话。管理弹窗按列表/节点筛选，内置项只读，改名/复制/删除/默认设置通过同一库接口。`PresetStore` 默认项清除和替换在磁盘写失败时同时回滚内存及原文件。
- 受控日志 `E:/项目/Veyra/logs/p4-a-20260928/commands.json` 共 16 项；每项有命令、退出码、前后范围 guard 和 stdout/stderr。`004`–`008` 的 QML 分进程验证默认设置、重启应用、清除及无默认重启；`010` 的管理弹窗两进程验证筛选、内置限制、改名冲突、复制删除、默认项重启持久化。最终 staging `E:/项目/Veyra/tests/p4-a-20260928/staged-001/` 的 `Main.qml` 与源码一致，测试定时注入已移除。
- `009` 预设库单测验证部分应用、模式隔离、内置只读、重名、64 项容量、损坏及超限原件保全、默认项写失败回滚。`016` 修正测试夹具后验证 v1–v21 逐版读取时原文件不变、显式保存升级可再读，以及旧后端迁移、退休 SR 模式和六组调色/NR v23 回归。`011`–`016` 保留两次失败及修复记录：旧测试路径残留污染容量用例；测试读取流未关闭阻止 Windows 文件替换；v18 夹具错写 v19 mask。最终测试路径 `repair-presets-r3.v1` 与旧路径隔离。
- 音频偏移不属于 `ChainSession` 的列表/节点模式状态。默认预设重启后的 17 ms 来自该预设重新应用；清除默认后的重启回到 0 ms。音频设备与偏移持久化仍列 P4-d，不能凭 P4-a 结果放行。
- 本片只证明软件与 QML 自动交互路径；未做人工视觉设计验收、实卡采集或全 P4 验收。P4-b 至 P4-g 未开工。
- 存档 `E:/项目/Veyra/archives/p4-a-20260928/checkpoint.json` 已回读校验：相对开工基线变更 9 个文件、证据 59 个文件；checkpoint SHA-256 `87ff4b399e8ef6c3a106daa6c15908535cc9159b3b02cb3298ad6d96a716badb`。第一次用 Windows PowerShell 5 执行归档脚本时 UTF-8 路径被错误解码，未生成目标目录；随后用 PowerShell 7 执行成功。候选 EXE 只记录身份，没有复制到源码 Git。

## P4-b 列表导出：生产路径验收与封存前结论

- 生产探针为 `E:/项目/Veyra/build/p4-b-20260928/build-001/veyra_export_probe.exe`，运行时 DLL 仅放在 `E:/项目/Veyra/tests/p4-b-20260928/staged-001/`；输入副本为 `E:/项目/Veyra/tests/p4-b-20260928/input.mp4`。原始输入由 `ffprobe` 独立确认：1920x1080、24 fps、12.000 s、H.264 视频、48 kHz/6 声道 AAC。
- CQ、CBR 8 Mbps、VBR 8 Mbps 各导出 30 源帧，生产进程均 exit 0；日志分别为 `cq-30.log`、`cbr-30.log`、`vbr-30.log`。三次均使用 NVIDIA NVENC H.264 (D3D12)，编码日志记录 `source=30 generated=0 hold=0 output=30`。`ffprobe` 实际解码容器：三份文件均有 30 视频帧、约 1.25 s 视频和 6 声道 AAC；CQ 文件约 493 KiB，CBR/VBR 文件约 1.30 MiB，码控选项确实进入编码器。
- trim `2.0..5.0 s` 生产导出 exit 0，`trim-2-5.mp4` 的 `ffprobe` 结果为 73 视频帧、约 3.0417 s 视频、3.056 s 容器、6 声道 AAC（日志 `trim-2-5.log`）。非法 trim `5..2 s` exit 1，且没有 final 或 `.partial`；非法音轨索引 0（输入只有音轨 1）同样 exit 1，无输出。错误原始日志保留在 `invalid-trim.log` 与 `audio-0-invalid.log`。
- 队列顺序的可验证下限已完成：连续提交同一输入的 8 帧和 6 帧两个任务，两个任务分别 exit 0；`queue-01.mp4` 为 8 视频帧/16 AAC 帧，`queue-02.mp4` 为 6 视频帧/12 AAC 帧，文件和日志保留。该证据证明单任务完成顺序与成品可读性，不证明 QML 多文件队列在真实 UI 进程中的并行/失败后继续策略。
- 首次 `veyra_media_probe.exe` 成品读取尝试在绝对路径上返回 FFmpeg `AVERROR(ENOENT)`；源码中 `std::string` 逐字节赋给 `std::wstring`，中文路径会损坏。该探针缺陷单独记录，未用它判定成品失败；成品结构和帧数改由系统 `ffprobe` 独立读取。没有改冻结的媒体/解码代码来掩盖这个缺陷。
- 追加真实 QML 进程验收：测试副本 `E:/项目/Veyra/tests/p4-b-20260928/qml-queue-stage/qml/Veyra/Main.qml` 注入一次性 Timer，产品源码 Main.qml 未注入。`qml-probe-app.log` 记录 worker 的 pause-request、resume-request、cancel-request 和最终“已取消”；仅留下 `.partial`，没有 final。该测试证明暂停/恢复/取消指令进入真实进程，不证明暂停期间的 ETA 精度或文件残留策略已完整验收。
- 首次三项队列 QML 实跑暴露 bridge 定时器在首项完成后不再 `pollExport()` 的缺陷：首项成品已生成，但 `exportQueueCount` 持续为 2，后续任务未启动（`qml-queue-app.log`）。仅修改允许的 `src/ui/QmlPlayerBridge.cpp`，当快照 active **或** `queued > 0` 时继续轮询。MSVC 增量构建 `veyra_qml_ui` 通过，记录为 `qml-queue-build-vs.log`；首次直接调用 cmake 因未装载 VS 开发环境缺少 `<type_traits>`，原始失败保留 `qml-queue-build.log`。
- 修复后的 QML 三项队列进程 exit 0，`qml-queue-r3-stdout.log` 显示首项 worker 完成、第三项 worker 启动、最终 `queued=0` 且状态“已完成”。`qml-queue-first-r3.mp4` 与 `qml-queue-third-r3.mp4` 各由独立 `ffprobe -count_frames` 确认 288 帧 H.264、563 帧 AAC、12.000 秒。预存的 `cq-30.mp4` 未被覆盖。测试中间一次 r2 通过 PowerShell 直接调用 GUI EXE，调用立即返回且无 stdout，仅首项成品出现；它不能作为队列成功证据。
- **冲突项没有被实际判为失败。** 冻结的 `ExportJobManager::start()` 在检查输出存在前递归调用 `poll()`；当队列还有第三项时，该递归会先启动第三项，冲突项被静默跳过。当前只证明首项与第三项顺序导出、队列最终归零及已有文件未覆盖，不能宣称失败提示/失败后继续策略通过。Media Foundation 编码器成品、图片列表导出、任意自定义宽高仍未验收或需要冻结合同。该管理器缺陷按 UI-only 边界记录，不能在本片修改。
- 当前导出入口和单项设置校验、节点模式/节点预设拒绝、启动冻结设置、输入存在性、当前文件 trim/音轨复制、后续队列默认全片/默认音轨、CQ/CBR/VBR、进度/暂停/取消/ETA 文案及构建已完成；真实进程队列和暂停/取消已补局部证据。P4-b 仍因上述管理器缺陷、MF、图片列表及其他未测边界保持 partial。
- 已建立未完成状态的增量存档 `E:/项目/Veyra/archives/p4-b-20260928/checkpoint.json`，回读并重新核对源码与证据哈希。该 checkpoint 明确标记 `status=partial`，保存源码与本轮证据哈希；不能当作 P4-b 完成凭证。
- 队列修复续验单独存档于 `E:/项目/Veyra/archives/p4-b-queue-retest-20260928/checkpoint.json`，保留本片源码/文档副本、QML 进程与成品哈希；状态同为 `partial`，不覆盖原 P4-b checkpoint。

## P4-c 字幕：页面可启动，桥接接口缺失（partial）

- QML 入口和回归范围已运行：`E:/项目/Veyra/logs/p4-c-g-20260928/qml-smoke/run-20260928T085957721Z-3265dba0/result.json` 的 home、minimal、professional、node、export、settings、capture dialog、playback-professional 八项均 `exit=0`；`dialog-matrix/result.json` 的 subtitle 对话框也能独立启动。
- 静态接口审计确认 `include/veyra/ui/QmlPlayerBridge.h` 的字幕段落明确没有任何轨道、外挂文件、样式、偏移或位置属性；源码注释说明 `SubtitleStyle` 仍由旧 Win32 shell 持有。`Main.qml` 快捷键、`CineBar.qml` 字幕菜单和 `DialogHost.qml` 字幕设置因此只显示“未接入/禁用”状态。
- 已实现/已验证：QML 对话框宿主能加载，播放页和全屏壳能启动，未伪造一份与旧渲染器不同步的字幕状态。
- 未验证：内嵌轨道选择、外挂字幕文件、四档描边、字号/颜色/背景、偏移与位置、双行不缩字、全屏/DPI/视频覆盖及高级字幕原型。当前缺口不是 QML 排版问题，而是冻结后端没有可消费的 bridge 合同。
- 结论：P4-c 只能记为 `partial`。按当前 2.0.0 规则，禁止借 UI 迁移修改旧 Win32 或字幕/媒体链路；需另行授权并提供共享字幕接口后才能继续。

## P4-d 音频：音轨和偏移已暴露，输出设备闭环未验（partial）

- `QmlPlayerBridge` 已提供 `audioTracks`、`selectedAudioTrack` 和 `audioOffsetMs`；`ProPage.qml` 能显示音轨列表、声道数和手动偏移，`DialogHost.qml` 能显示音量、静音及偏移控制。
- QML smoke 的 professional/export/settings 入口和 `dialog-matrix/result.json` 的 audio 对话框均启动通过。组件测试还覆盖了滑条外部绑定、拒绝写回不伪显示为成功以及单项复位，不代表音频设备真的切换。
- `DialogHost.qml` 当前明确写出“输出设备选择与立体声下混尚未接入：当前跟随系统默认设备”。因此本片没有把 `audioOffsetMs` 的 UI 持久化或一条可写属性当成设备能力。
- 未验证：真实 WASAPI 设备 ID 选择、设备拔插错误、重启恢复、输出布局、立体声下混、音频时钟连续性、PCM 是否丢失，以及测得处理延迟与用户偏移的分离口径。
- 结论：P4-d 为 `partial`；不改音频服务、时钟或 PCM 链路。需要真实设备和现有音频接口的独立验收证据。

## P4-e 四片源：四类 QML 入口通过，硬件和高级屏幕选项未验（partial）

- 文件/图片入口由 home、minimal、professional、export smoke 覆盖；采集卡、PS5、屏幕捕获对话框在 `qml-smoke/result.json` 和 `dialog-matrix/result.json` 中均能启动。
- 当前 bridge 已暴露采集设备列表与稳定 ID（`captureDevices`、`captureDeviceId`、`captureDeviceLabel`、`captureForceSdr`、`captureFlipVertical`），屏幕目标列表与 ID（`screenTargets`、`screenTargetId`、`screenTargetLabel`、`refreshCaptureTargets()`），以及 PS5 主机和 PIN 字段（`remotePlayHost`、`remotePlayPin`）。这些字段足以证明 QML 能消费既有枚举/配置边界，但不证明设备工作。
- 已验证：入口加载、配置控件绑定不崩溃、测试数据目录隔离；未验证：真实文件和图片会话恢复、采集卡实际枚举/格式打开/上次会话恢复、PS5 配对与凭据恢复、屏幕缩略图/预览/裁剪/指针及跨屏 DPI。
- 静态审计还确认屏幕高级选项没有完整 bridge 合同，采集和 PS5 的真实设备路径没有本轮可复现日志。不能把对话框能打开写成四片源完成。
- 结论：P4-e 为 `partial`，硬件验收和缺失接口必须单独补证据；不修改 source/capture/remote-play 后端。

## P4-f 设置与诊断：偏好迁移和页面启动通过，逐项产品入口未验（partial）

- `settings` QML smoke 通过，`veyra_qml_data_tests` 补充不存在 scratch 目录后输出 `PASS qml data migration`；这证明测试目录中的 `ui-session.v1` 迁移和默认数据路径可运行。
- bridge 已有默认页、减少动效、最近文件、组件列表、版本和状态等 QML 可消费字段；设置页、组件/状态展示和相关弹窗能在隔离数据目录启动，日志没有未处理的 QML 异常。
- 已验证：页面和偏好迁移的自动路径；未验证：真实日志目录打开、组件详情与许可证链接、更新检查、反馈/支持入口、快捷键全量持久化、截图目录选择、语言/缩放/主题等每项重启恢复。
- 结论：P4-f 为 `partial`。本片没有把静态链接、文案或一个迁移测试扩大成完整设置/诊断验收。

## P4-g 调色补缺：已有控件和定向交互通过，LUT/独立颜色预设仍缺（partial）

- 调色并非从零开始：`ColourPanel.qml`、`ColourCurve.qml`、`ColourMixer.qml`、`ColourWheel.qml` 和 `NrLayerEditor.qml` 已存在，bridge 提供 `colourParameters`、`colourGroups`、`selectedColourLayer` 与 `colourState`。本片保留已有七组、组旁路/还原、单滑条复位和原图入口语义。
- `E:/项目/Veyra/logs/p4-c-g-20260928/qml-tests/veyra_qml_quick_tests.exe.stdout.log` 显示 21 passed、0 failed：曲线节点增删/拖动/右键删除、混色器单色带隔离和黑白模式、四区色轮操作/复位、NR 四层独立卡片及完整参数范围、菜单/滑条回归均通过。easing 测试也为 `failures=0`。
- 已验证：QML 控件真实鼠标事件改变模型值，实例之间不串改，滑条被后端拒绝时不伪造成功，调色控件在列表/节点页可加载。上述是 UI/bridge 级证据，不是完整真实画面或导出像素验收。
- 缺口：当前没有 LUT 文件选择/导入 bridge；现有 `savePresetAs(..., mask)` 的 `color` 位是整链预设的一部分，不能冒充独立颜色预设。独立颜色预设的创建、应用、保存恢复、错误回滚和重启隔离均未取得证据。
- 未验证：列表与节点多实例的真实 GPU 画面、LUT 失效报错、删除/回滚、重开后状态、图片/视频导出颜色结果；P5 的外观动效也不计入本片。
- 结论：P4-g 为 `partial`。按当前范围只能记录接口缺口，不能为了“完成”在 QML 之外补造 LUT/颜色存储协议。

## P4 汇总与封存状态

| 分片 | 状态 | 本轮可确认事实 | 主要阻塞 |
| --- | --- | --- | --- |
| P4-a | completed | 预设与状态已实测并封存 | 无本片阻塞 |
| P4-b | partial | NVENC CQ/CBR/VBR、trim、非法输入、QML 暂停/取消与首项/第三项队列实跑、成品 ffprobe | 冲突项静默跳过、MF、图片列表、自定义尺寸和 ETA/残留细节等 |
| P4-c | partial | 字幕对话框/播放入口可启动 | QmlPlayerBridge 没有字幕接口 |
| P4-d | partial | 音轨、偏移和音频 UI 可加载 | 输出设备、下混、真实设备与持久化未验 |
| P4-e | partial | 文件/图片、采集、PS5、屏幕入口可启动 | 硬件、屏幕高级选项、会话恢复未验 |
| P4-f | partial | 设置页和数据迁移自动路径通过 | 日志/更新/支持/快捷键等逐项未验 |
| P4-g | partial | 调色控件定向 Quick Test 21/21 通过 | LUT 导入、独立颜色预设、真实画面未验 |

P4 整体保持 `blocked-by-evidence`，不能标记为完成。这里的 blocked 是交付事实，不是允许修改冻结链路的理由：需要补证据或另行授权接口后才能关闭对应分片。各分片 checkpoint 只记录当前源码和证据哈希，状态为 `partial`，不能当作完成凭证。

## 2026-09-28 续验：QML staging 与完整 smoke

- canonical checkout 的 `ui-migration-scope-guard.ps1` 续验通过：`E:/项目/Veyra/logs/p4-continuation-20260928/scope-before-r3.json`，`head=53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`、`frozenChecked=474`、`failures=[]`。
- 旧 `p4-a-20260928/staged-001` 与当前 build 哈希不一致，未复用；按既有 staging 合同新建 `E:/项目/Veyra/tests/p4-continuation-20260928/staged-r3`。`veyra_qml_ui.exe` build/staging SHA-256 均为 `E389F958522AB45A9754C0352CEDD27313B65A733624F4E5CCE85CD026152A8B`；三个 QML-only 测试也逐一核对一致：data `C04EDEB3B5D0B6EC6D2EAE0A6EAFA85EFA2F0B0ADAE92B6FBAB3866FEC2789B6`、easing `4071AF02CEAE321674B5F8C171C3140E9C1DD2F3921AD852E636E057CE36CA23`、quick `DF2509001FEEF526E7082BA1B806A5E5BDAE4AA23C223CC3A09A965F00E5C022`。
- `scripts/run-unit-ui-migration.ps1` 续验通过，证据 `E:/项目/Veyra/logs/p4-continuation-20260928/qml-unit-r3/summary.json`：data/easing/quick 三项均 `exit=0`，`failures=0`。
- `scripts/acceptance/qml-ui-smoke.ps1` 首次未传 fixture 时按合同将 playback 标为 skipped 并退出 1；该结果没有被当作通过。随后使用既有 `test_av_1080p.mp4` fixture 重跑，完整八项（home、minimal、professional、node、export、settings、dialog-capture、playback-professional）均 `exit=0`，证据 `E:/项目/Veyra/logs/p4-continuation-20260928/qml-smoke-r4/run-20260928T101254254Z-69334c50/result.json`，`failures=0`、`skipped=0`。
- 续验只证明当前 QML staging、页面入口、播放入口和 QML-only 自动测试可运行；不改变 P4-b 的冲突项静默跳过、MF/图片列表/任意尺寸等开放项，也不改变 P4-c 至 P4-g 的字幕、设备、硬件、诊断、LUT/独立颜色预设缺口。P4-a 仍 `completed`，P4-b 至 P4-g 仍 `partial`，P4 整体仍不得标完成。
- 本轮增量存档：`E:/项目/Veyra/archives/p4-continuation-20260928/checkpoint.json`，状态 `partial`；保留 staging、测试输出、哈希和上述 scope guard 结果，不提交、打 tag、合并、推送或发布。

## 2026-09-28 续推（Claude Code 接续）：按P4授权补接点

**纠偏。** 上文P4-b至P4-g“需另行授权”的结论与本分支AGENTS顶部P4授权（“P4所需既有服务/bridge接点获本次功能范围授权”）不符：字幕、LUT、颜色预设、导出队列都有现成服务，缺的只是接点。用户本轮要求“把p4推完”，按原授权继续，不扩展到P5外观/P6性能或算法/时钟重写。

**范围基线。** `E:/项目/Veyra/archives/p4-rest-20260928/scope-baseline.json`：P4-a基线文件哈希原样继承，仅追加下列allow；P4-a基线不改。

**逐片计划。**
1. P4-g：bridge接`ColorLutStore`（列出/导入.cube/选择/清除，错误回显）和`ColorLookStore`（独立颜色预设：保存当前调色层、应用到当前层、删除、导入/导出.vpcolor）；ColourPanel LUT组与“颜色预设”行。验：导入真实.cube后画面像素变化、错误文件拒绝且原设置不变、预设保存→重启→应用恢复、列表与节点两个调色实例分别应用。
2. P4-c：bridge持有`SubtitleLoader`，主/副轨、外挂文件、偏移、自动对齐；样式（字号、字体、描边四档、背景条、底部距离、双语、按行缩放）持久化到`<data>/qml-preferences.v1.json`；渲染复用旧壳`SubtitleOverlay`，作为视频原生窗口的子窗口（随视频区域裁剪，不压住QML弹层）。验：外挂SRT与内嵌轨切换、叠层像素出现、偏移生效、重启样式恢复、全屏。
3. P4-d：`WasapiAudioSink`加进程级首选端点ID（空=系统默认）、端点枚举、当前端点/回落状态、强制立体声下混；首选端点消失时回落默认并提示，重现后切回。bridge设备列表/选择/持久化。验：真实端点切换日志与格式、无效ID回落、重启恢复、下混布局日志。
4. P4-b：修`ExportJobManager::start()`递归`poll()`导致冲突项被跳过；冲突/失败项在队列中明确报失败并继续下一项。验：三项队列中间项冲突时报错、第三项完成且成品可解码；MF编码器路径实测（若可强制）。
5. P4-f：设置页日志目录/截图目录/项目主页/诊断复制/组件列表等入口接实，快捷键表；偏好重启恢复。
6. P4-e：文件/图片会话恢复、屏幕采集目标刷新与打开实测；采集卡与PS5以本机实有硬件为限，缺硬件项列未验。

每片完成后单独存档`E:/项目/Veyra/archives/p4-rest-20260928/<slice>`，记录候选EXE哈希与测试日志。

## 2026-09-28 续推结果（Claude Code）

详细证据与失败记录见 WORKLOG 同日“P4续推”节；受控日志 `E:/项目/Veyra/logs/p4-rest-20260928/commands.json`，存档 `E:/项目/Veyra/archives/p4-rest-20260928/p4-final`。

| 分片 | 状态 | 本轮实测（同一最终候选 candidate-p4） | 仍未验/未做 |
| --- | --- | --- | --- |
| P4-a | completed | （Codex已完成；本轮回归未退化） | — |
| P4-b | 功能完成，1项待决定 | 队列冲突明确失败并继续、成品解码、MF CQ/CBR/VBR、图片单张/批量、节点拒绝 | 任意自定义宽高需新增导出缩放步骤（管线能力），待用户决定 |
| P4-c | completed | 内嵌/外挂/主副轨/偏移/自动对齐入口、样式持久化、真实叠层像素、全屏避让、B/Z/X/T/Y | 自动对齐算法本轮未跑长片（只验入口） |
| P4-d | 软件路径completed | 真机端点切换续播、未知ID回落、5.1→2下混、重启保持 | 多声道设备上强制立体声差异、物理拔插事件 |
| P4-e | 文件/图片/屏幕/采集completed；PS5移植完成未实机 | 显示器/窗口捕获+裁剪、KUHAIMI采集卡1080p120连接与继续上次（含重启）、PS5读已保存配对 | PS5连接/配对/手柄转发（需用户PS5在场）；屏幕目标缩略图未做 |
| P4-f | completed | 全部设置项持久化、真实按键V/Tab/重绑Ctrl+P、125%缩放重启、软解、续播 | 界面语言仅简体中文（如实显示） |
| P4-g | completed | LUT导入/选择/清除像素、独立颜色预设保存/应用/重启、坏文件拒绝、实例隔离 | — |

**试用后修复（同日）：** 首页背景发白（P4-f背景强度引入，`VBackdrop.fillItem`不能是非纹理项）已修并三档像素验证；全页面18屏巡检另修两处既有缺陷（`onPageChanged`读到旧`cinema`致极简页之后的页面被压矮；无副标题对话框关闭按钮错位）。修后单测/smoke/tier23/节点编辑/P3离线与实播回归/P4-c/e/f重跑全部通过。见 WORKLOG 同日“用户试用后修复”条与存档 `p4-final-r2`。
