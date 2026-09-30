# XeSS / FSR 3.1 / FSR 4 补帧执行方案（2026-09-30）

## 授权、基线和边界

用户本轮授权：“行，那就开始排查修复，以及增加FSR3 FSR4 写执行方案”。本轮开始实施，覆盖这些功能所需的 UI / bridge / settings / graph / FG / Present 接点的历史冻结。PS5 连接已由用户确认恢复，不列入本轮故障。

- 工作区：`E:/项目/Veyra/worktrees/p0-p1-r53-20260927`；分支：`codex/fg-fsr-xess-20260930`；Git 起点：`53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`。
- 用户已有修改逐文件保全：`E:/项目/Veyra/archives/fg-fsr-xess-20260930-start/source/`，169 个修改或未跟踪文件；另存原差异和状态。不复制 SDK / runtime / 构建工程。
- 不可变范围基线：同目录 `scope-baseline.json`，1091 个文件哈希，SHA256 `c871f25fe503e30aeae3f63234c920a7574edf65da8d1643ec29e94b173d8a11`。扩展范围另存基线，不改原哈希。
- 本轮不修改桌面旧 checkout、原 beta、main、旧 Win32 UI、NR/SR 算法、采集/解码/音频时钟。无 commit / tag / merge / push / Release / 代发消息授权，不派子 Agent。
- 每项测试最长 300 秒，构建最长 900 秒。同一失败最多两次有证据修复；20 分钟没有新证据停止该路径并留下状态。

## 已确认事实与未定问题

| 问题 | 当前证据 | 处理 |
| --- | --- | --- |
| QML 没有 FSR FG 入口 | bridge 只认 `dlss` / `xess`，列表和节点选择器也只有两项 | 加入独立 FSR 3.1 / FSR 4 项，设置和预设往返一致 |
| FSR 退出后要求重建播放器 | `PresentSink` 保留 FSR proxy，但 XeSS 分支又清空交换链并走创建路径；旧测试还有 FSR proxy 销毁后 HWND 无法重建的证据 | 优先验证 SDK 独立输出路径，FSR 不再接管 HWND；不建立副显示器、第二视频窗口或隐藏呈现窗口 |
| AMD 用户 XeSS 只能 2× | 缺显卡型号、客户端版本、失败形式和该用户日志；当前提供方身份及五处解锁偏移与既有移植一致 | 分离 UI 上限、模块身份、补丁事务、SDK Create / Init / SetNum、真实 SDK 生成报告；不先归咎 AMD |
| FSR 初始化日志可能报错版本 | 现代码把版本枚举首项当作已选版本，创建不固定算法版本 | 查询版本 ID，显式选择 3.1 或 4.0，创建后再查询实际 provider；不得把 3.1 fallback 标为 FSR 4 |
| FSR 4 输入合同缺口 | 现 prepare 早于 configure；相机基向量全零；transfer 写死 sRGB | 按 Configure → PrepareV2 → Generate；稳定虚拟相机基；SDR / scRGB / PQ 分别声明。视频估计 guidance 不冒充游戏原生输入 |

FSR 3.1 是符合 API / 硬件条件的跨厂商路径，首批 2×。FSR 4 ML FG 官方 Windows 支持 RX 9000；不能称所有 AMD 卡都支持。XeSS 非 Intel 官方为 2×，Veyra 3× / 4× 是固定身份的进程内实验解锁。NVIDIA FSR 4 ML FG 暂无已核实成熟 Windows 实现，不在本轮虚构支持。FSR 超分和 FSR 补帧分别统计。

## 实施顺序与门槛

1. **存档和故障基线。** 保存源码、分支、组件身份、原 beta 哈希；复核现有 FSR / XeSS 接点。建立本轮隔离日志和测试配置，不使用用户配置。
2. **独立 FSR GPU 输出验证。** 沿用 AMD SDK API，使用 `FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY`，调用方提供 command list 与输出纹理；强制 3.1 provider，以真实运动图和读回验证中间帧。读回只在测试。若接口失败，保留日志并重新评估具体替代接点，不以黑帧或返回码代替输出验收。
3. **接入共享处理图。** FSR 3.1 / 4 复用现有 A/B history、bounded FrameBatch、GPU fence、生成纹理 lease 和 PresentationScheduler。只生成一个中间帧；reset seed 不当有效生成帧；字幕/OSD 仍在 FG 之后。不新增播放循环、不逐 pass CPU 等 fence、不改音频或源 PTS。
4. **FSR 版本和入口。** 保留已序列化 enum 值，新增 FSR 4 请求；FSR 3.1 强制对应 provider ID，FSR 4 只在 provider 实际可用时启动。失败恢复前一套设置并明确原因，不静默声称 ML 生效。预设格式需能表达新增值，旧版本保持可读。
5. **XeSS 4× 排查修复。** 修补实际发现的上限/事务/初始化问题；日志包含显卡/驱动、请求倍率、provider 身份、unlock 与 pacing 安装状态、SDK 实际上限/状态。2× 不打补丁；3×/4× 不改磁盘 DLL。失败后保持播放并给出可诊断错误。
6. **真实切换和回归。** 相同进程和视频 HWND 连续切换关闭 / FSR 3.1 / XeSS 2× / XeSS 4× / DLSS 2×；记录身份、PTS、有效生成数、资源和失败码。覆盖暂停继续、seek、缩放/resize、节点/列表、预设保存恢复、运行库缺失/不支持回滚，以及 DLSS 既有行为。
7. **交付状态。** 报告代码、构建、实际测试和未验实卡；给出本轮候选路径与唯一剩余门槛，不改原 beta，不发布。

## 验收定义

- FSR 3.1：实际调用选定 provider，运动输出存在且不是黑图/复制原帧，GPU 完成后才标记有效；产生中间 PTS 并通过现有呈现路径提交。
- 热切换：同 PID / HWND 无重启、无额外显示器、无新隐藏视频窗口；原帧持续推进；失败请求原子恢复，不持续黑屏。
- XeSS：2× / 3× / 4× 可见并可提交；SDK 的生成报告与请求相符。AMD 用户特定失败只有在同型号或其日志有对应证据后才能称已定位。
- FSR 4：provider 选择和不支持的负例在本机验证；RX 9000 实际输出、颜色、性能和连续切换须实卡验收。缺卡不伪造通过，不把 RTX 5070 的 FSR 3.1 结果套给 ML FG。
- submitted FPS / provider report 只表示软件提交或 SDK 报告，不代表面板扫描帧数或屏幕端到端延迟。
- 结束时逐文件核对范围基线和原 beta 哈希；新 SDK / runtime / 模型不进入源码版本控制。

## 固定来源与许可证

- AMD FidelityFX SDK 2.3.0：`GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`，固定提交 `60f4ea81909200d8542eca14dccb2628b763a9a3`。重点接口：`ffx_framegeneration.h`；独立 configure 合同：`framegeneration/fsr3/internal/ffx_provider_fsr3framegeneration.cpp`。MIT / SDK 随附许可，保留 notices。复用 API 与现有适配器，不重写 AMD 算法。
- 既有 XeSS 解锁移植：`Coldwood1026/OptiScalerDp4aUnlock` 原固定提交 `70676c5f`（原 OptiScaler 名称），GPL-3.0；本轮比对上游 `9eea95bba9fda7121f214d2eba358423be598d7e` 的五处偏移。沿用固定运行库身份及事务回滚；修改逐项记 `THIRD_PARTY_NOTICES.md`。
- 版本选择辅助参考：OptiScaler `45a2001303ddff632e279f77aef85ceede5832cb`，GPL-3.0，`FSRFG_Dx12.cpp` 的版本 ID / Configure-before-Prepare API 使用。若复制具体源码必须追加归因。
- 官方文档：[AMD FSR SDK](https://gpuopen.com/amd-fsr-sdk/)、[FSR ML Frame Interpolation](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/60f4ea81909200d8542eca14dccb2628b763a9a3/Kits/FidelityFX/docs/techniques/frame-interpolation-ml.md)、[Intel XeSS FG](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md)。

## 本轮产物

统一前缀 `fg-fsr-xess-20260930`：`E:/项目/Veyra/build/`、`tests/`、`logs/`、`tmp/`、`archives/`。候选属于本地验证，不覆盖 `test-packages/2.0.0beta`。TEMP / TMP 只对本轮子进程配置。

## 执行记录

### 当前交付

2026-09-30：第 1–6 项的本机施工、构建及针对性验收已完成；第 7 项记录本轮候选。FSR 3.1 热切换修复已经真实运行。FSR 4 ML 的入口、设置、provider 选择及不支持回滚已实现，**RX 9000 实际生成未验**；AMD 反馈者的 XeSS 2× 限制未取得其型号/日志，不能称已经定位该用户的根因。

- 候选：`E:/项目/Veyra/tests/fg-fsr-xess-20260930/candidate/veyra_qml_ui.exe`，SHA256 `25439d59af47813b7fadad7b4e3b3c839acc7531414a8613d9d86f6730b7d8a8`。
- 构建：`E:/项目/Veyra/build/fg-fsr-xess-20260930`，VS 2022 / MSVC 14.44 / C++20 / Release / Qt 6.8.3；FFmpeg 沿用 `C:/veyra-deps/ffmpeg-ps5-dav1d-installed` 的 PS5 slice 补丁版本。
- 这是依赖本机批准运行库路径的测试 staging，含测试程序；不是新便携包，不覆盖 Claude 的 `2.0.0beta`。
- 最终增量源码及清单：`E:/项目/Veyra/archives/fg-fsr-xess-20260930-final/`；候选身份及保护项复核：`E:/项目/Veyra/logs/fg-fsr-xess-20260930/final-audit.json`。

### 实现与实际故障修复

1. `FsrFgPresenter` 改为独立 effect context，没有 HWND / swapchain context；共享图生成纹理，沿用 bounded FrameBatch / slot lease / GPU consumer fence / 呈现调度。删除 `PresentSink` 旧 FSR retained proxy 接点。同一视频窗口正常切换，不建立副显示器、第二视频窗口或隐藏视频窗口。
2. FSR 显式枚举并锁定算法 ID，创建后核对真实 provider；本机返回 `3.1.6` / `0xF600000000C01006`。FSR 4 请求要求 `4.0.*` ML provider，无匹配则拒绝，不把 3.1 标为 FSR 4。Configure → PrepareV2 → Generate 顺序、稳定虚拟相机基和输入 transfer 已接入；视频估计 depth / motion 保留实验边界。
3. `Fsr4 = 3` 保留既有 enum 数值，列表/节点各有四个入口；FSR 3.1 / 4 都限制 2×。provider 文案、节点标签和运行状态栏分开显示 FSR / DLSS。legacy preset 必要时写 v25；旧格式可读；preset library 与两模式 session 保存/重开通过。
4. 排查发现 preset library 只验证节点链，非法 FSR 4× 可写入预设。现在先验证 FG backend / multiplier 组合，拒绝时保持文件事务原样；真实失败用例修复后通过。
5. XeSS 修复 mismatch 日志使用两个临时字符串迭代器的未定义行为，补充 unlock 具体结果；沿用已审计的五处进程内补丁，没有修改磁盘 DLL。QML 根据所选后端/已审计组件/实际 SDK 上限提供 2 / 3 / 4×，节点 draft 与列表配置一致。
6. 真实 UI 验收首次复现：引擎拒绝 FSR 4 后已经恢复 FSR 3.1，但 facade 的 pending 设置和选择器仍为 FSR 4。现在 `requestSettings` 在同一锁内返回准确 revision，facade 只在该请求被拒绝时恢复 FG 字段；bridge 同步两模式的 FG 状态，其他待应用字段保留。修后重新测试 UI 和预设重开通过。
7. 导出沿用原有非 DLSS FG 替换行为；`VideoExportJob` 仅将新 FSR 4 enum 纳入相同既有检查，不宣称支持 FSR 3.1 / 4 离线补帧导出。本轮没有改 NR / SR 算法、音频时钟、采集、解码、旧 Win32 或着色器。

### 实测结果

本机为 RTX 5070（驱动 `32.0.16.1656`）与 AMD Radeon(TM) Graphics 核显（`32.0.21043.5001`）。AMD 核显验证不能外推为所有 Radeon 或 RX 9000 通过；机器已有虚拟显示设备，但本轮没有把它用作 FSR 修复接点。

| 验证 | 最终结果 | 证据目录（统一在 `E:/项目/Veyra/logs/fg-fsr-xess-20260930/`） |
| --- | --- | --- |
| 产品 + 针对性测试目标构建 | exit 0；最终状态文案修复已进入候选 | `build-status-label-final.log` |
| 独立 FSR 真实运动图，NVIDIA / AMD | 各 39 / 39 中间位置输出，平均质心误差 0 px；各 3 次 context 创建/销毁，D3D12 debugErrors=0 | `gpu-final/fsr-independent-*.log` |
| 同一进程 / HWND / device 连续切换 | NVIDIA 18 周期、774 生成帧；AMD 15 周期、660 生成帧；含 FSR 3.1 / XeSS 2、4× / 关闭，NVIDIA 另含 DLSS；reset、resize、FSR 4 拒绝及 FSR 3.1 重新启用均通过，debugErrors=0 | `gpu-final/fsr-switch-*.log` |
| 既有 DLSS 呈现 | 4× / 6× / 4× 三周期，consumer fence 与真实像素检查通过 | `gpu-final/dlss-presentation.log` |
| 设置、链、预设与 session | 四项 exit 0；contract 207 checks / 0 failures；FSR 3.1 / 4 往返、非法 4× 拒绝、list/node session 均通过 | `unit-rollback/summary.json` |
| QML 数据、动效和组件 | 三项 exit 0 | `qml-r2/summary.json` |
| 最终真实 QML 产品 | live PID 12820，67.36 s，exit 0 / FG_UI_PASS；恢复 PID 9152，3.69 s，exit 0 / FG_UI_RESTORE_PASS | `ui-final/summary.json` |
| 缺失 FSR 运行库负例，NVIDIA / AMD | 两次预期 exit 1，日志明确 load failed；无假生成帧、debugErrors=0 | `missing-runtime/summary.json` |

真实界面使用 4K30 视频、NR / SR 关闭：FSR 3.1 2× 约 60 提交 fps；XeSS 2 / 3 / 4× 约 60 / 90 / 120；DLSS 4× 约 120。覆盖 FSR→XeSS→DLSS→FSR、非法 FSR 4×、不支持的 FSR 4 ML 回滚、暂停后 seek / resize / resume、节点添加、独立模式配置、三份预设保存和进程重开。以上是软件提交率与 SDK 报告，未测屏幕端到端延迟。

XeSS 生成纹理位于 SDK 私有呈现队列，切换测试只读调用方持有的真实输入并独立核验 SDK 生成计数；**没有把输入读回当 XeSS 生成像素证明**。FSR / DLSS 另读实际调用方拥有的生成/呈现输出。GPU→CPU 读回仅用于测试，没有进入正常播放器链路。

### 失败证据与修复

- 初次构建发现 C 数组 `.size()`（C2228）及 enum / unsigned 比较（C4389）；改用固定 pool slot 常量和明确转换。对应失败构建日志保留，最终构建 exit 0。
- `unit-final` 两个新增非法 FSR 4× 预设断言失败，修复 preset library 验证缺口；`unit-r2` / `unit-rollback` 通过。
- `qml-final` 的数据测试缺少必需 scratch 参数，是 runner 调用错误；`qml-r2` 传入独立路径后三项通过。
- AMD 切换测试初次 6 个跨队列错误全部发生在测试读取 XeSS proxy backbuffer 时；诊断复跑定位到 input 10 / 35 的 readback。改为按 SDK 私有队列的资源所有权合同读调用方真实输入，保留 SDK 生成计数验证；`gpu-final` 两卡均 debugErrors=0。没有修改生产链路来掩盖错误。
- `ui/live.log` 首次发现 facade 回滚不一致；`ui-r2` 修复后通过；最终状态栏 FSR 标签修复加入断言后，`ui-final` 再次通过。失败日志未删除。

### 可复现命令与范围控制

在本轮工作区运行 `python -B scripts/acceptance/fg-fsr-xess-control.py guard`。测试用同一脚本的 `unit` / `qml` / `gpu` / `ui` 子命令，`--out` 必须给新的 E 盘目录；每个测试上限 300 s，UI 每进程 210 s。UI fragment 仅注入 disposable staging，finally 恢复 Main.qml；使用独立 `--data-dir`，不动用户配置。

构建命令为 `cmake --build E:/项目/Veyra/build/fg-fsr-xess-20260930 --target veyra_qml_ui veyra_qml_data_tests veyra_qml_easing_tests veyra_qml_quick_tests veyra_repair_contract_tests veyra_repair_preset_tests veyra_effect_chain_tests veyra_preset_library_tests veyra_fsr_dispatch_tests veyra_fsr_switch_tests veyra_fg_presentation_tests --parallel 6`，由 `E:/项目/Veyra/tmp/fg-fsr-xess-20260930/build-player.cmd` 调用 VS vcvars；子进程 TEMP / TMP 指向同任务 tmp，构建上限 900 s。

保留原 `scope-baseline.json`。发现具体必要接点后，在修改前追加不可变扩展：b（preset / 针对性测试 / UI fragment）、c（导出现有行为兼容）、d（真实 UI 失败后的 facade），全部继承原哈希；最终 d SHA256 `dd0791c996344ea9f0cf8f516e84b5086dd8810eedbf36c7b400a1707dc90bfc`。guard 比较所有非忽略源码的原哈希与授权 allowlist，不只比较 Git HEAD 差异；HEAD 与 main 没有推进。

未运行两个历史 `fsr_probe` 目标（工作树的旧相对 SDK 头文件路径缺失）；本轮新独立 GPU 测试与产品目标使用批准 SDK 的显式绝对路径，已经构建/运行。旧探针不作为通过证据。

### 仍需完成的验收

- **FSR 4 ML：** RX 9000 + 合适驱动实卡的实际 provider、生成输出/画质、HDR、性能与连续切换；本机只验证不支持负例。
- **AMD 反馈者 XeSS：** 取得其 GPU 型号/驱动、客户端版本、实际 `libxess_fg.dll` 身份与 Create / Init / SetNum 日志，区分 UI 仅 2×、解锁不匹配或 SDK 拒绝；本机 AMD 4× 通过不能替代该用户复现。
- **扩展稳定性：** HDR 实屏颜色、长时运行与物理显示/延迟，本轮短测没有证明。缺失运行库已验证独立 backend 负例，真实 UI 下运行库缺失回滚没有单独复测。
- **2.0.0 总交付：** 本轮不替代 P6 同候选全产品矩阵、P7 正式候选包/配置升级回退/许可与源码清单；旧 Win32 删除、merge / push / Release 仍需相应明确授权。历史不同候选的 P3 / P4 / P5 或 NR 结果不能直接拼成这份候选的整体验收。
