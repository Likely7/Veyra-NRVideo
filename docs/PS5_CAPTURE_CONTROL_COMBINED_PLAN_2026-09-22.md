# PS5 远程控制 + 采集卡画面组合模式执行方案

日期：2026-09-22
状态：方案待实施；本文档仅记录方案与证据，未修改任何源码，功能未开工。
基线：本地 main HEAD `05998da`（1.4.3 发布记录）。开工时工作区存在未提交改动（`CMakeLists.txt`、`apps/veyra/TelemetryWindow.cpp`、`docs/WORKLOG.md`、未跟踪 `launch.vs.json`），实施前重新确认 `git status` 并保存开工存档点，不覆盖这些改动。
分支约定：按项目惯例在隔离分支（建议 `codex/ps5-capture-control-20260922`）施工，开工前打 `checkpoint/pre-ps5-capture-control-20260922` 存档；合并 main 需用户当时明确授权。
产物目录：遵守 AGENTS.md 本机产物目录规则，本轮产物写入 `E:\项目\Veyra\` 下 `build/ps5-capture-control-20260922`、`tests/…`、`logs/…`、`tmp/…`。

## 1. 用户目标与完成边界

用户需求：在专业模式的 PS5 连接页面增加"启用采集卡"复选框，勾选后出现采集卡连接配置（直接引用原有配置页）。目的是**用 PS5 远程功能控制 PS5，画面用采集卡的画面**——在 PC 端控制 PS5 的同时获得采集卡的高清画面（含 4K/HDR），而非远程串流最高 1080p 的压缩画面。

用户已确认的两个设计决策（2026-09-22 对话）：

1. **完整内嵌**：把现有采集卡配置页的控件与逻辑提取为共享区块，PS5 页面勾选后内嵌显示原有全部配置项；采集卡弹窗改用同一实现，两边永远同源。
2. **音频来源 = 采集卡 HDMI 音频**：声音与画面同源，走采集管线现成的音画同步；PS5 远程 Opus 音频在本机接收侧丢弃。手柄震动/自适应扳机触觉走控制通道，不受影响。

完成边界：

- 勾选后 PS5 远程会话仅作控制通道（PC 手柄 → PS5），画面与声音走采集卡 HDMI；未勾选时行为与现在完全一致。
- PS5 远程会话期间 HDMI 输出持续有效，采集卡能拿到完整画面——这是本组合模式的价值前提，也是用户已认知的行为。
- **不做**：键盘 → PS5 映射（现无此能力，控制 = PC 手柄，与现有串流一致）；不宣称"零视频带宽"（协议层没有关闭视频的开关，见 2.3，PS5 仍发送低码率视频保活，UI 与文档如实注明）；不新增采集 HDR/HDMI 音频处理路径（全部复用现有采集管线）。
- RemotePlayPanel 为日常/专业两种模式共用的弹窗，复选框默认两种模式都显示；如后续要求仅专业模式可见，加 `uiState.mode` 门控即可。
- 本方案不授权 push、Release 或打包。

## 2. 已核对的事实（2026-09-22 静态审计）

### 2.1 控制通道与视频流在数据面上解耦

- 手柄路径：`ControllerInput::poll`（UI 线程，AppShell `ControllerTimer` 8ms）→ `EngineController::remotePlayController`（`src/engine/EngineController.cpp:88`）→ `RemotePlaySessionSource::controller`（16 深度有界队列 + 100ms 过期 + 失焦清空，`src/source/RemotePlaySessionSource.cpp:200-218`）→ 会话 owner 线程内每 4ms `source.submitController`（`RemotePlaySessionSource.cpp:88-94`）→ `ChiakiBackend::submitController` → `chiaki_session_set_controller_state`（`src/remoteplay/ChiakiBackend.cpp:236-265`）。该路径完全不经过 VideoIngress/解码器/渲染。
- 反方向先例已存在：`viewOnly`（仅观看）= 只要视频不要手柄（`enable_dualsense=false`，`ChiakiBackend.cpp:187`）。本功能是它的镜像：只要手柄不要视频。

### 2.2 会话宿主自包含

- `RemotePlaySessionSource::connect()` 自起 owner jthread（`RemotePlaySessionSource.cpp:13`），会话生命周期不依赖引擎渲染循环存活；引擎侧 `activeRemote_` 只是共享指针持有（`EngineController.cpp:176-178`），run() 收尾时释放并 `remote->close()`（`EngineController.cpp:1554-1557`）。
- 会话停止契约（idle 手柄 → stop → join → fini）在 `ChiakiBackend::stop()`（`ChiakiBackend.cpp:205-226`），与视频无关，可独立执行。

### 2.3 协议层没有"关闭视频"开关

- `docs/REMOTEPLAY_COLOR_UI_CONTROLLER_REPAIR_PLAN_2026-09-12.md:64` 明确约束：不能宣称有通用 "video-only 绕过账号占用" 的协议开关。**可行做法是照常接收但在本机接收侧丢弃**：`ChiakiBackend::Impl::videoCallback` 返回 false 即拒收并计数 `callbackRejected`（`ChiakiBackend.cpp:114-133`），该路径已存在。Opus 音频同理在 `opusFrame` 直接返回（`ChiakiBackend.cpp:141-152`）。
- 为把保活视频的带宽/解码开销降到最低，组合模式的 PS5 请求档案自动使用低规格：720p · 30fps · H.264 · 5Mbps（档位来自 `RemotePlayPanel.cpp:30,73-74` 的现有枚举），不引入新档位。

### 2.4 健康判定是主要改造点

- `StreamRecovery` 以**解码帧**为进度：无首帧 30s 判死、每会话 3 次重连（`include/veyra/remoteplay/StreamRecovery.h:11-29`）；`RemotePlaySessionSource::run()` 只在 `source.read()` 出帧时调 `recovery.frame()`（`RemotePlaySessionSource.cpp:108-115`），不出帧时走 `recovery.poll()`（:128-139）。控制-only 模式不解码视频，**必须改为以收到的视频回调计数（`native.videoCallbacks`）计进度**，否则 30s 后会话被误判死亡。native snapshot 已在 run() 循环内周期刷新（`RemotePlaySessionSource.cpp:99`）。
- 重连/RP_IN_USE 20s 退避逻辑（`RemotePlaySessionSource.cpp:29-31,150-173`）与解码无关，原样复用。

### 2.5 采集卡配置 UI 不能直接嵌入，但底层全部可复用

- `CapturePanel.cpp` 状态全部在匿名命名空间文件级静态 + 弹窗单例（`apps/veyra/ui/CapturePanel.cpp:17,168`），控件一次性建于 WM_CREATE，`arrange()` 硬编码坐标——不能作为子区块直接挂进 PS5 页面。
- 可复用：`CaptureCardSource::deviceDetails/formats/makeCapturePath`（`include/veyra/source/CaptureCardSource.h:30-35`）、`CapturePreferenceStore`（`apps/veyra/ui/CapturePreferenceStore.h:11-18`）、异步查询 + WM_TIMER 回填模式（`CapturePanel.cpp:54-57,82-117`）、主题工具（`apps/veyra/ui/Theme.h`）、`installDialogHelp`（`apps/veyra/ui/SettingHelp.h`）。
- 勾选显隐/启停控件的现有先例：`ScreenCapturePanel.cpp:31`（`methods()` 按 Kind `EnableWindow`）、`SettingsWindow.cpp:1479` + `arrange()` :1130-1136（`smoothMotionHelpExpanded` 控制整段区域可见性与高度）。

### 2.6 引擎一次只有一个 activeSource

- `EngineController::run()` 按 `physicalCapture`/`isScreen`/`remote` 三布尔选定唯一 `activeSource`（`EngineController.cpp:169-190`）；`open()` 与 `openRemotePlay()` 都整体重置快照互相踢掉对方会话（`EngineController.cpp:69-86`）。**组合模式 = 采集卡占用渲染槽位（`engine.open(video, "capture2:...")`），PS5 会话作为 run() 之外的旁路对象存活。**
- 采集画面与窗口的绑定方式：交换链直接建在主窗口视频子 HWND 上（`VideoPresenter::open` → `gfx::PresentSink`，`src/gfx/PresentSink.cpp:83,204`），拉起采集画面的唯一路径就是 `openFile(capturePath)` → `engine.open`（`apps/veyra/ui/AppShell.cpp:328,688`）。
- 采集音频（HDMI）自带独立时钟与音画同步（`sink::CaptureAudioSession`，由每帧真实呈现回调 `captureSource.videoPresented` 驱动）；不依赖 PS5 远程时钟。远程路径的音画同步逻辑（`videoPresented` 喂音频钟）只在解码帧存在时才有意义，组合模式不涉及。

### 2.7 UI 现状

- PS5 面板：`apps/veyra/ui/RemotePlayPanel.cpp`，控件 ID enum :18，ViewOnly 复选框 :101，Connect 处理 :208（写 ProfileStore + settings.ini + 调 connect 回调），`showRemotePlayPanel` 签名 :221（connect/pin/status/stop/calibration 五个回调），窗口固定 590×709 dip :225。
- 入口与回调接线：`AppShell.cpp` case RemotePlay :675-686，case Capture :688（`showCapturePanel` 的 start 回调就是 `openFile(path)`），`ControllerTimer` :705（条件 `state.remotePlay && (running||Opening)`）。
- PS5 面板持久化：每主机 `.dat`（DPAPI，`ProfileStore`）+ `settings.ini` [RemotePlay] 节（LastProfile/DecodeMode/FineSampling）。新增的"启用采集卡"勾选状态**不进入** `.dat` 档案（组合模式自动用低规格档案，不污染正常串流设置）；建议存入 settings.ini [RemotePlay] `UseCapture`。

## 3. 方案设计

### 3.1 后端：RemotePlaySessionSource 增加 control-only 模式

- `RemotePlayConnectDesc` 新增 `bool controlOnly=false`（`include/veyra/source/RemotePlaySource.h:25`）。该结构仅存在于连接请求，不入 ProfileStore 存档，不影响持久化格式版本。
- `ChiakiBackend::start()` 增加丢弃开关（`NativeConnectRequest` 传字段或 start 前 setter）：controlOnly 时
  - `videoCallback`：校验基本合法后直接 `++videoCallbacks; return false;`（不拷贝 payload、不入 ingress、零内存驻留）；
  - `opusFrame`：直接返回，不累积 PCM；
  - 事件（CONNECTED/LOGIN_PIN/QUIT/RUMBLE/TRIGGER_EFFECTS/MOTION_RESET）、手柄、触觉、反馈路径**完全不动**。
- `RemotePlaySessionSource::run()` control-only 分支：
  - 跳过 WASAPI `audio_.configure/start` 与音频 feeder 线程（`RemotePlaySessionSource.cpp:51-70`）；
  - 跳过 `source.read()` 解码与 `publishDecoded`；
  - **进度改为 `native.videoCallbacks` 前进时调 `recovery.frame()`**；native snapshot 刷新频率从 1s 提高到与循环匹配（或用 `videoCallbacks` 原子计数直接读取），保证 30s 判死窗口内有足够进度采样；
  - 登录 PIN 提交路径保留（`RemotePlaySessionSource.cpp:81-86`）。
- 组合模式下 UI 强制 `viewOnly=false`（`enable_dualsense=true` 才有手柄通道）。

### 3.2 引擎：EngineController 增加旁路控制会话

- 新成员 `std::shared_ptr<source::RemotePlaySessionSource> controlRemote_`（与 `activeRemote_` 并列，`include/veyra/engine/EngineController.h:130`）。
- 新方法（均在 `#ifdef VEYRA_ENABLE_REMOTEPLAY` 内）：
  - `openRemotePlayCapture(HWND, RemotePlayConnectDesc, std::wstring capturePath, PlayerOptions)`：
    - 若 `controlRemote_` 已存活且请求主机/consoleId 相同 → **复用现有 PS5 会话**，仅重开采集（避免误杀活跃控制会话触发 RP_IN_USE）；
    - 不同 → 先按停止契约关闭旧会话，再以 `controlOnly=true` 建立新会话；
    - **连接顺序：先连 PS5（易失败项：配对/网络/PIN），成功后再 `open(video, capturePath, options)` 开采集**；PS5 失败则不开采集、状态如实报错。
  - `stopRemotePlayControl()`：仅关闭 PS5 控制会话（idle 手柄 → stop → join 契约不变）。
- 路由合并：`remotePlayController/remotePlayFeedback/remotePlayLoginPin`（`EngineController.cpp:87-89`）与 `snapshot()` 远程遥测（:149-155）改为优先 `controlRemote_`、否则 `activeRemote_`。
- `PlayerSnapshot` 新增 `bool remoteControl=false`；组合模式下填充 remote* 遥测字段（状态、恢复、收包计数），供 UI 状态回调显示。
- 生命周期规则：
  - `EngineController::open()`（`EngineController.cpp:69`）**不**无条件清 `controlRemote_`：仅当新路径不是本组合模式登记的采集路径时关闭（组合模式下重连/重配采集卡不杀 PS5 控制；打开普通文件、屏幕采集、纯远程串流则关闭控制会话）。实现上用"组合模式登记的 capturePath"比对当前请求路径。
  - `engine.stop()`（停止按钮）**保留**控制会话——快照 `remoteControl=true` 时状态栏提示"PS5 控制仍在运行，断开请在 PS5 面板操作"。
  - `openRemotePlay()`（普通串流）与析构函数关闭 `controlRemote_`。

### 3.3 UI：提取共享采集配置区块 + PS5 面板内嵌

- **新建 `apps/veyra/ui/CaptureConfigBlock.h/.cpp`**：把 CapturePanel 的控件创建（视频设备/格式/音频监听/输入颜色/转 SDR/翻转/缓冲/帧率/刷新/状态）、`rebuildAudioList`、异步查询 + 定时回填、选中记忆、`makeCapturePath` 组路径与 `CapturePreferenceStore` 保存，提取为可挂任意父窗口的区块：
  - 控件 ID 基址参数化（如 600 起，避免与宿主 ID 冲突）；专用 timer id；接口形如 `create(parent)/show(bool)/arrange(x,y)/handleCommand(id)/poll()/capturePath()/savePreferences()`；
  - 四个引擎级即时设置（forceSdrPreview/captureAudio/captureFlipVertical/captureBuffer）的 read/set 回调由宿主注入，语义与现 `showCapturePanel`（`CapturePanel.h`）一致。
- **`CapturePanel.cpp` 重构为宿主**：弹窗外观、帮助文本、连接按钮不变，内部改用同一区块。此步是行为不变的纯重构，单独提交并做采集面板手工冒烟。
- **`RemotePlayPanel.cpp/.h`**：
  - 新复选框 `UseCapture`（`BS_AUTOCHECKBOX`，位置在"仅观看"附近；勾选时强制取消并禁用 ViewOnly——组合模式必须转发手柄）；
  - 勾选后：显示内嵌区块、禁用 PS5 画面相关控件（格式/解码/采样/码率，注明"组合模式自动使用低规格保活档"）、`SetWindowPos` 加高窗口并按 expanded 标志重排 `arrange()`（参照 SettingsWindow 折叠先例）；取消勾选恢复原高度；
  - Connect 处理：勾选时校验采集设备已选 → 保存采集偏好 → 组 `capture2:` 路径 → 携带双 desc 调用新回调；`showRemotePlayPanel` 签名扩展：启动回调改为 `(RemotePlayConnectDesc, std::wstring capturePath)`（空路径 = 原模式），或新增独立组合模式回调；
  - 复选框状态持久化到 settings.ini [RemotePlay] `UseCapture`，默认 0。
- **`AppShell.cpp`**：
  - `case RemotePlay`（:675）新回调：启动 SDL 手柄 + ControllerTimer（现有逻辑），调 `engine.openRemotePlayCapture`，主窗口标题 "Veyra — 采集卡 · PS5 控制"；
  - `ControllerTimer`（:705）条件扩展 `|| state.remoteControl`；
  - 状态回调（:676-685）适配组合模式：显示"生效：采集卡画面 · PS5 控制已连接"及手柄能力信息（陀螺仪/触摸板/扳机/触觉，沿用现有格式）；
  - 断开回调：`engine.stopRemotePlayControl(); engine.stop();`。

## 4. 施工顺序（每步可构建、可回归）

1. **后端 control-only**：`RemotePlayConnectDesc` 标志 + `ChiakiBackend` 丢弃开关 + `RemotePlaySessionSource` control-only run 分支 + StreamRecovery 进度改造；新增针对性测试（收包计数驱动 recovery、无解码不判死、PIN 路径不受影响）。
2. **引擎旁路会话**：EngineController 新方法/成员/路由/快照字段 + 生命周期规则（含"重开采集不杀控制"）；跑现有引擎与 remoteplay 回归。
3. **UI 区块提取**：新建 CaptureConfigBlock，CapturePanel 改为宿主（纯重构，行为不变），采集面板手工冒烟 + 相关既有 UI 测试。
4. **PS5 面板接入**：UseCapture 复选框 + 内嵌区块 + 连接流程 + AppShell 接线 + 状态/标题/定时器适配。
5. **验证与文档**：`VEYRA_ENABLE_REMOTEPLAY=ON` 完整构建（产物写 `E:\项目\Veyra\build\ps5-capture-control-20260922`）；相关 ctest + `scripts/gates/delivery.ps1`；更新 `docs/WORKLOG.md` 与本文档的实施状态。

每步交付按 AGENTS.md 要求报告：修改文件、实际运行的构建/测试命令、真实日志、未执行项如实标注。

## 5. 验收与反证

自动化检查（每步）：

- 构建零警告通过（含 `VEYRA_ENABLE_REMOTEPLAY=ON`）；
- remoteplay / 采集相关既有测试套全绿；新增 control-only 单元测试通过；
- 最终交付前跑 `scripts/gates/delivery.ps1`。

用户真机验收清单（本机 RTX5070 + USB3 采集卡 + PS5；Agent 无法独立完成的项目如实标"未执行"）：

| 检查 | 完成条件 |
| --- | --- |
| 组合连接 | 勾选后选采集设备 → 连接：手柄能控制 PS5，画面为采集卡输入（4K/HDR 按采集管线现有能力），HDMI 音频正常且与画面同步 |
| 未勾选回归 | 不勾选时连接 = 现有纯串流行为逐项一致；采集卡弹窗功能与提取前一致 |
| 断开/重连 | PS5 面板断开同时结束控制与采集；重连在 RP_IN_USE ~20s 窗口内按现有退避恢复；登录 PIN 流程可用 |
| 停止按钮 | 停止仅结束采集与增强，PS5 控制保持（提示文案出现）；从 PS5 面板可单独断开控制 |
| 会话切换 | 组合模式中打开普通文件/屏幕采集/纯远程 → 控制会话被关闭，无悬挂；重开组合模式正常 |
| 保活开销 | 日志显示 control-only 会话 `videoCallbacks` 持续前进、零解码、无 30s 误判死亡；低规格档案生效 |
| 状态如实 | UI 显示"生效：采集卡画面 · PS5 控制已连接"；不显示串流码率/分辨率冒充采集画质 |

反假冒条款：不得把"PS5 仍发送低码率保活视频"说成"零视频带宽"；不得把控制-only 会话的收包计数冒充解码帧率；不得用串流 1080p 画面冒充采集卡 4K/HDR 画面；采集画质/HDR/音画同步的表现由采集管线现有验收状态决定，本方案不重复宣称。

## 6. 授权边界

- 本轮（2026-09-22）仅授权撰写本方案文档；实施、打包、push、GitHub Release 均未授权，需用户在后续对话中明确授权后进行。
- 实施时遵守：隔离分支施工、不推送不发布；源码 Git 禁止 SDK/DLL/模型/凭据；产物写入 `E:\项目\Veyra\`；chiaki-ng AGPL-3.0 + OpenSSL exception 归因沿用现有 `licenses/remoteplay/`，不引入新的第三方代码。
