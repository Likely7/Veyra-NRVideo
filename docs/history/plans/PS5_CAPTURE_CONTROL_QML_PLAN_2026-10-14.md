# PS5 手柄控制 + 采集卡画面：2.0.x 适配方案 2026-10-14（Claude）

> 状态：**已按方案 A 实施**（用户：“方案A吧，开工！”），实施记录见 [PS5_CAPTURE_CONTROL_2026-10-14.md](../reports/PS5_CAPTURE_CONTROL_2026-10-14.md)。
> 实施时更正一处：音频不能“不挂接收”（Chiaki 没有回调时会泄漏每个音频帧），改为挂空回调。下文保留当时的方案。

用户原话：“你看一下 PR7，他可以只串流手柄到电脑上，输出依旧用采集卡，为什么要这个功能呢，有些人想先在电脑处理画面，
然后再次串流给掌机或者其他电脑，玩 ps5，这样就可以通过电脑来控制 ps5 的游戏了，但是这个设置 ui 不知道怎么设计比较好，
你给想想然后看看怎么适配，因为这是远古版本的 pr 了”。

## 1. PR #7 现状

- [Likely7/Veyra-NRVideo#7](https://github.com/Likely7/Veyra-NRVideo/pull/7)，分支 `codex/ps5-capture-control-20260922`，2026-09-22。
  - 状态 OPEN，`CONFLICTING`。
  - 基线 `05998da`（1.4.3），比现在的代码落后 525 个提交。
- 改动分两块：
  - **界面**（约 2/3）：改的是旧 Win32 界面 `apps/veyra`（`RemotePlayPanel`、`CaptureConfigBlock`、`CapturePanel`、`AppShell`）。
    2.0.x 发布的是 QML 界面 `veyra_qml_ui`，这部分**用不上**，要重新设计。
  - **后端**（约 1/3）：`RemotePlayConnectDesc.controlOnly`、`ChiakiBackend` 丢弃媒体、`RemotePlaySessionSource` 控制专用循环、
    `EngineController` 旁路会话。
    - 从 PR 基线到现在（`git diff --stat 05998da HEAD`）：
      - `ChiakiBackend`、`RemotePlaySessionSource`、`ControllerInput`、`ProductBoundaryTests` 没有改动；
      - `RemotePlaySource.cpp` 只多 2 行；
      - `EngineController` 改了约 1700 行（Moonlight、Xbox、引擎看门狗等），这部分要重写。
    - 前几个可以直接移植，但其中有几处必须改，见第 4 节。
- PR 自己写明“真实 PS5 + 采集卡组合链路未验收”。

## 2. 两种用途，同一条链路

| 用途 | 链路 |
|---|---|
| A. 原 PR：电脑旁边玩 | PS5 → HDMI → 采集卡 → Veyra（4K/HDR 原画 + 增强）；电脑上的手柄 → Veyra → PS5 远程游玩（只传手柄）→ PS5 |
| B. 这次的需求：再串流给掌机/别的电脑 | PS5 → 采集卡 → Veyra 处理 → Sunshine / Apollo 等串流到掌机；掌机手柄 → 串流软件在电脑上建的虚拟手柄 → Veyra → PS5 远程游玩 → PS5 |

B 比 A 多出三个问题，PR #7 都没处理：

1. **焦点**：现在只有 Veyra 是前台窗口时才转发手柄（`ControllerInput::poll(focused)`，失焦送空手柄）。
   - 再串流时焦点可能被串流软件、通知或别的窗口拿走，掌机那边就失控了。
   - 需要一个“后台也转发”开关。
2. **多个手柄**：现在只用第一个找到的手柄（`SDL_GetGamepads` 第一个）。
   - 电脑上已经插着手柄时，串流软件后建的虚拟手柄会被忽略，掌机按键没反应。
   - 需要“谁按键就用谁”。虚拟手柄和真 Xbox 手柄在 SDL 里常常同名，按名字选不可靠，所以不做下拉选择。
3. **声音**：再串流时声音走采集卡；采集卡对话框的“音频监听”没开，掌机就没声音。界面上要提示。

## 3. 界面方案

### 方案 A（推荐）：采集卡对话框里加“PS5 手柄控制”分区

画面来源仍然是采集卡，PS5 远程游玩只当手柄通道。这和引擎的实际结构一致：采集卡是唯一的画面源，PS5 是旁路。

关闭时只有一行：

- **用远程游玩控制 PS5**（开关）
  - 说明：画面和声音来自采集卡，手柄输入经 PS5 远程游玩发给主机。
  - 用途：把处理后的画面再串流给掌机或别的电脑时，用那边的手柄玩 PS5。

打开后展开：

- **PS5 主机**：已配对主机下拉（和“PS5 串流”共用同一个选择）+ 连接状态标签 + “配对…”按钮（打开 PS5 对话框）。
  没有配对过时显示“先在「PS5 串流」里配对一次”和“去配对”。
- **后台也转发手柄**（开关，默认关）。
  - 说明：再串流时建议打开，Veyra 不在前台也继续转发。
  - 风险提示：打开后在这台电脑上用别的程序时，手柄也会操作 PS5。
- **手柄**：只显示，不让选。“谁按键就用谁”，显示当前手柄名和陀螺仪/触摸板能力。
- **登录 PIN**：只在 PS5 要求时出现。
- **状态行**：已连接 · 保活画面 30 fps（收到即丢弃）/ 正在连接 / 重连中（1/3）/ 失败原因。
- **固定说明**：
  - PS5 仍会发一路 720p 30fps、约 5 Mbps 的保活画面，本机收到即丢弃；
  - PS5 同一时间只允许一个远程游玩连接；
  - 再串流时记得打开上面的“音频监听”。

行为：

- 采集中拨开关，立即连上或断开 PS5，不重连采集卡。
- “连接并开始”：采集卡和 PS5 控制一起启动。PS5 连不上不影响出画，只显示原因。
- “断开”：两个都停。
- 打开文件、屏幕捕获、PS5 串流、PC 串流、Xbox：先停掉 PS5 控制。
- “继续上次采集”：开关开着就一并恢复。
- 采集卡因为改格式或信号断了重连，PS5 控制不断。

PS5 串流对话框只加一处引导：

- 一行“画面用采集卡？”，配一个“打开采集卡设置”按钮；
- 说明：PS5 的 HDMI 接了采集卡时，可以只用远程游玩传手柄，画面用采集卡原画（可 4K/HDR），也方便再串流给掌机。

主页的采集卡卡片：开关打开时显示一个小标签“PS5 手柄”（可选）。

### 方案 B：PS5 串流对话框里加“画面来源”

PR #7 的思路：PS5 对话框顶部加“画面来源：PS5 串流 | 采集卡”。

- 选“采集卡”后：
  - “串流（重连生效）”一组隐藏；
  - 换成一行采集卡摘要（设备 · 1080p60 NV12）和“采集卡设置…”按钮；
  - 再加“后台也转发手柄”。
- “连接”按保存的采集卡设置开采集，同时连 PS5 控制。

不推荐的原因：

- 采集卡设备/格式要跳到另一个对话框改。
- 断开、继续上次分散在两个对话框。
- PS5 对话框本来就有 5 个分区，会更长。
- 主页“继续上次”记的是“PS5”还是“采集卡”说不清。

PR #7 那种把采集卡全部设置嵌进 PS5 面板的做法（窗口 709→1100 dip），在 QML 里虽然好嵌，但两个对话框合起来太长，也不推荐。

## 4. 后端移植：哪些照搬，哪些必须改

照搬：

- `RemotePlayConnectDesc.controlOnly`、`RemotePlaySource::discardMedia_` → `ChiakiBackend::start(..., discardMedia)`。
- `enable_dualsense = !viewOnly || discardMedia`：控制模式强制有手柄通道。
- `RemotePlaySessionSource` 的控制专用循环：
  - 没有解码器、没有 WASAPI；
  - 用“收到的保活视频回调数”喂 `StreamRecovery`，不会被 30 秒无解码帧判死；
  - 保留登录 PIN、RP_IN_USE 10/20/40 秒退避、`alive_` 标志。
- 保活档案 720p · 30fps · H.264 · 5 Mbps，不写进配对存档。
- `ProductBoundaryTests::controlOnlyBoundary`、`--control-stress`。

必须改：

1. **PR 的丢帧方式会让 Chiaki 每帧向 PS5 报一次“坏帧”**（读 `chiaki-msvc-stage/lib/src/videoreceiver.c` 确认）。
   - PR 让视频回调返回 `false` 来丢弃。Chiaki 对此的处理：
     - 记为失败，不加参考帧；
     - `frame_index_prev_complete` 不前进；
     - 下一帧就判定“缺帧/坏帧”，调用 `stream_connection_send_corrupt_frame` 发给主机；
     - 每帧再写 2 条警告（`upstreamWarnings`、`assemblyErrors` 一直涨）。
   - 结果是每秒约 30 次坏帧报告，主机可能因此反复补关键帧，也可能降质。
   - 改法：
     - 视频回调计数后返回 `true`（收下就丢），Chiaki 正常维护参考帧，不发坏帧报告；
     - 音频不挂 Opus 接收（不 `chiaki_session_set_audio_sink`），连解码都省掉；
     - 震动、自适应扳机、触觉、事件路径不变。
2. **PR 在界面线程上同步关闭会话**：`open()`、`openRemotePlayCapture()`、`stopRemotePlayControl()` 里直接 `retire->close()`。
   - `close()` 要等 Chiaki stop/join，网络不好时会卡住界面。
   - 改为引擎里一个专用控制线程，按“最后一次请求为准”依次关旧、开新，界面线程只投递请求。
   - PS5 串流开始前等这个线程把旧会话关完：PS5 只有一个远程游玩名额。
   - 析构时有上限等待，沿用引擎现有的退出看门狗做法。
3. **PR 让采集卡等 PS5 连上才出画**。
   - `connect()` 只代表 `chiaki_session_start` 成功，真正的网络、配对、RP_IN_USE 失败都在之后异步发生，所以这个“先连 PS5”挡不住什么，反而让画面受 PS5 拖累。
   - 改为两者独立：采集卡照常开，PS5 控制另报状态。
4. **PR 靠“采集路径字符串相同”决定保留控制会话**。QML 里改格式就是新路径，会误杀控制会话。
   - 改为桥接层显式管理：采集会话在，控制就在；换源或断开才停。
5. **新增（PR 没有）**：
   - `ControllerInput` “谁按键就用谁”：打开全部手柄，收到非当前手柄的按键就切换，切换时清空触摸/陀螺仪状态；
   - 后台转发：桥接层把 `focused || 后台转发` 传给 `poll`/`feedback`；
   - 控制状态接口 `remoteControlStatus()`：状态、保活 fps、重连、失败原因、是否要 PIN。

## 5. 2.0.x 改动清单

| 层 | 文件 | 内容 |
|---|---|---|
| 后端 | `include/veyra/source/RemotePlaySource.h`、`src/source/RemotePlaySource.cpp` | `controlOnly` → `discardMedia_` |
| 后端 | `include/veyra/remoteplay/ChiakiBackend.h`、`src/remoteplay/ChiakiBackend.cpp` | `start(..., discardMedia)`：收下即丢、不挂音频 |
| 后端 | `include/veyra/source/RemotePlaySessionSource.h`、`src/source/RemotePlaySessionSource.cpp` | 控制专用循环、`alive_` |
| 引擎 | `include/veyra/engine/EngineController.h`、`src/engine/EngineController.cpp` | 控制线程、`startRemoteControl` / `stopRemoteControl` / `remoteControlStatus`、手柄与 PIN 路由、串流前等待名额 |
| 输入 | `include/veyra/remoteplay/ControllerInput.h`、`src/remoteplay/ControllerInput.cpp` | 谁按键就用谁、当前手柄名 |
| 桥接 | `include/veyra/ui/QmlPlayerBridge.h`、`src/ui/QmlPlayerBridge.cpp` | `ps5Control` 属性、开关方法、偏好 `ps5CaptureControl` / `ps5ControlBackground`、采集开停与换源时的启停、手柄定时器条件 |
| 界面 | `qml/Veyra/DialogHost.qml`（采集卡、PS5 两个对话框），可选 `HomePage.qml` | 方案 A 的分区和引导 |
| 文案 | `i18n/catalog.json` | 简中/繁中/英/日 |
| 测试 | `tests/remoteplay/ProductBoundaryTests.cpp`、UI 合同、对话框截图 | 见第 6 节 |

不移植：PR 的 Win32 界面（`RemotePlayPanel`、`CaptureConfigBlock`、`CapturePanel`、`AppShell`），以及与本功能无关的两个提交
（VS2026 IDE 启动修复、设置页滚动条）。

## 6. 验证

本机能做的（软件侧）：

- 控制专用会话：非法主机反复连接/关闭 200 次不泄漏（PR 的 `--control-stress`）；手柄队列语义不变；失焦送空。
- 丢帧方式：加一个只在测试里用的入口，确认丢弃模式下视频回调返回“已接收”、计数增加、不进解码队列。
- 引擎：控制线程开/关/换主机/析构不卡界面线程（GUI 心跳日志）；PS5 串流开始前等待旧控制会话关完。
- 手柄：本机接一个真手柄 + 一个虚拟手柄时，按键切换当前手柄（有 ViGEm 时测；没有就只测单手柄不回归）。
- 界面：采集卡对话框开/关/无配对/需要 PIN 四种状态截图；PS5 对话框引导行；UI 合同；中英日文案长度。

只有真机能确认的（需要用户或反馈的人测）：

- PS5 远程游玩期间 HDMI 输出是否保持原分辨率、HDR、120Hz（PR 的前提，目前没人实测过）；
- 保活画面的实际码率、主机是否稳定；
- 手柄延迟：本机手柄直连、经 Sunshine 虚拟手柄两种；
- 登录 PIN、RP_IN_USE 退避、断网重连；
- 掌机经串流软件玩时的震动和陀螺仪（虚拟 DS4 才有陀螺仪和触摸板，虚拟 Xbox 手柄没有）。

## 7. 待用户拍板

1. 界面用方案 A（采集卡里加分区，推荐）还是方案 B（PS5 对话框里选画面来源）。
2. “后台也转发手柄”默认关（推荐，避免在电脑上做别的事时误操作 PS5），还是打开 PS5 手柄控制时默认开。
3. 做在当前分支 `claude/fg-gpu-home-20261014` 上，还是单开分支（例如 `claude/ps5-capture-control-20261014`，基于 main）。
