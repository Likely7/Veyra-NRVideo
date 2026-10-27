# PS5 手柄控制（采集卡画面 + 远程游玩只传手柄）实施记录 2026-10-14（Claude）

方案见 [PS5_CAPTURE_CONTROL_QML_PLAN_2026-10-14.md](../plans/PS5_CAPTURE_CONTROL_QML_PLAN_2026-10-14.md)。用户拍板：“方案A吧，开工！”。
另外两个待定项没有回复，按方案里的推荐执行：

- “后台也转发手柄”默认关；
- 继续做在 `claude/fg-gpu-home-20261014` 上，提交单独成组，需要时可以挑出去。

证据在 `logs/claude-fg-gpu-home-20261014/`。

## 用法

1. 先在「PS5 串流」里配对一次（已有配对可跳过）。
2. 打开「采集卡」，在「PS5 手柄控制」分区打开“用远程游玩控制 PS5”。
   - PS5 主机：和「PS5 串流」共用配对和当前选择；
   - 后台也转发手柄：再串流时建议打开；
   - 手柄：谁按键就用谁，显示当前手柄名和是否有陀螺仪；
   - 登录 PIN：只在 PS5 要求时出现。
3. 点“连接并开始”：采集卡出画，同时连接 PS5，只传手柄。
   - 采集中拨开关：立即连上或断开 PS5，不重连采集卡；
   - 改采集格式重连、信号断了自动重连：PS5 手柄控制不断；
   - “断开”：两个一起停；
   - 打开文件、屏幕捕获、PS5 串流、PC 串流、Xbox：PS5 手柄控制先停。PS5 同一时间只允许一个远程游玩连接，PS5 串流会等它关完再连（最多 10 秒）；
   - 失败后点“重连”，或关开一次开关。
4. 再串流给掌机：用 Sunshine / Apollo 等推这台电脑的画面。掌机手柄经串流软件变成电脑上的虚拟手柄，Veyra 读到后转发给 PS5。
   - 记得打开采集卡的“音频监听”，串流软件才收得到声音；
   - 虚拟 DS4 才有陀螺仪和触摸板。

PS5 串流对话框“仅观看”下面加了一行“画面用采集卡”，按钮打开采集卡设置。
主页“采集卡”卡片在开关打开时显示“PS5 手柄”标签。

## 实现

**后端（从 PR #7 移植，按方案第 4 节修正）**

- `RemotePlayConnectDesc.controlOnly` → `RemotePlaySource::discardMedia_` → `ChiakiBackend::start(..., discardMedia)`。
- **视频回调计数后返回 true（收下即丢）。**
  - PR #7 返回 false。读 `chiaki-msvc-stage/lib/src/videoreceiver.c`：false 会让 `frame_index_prev_complete` 停住，下一帧起每帧都 `stream_connection_send_corrupt_frame`，每帧写两条警告。
- **音频挂一个空回调的接收器，不是不挂。**
  - 方案原写“不挂 Opus 接收”。实施前再读 `audioreceiver.c`：没有 `frame_cb` 时，接收器在释放缓冲前就返回，每个音频帧都会泄漏。
  - 现在挂空回调，Chiaki 照常释放缓冲；不初始化 Opus 解码器，不解码。
- `enable_dualsense = !viewOnly || discardMedia`。
- `RemotePlaySessionSource` 控制专用循环：
  - 没有解码器、WASAPI、进度监视线程；
  - 每 4 ms 发一次手柄，等待用高精度可等待计时器（`Sleep(1)` 在默认计时精度下约 15.6 ms）；
  - 保活视频回调数增加就算进度喂给 `StreamRecovery`；会话失败（QUIT）直接按失败处理，不等 30 秒；
  - 保留登录 PIN、RP_IN_USE 10/20/40 秒退避、重连提示（“PS5 手柄控制中断，正在重新连接（n/3）…”）；
  - 新增 `alive()`、`failed()`。
- 保活档案 720p · 30fps · H.264 · 5 Mbps，只用于这次连接，不写回配对存档。

**引擎**

- `startRemoteControl` / `stopRemoteControl` 投递给专用控制线程（最后一次请求为准），界面线程从不等待主机。
  - 控制线程先关旧会话再开新会话；
  - 连接失败也保留会话对象，状态带出原因。
- `open()`：非采集卡路径停 PS5 手柄控制；采集卡重开不停。
- `openRemotePlay`：先停控制，串流任务里等控制线程关完（最多 10 秒，停止键可打断）。
- `openMoonlight`、`openXbox`、`startExport`：先停控制。
- 析构：先通知控制线程收尾，主线程结束后最多等 8 秒，超时按现有退出看门狗做法结束进程。
- `remotePlayController/Feedback/LoginPin`：有 PS5 串流时给串流，否则给 PS5 手柄控制。
- `remoteControlStatus()`：是否在启动、是否存活、是否已连接、是否要 PIN、是否在重连、失败原因、保活 fps；
  `remoteControlRequested()` 是给 8 ms 手柄定时器用的轻量查询。

**手柄**

- `ControllerInput::setFollowLastPressed`：打开全部手柄，谁按键就读谁。
  - 切换时关掉旧手柄的震动和扳机效果、陀螺仪，清空触摸和陀螺仪状态；
  - 只在“有焦点或允许后台”时切换；
  - 当前手柄拔掉后改读另一个已打开的手柄；
  - 关闭跟随时只保留当前手柄，行为与原来相同。
- `Capabilities.name`：当前手柄名。
- **共享手柄引用计数（顺手修的原有隐患）**：PS5 串流、PC 串流、Xbox 和 PS5 手柄控制共用一个手柄读取器。
  - 原来每个来源的定时器在自己结束时直接 `stop()`；切换来源时，旧来源的定时器可能把新来源刚打开的手柄关掉。
  - 现在每个来源 `acquirePad` / `releasePad`，最后一个释放才停；只有 PS5 手柄控制在用时打开跟随。

**桥接与界面**

- `ps5Control` 属性：可用、开关、后台、状态（off / starting / connected / pin / recovering / failed / nopair）、原因、保活 fps、手柄名、陀螺仪。
- `ps5ControlSet("enabled" | "background", bool)`：偏好 `ps5CaptureControl` / `ps5ControlBackground`。
- `ps5ControlRestart()`；登录 PIN 沿用 `ps5SendLoginPin`。
- 启动时机：
  - `startCaptureSession` 时启动，与采集卡打开并行；
  - 正在 PS5 串流时不在这里启动：串流还占着名额，等采集卡出画后由轮询启动；
  - 开关在采集卡还在打开时拨开的，也由轮询在出画后启动；
  - 失败的会话保持“已请求”，轮询不会反复重连。
- 换主机（任一对话框）时，正在运行的 PS5 手柄控制换到新主机。
- 只有 Playing / Paused 算采集卡在播：“断开”后采集任务拆除期间仍是 `running`（Stopping），
  轮询曾在 35 ms 后又把 PS5 手柄控制连上（GUI 测试发现，已修）。
- 测试开关（只认 `VEYRA_TEST_` 前缀）：
  - `VEYRA_TEST_PS5_CONTROL_HOST`：用一个假配对连这个地址，完全不读已保存的配对，测试只用 192.0.2.1（RFC 5737 文档地址，不可路由）；
  - `VEYRA_TEST_PS5_CONTROL_STATE`：只改显示的状态，用来截没有主机时到不了的状态；
  - `VEYRA_TEST_PS5_CONTROL_ANY_SOURCE`：任何在播的片源都当作采集卡，不用接采集卡就能跑完整的开启/失败/重连/断开流程。
- 文案：新增 38 条繁中/英/日；顺手补齐 latency-min 分支留下的 5 条显示同步状态翻译。
  `scripts/i18n/extract.py --check`：缺失 0、占位符问题 0。

## 测试

| 项目 | 结果 | 证据 |
|---|---|---|
| 全部目标构建 | 通过（70 秒）；新增代码无警告，已有警告仅来自 MSVC `<utility>` 和 FFmpeg 头文件，与之前构建相同 | `build-20261014-222329` |
| 远程游玩边界测试 | 通过：非法主机拒绝 ×3、控制专用会话失败后 `alive=false`、手柄队列与失焦清空 | `tests-ps5control/boundarydefault.log` |
| 200 次控制专用连接/关闭 | 通过 | `boundary--control-stress.log` |
| 谁按键就用谁（两个 SDL 虚拟手柄） | 通过：按 B 读 B、按 A 读 A、无焦点不切换、拔掉 A 改读 B、关闭跟随后不切换 | `boundary--follow.log` |
| 原有虚拟手柄测试 | 通过 | `boundary--virtual.log` |
| UI 合同、repair | 通过（384 个布局用例；259/0） | `tests-ps5control/` |

GUI（`tmp/claude-fg-gpu-home-20261014/ps5c_gui.py`；每次都设了 192.0.2.1，本机保存的真实配对只被读出显示，没有用来连接）：

| 场景 | 结果 | 证据 |
|---|---|---|
| 开关关/开、未播放 | 关闭时一行；打开后主机（共用配对）、“未连接”、后台转发、手柄“—”、说明 | `ps5c-dialog-234905` |
| 播放中打开 → 失败 → 重连 → 断开 | 开关后 3 ms 会话已建立（界面线程不等）；5 s 后 Chiaki 连接超时，“已停止”+“重连”按钮+原因（终止码 2 / 错误 2002）；重连显示“连接中”；断开后回到“未连接”且不再自动重连 | `ps5c-live-235225`（修复前 `ps5c-live-234918` 断开后 35 ms 被重新连上） |
| 需要 PIN / 已连接（强制显示） | PIN 行与提交按钮、“需要 PIN”；“已连接”绿色标签、后台转发开 | `ps5c-pin-234950`、`ps5c-connected-235004` |
| PS5 串流对话框 | “仅观看”下方“画面用采集卡”行 +“采集卡设置” | `ps5c-ps5dialog-235326` |
| 主页 | 采集卡卡片右上“PS5 手柄”标签 | `ps5c-home-235337` |
| 英文 | 分区文字换行正常；按钮改为“Pair…” | `ps5c-en-235714`（第二张） |
| 正在连接时退出 | 会话取消（cancelled=true），关闭到进程退出约 0.26 s，退出码 0 | `ps5c-exit-235457` |

回归（`tests-ps5c-235848`、`ps5c-*-23*/00*`）：

- repair 259/0、UI 合同、realtime preview 0 失败；
- 节奏 4 项（6X 回放垂直同步、屏幕 XeSS、屏幕 DLSS 4X、生命周期）全部通过；
- `veyra_screen_capture_tests` 仍是那 5 项 HDR 桌面 RGB 颜色检查失败（原版代码同样失败）。

测试包 r6：`tests/claude-fg-gpu-home-20261014/Veyra-2.0.7-test-r6-NVIDIA`（提交 a7d8598，EXE `eeec127d…`，
回执 `reports/claude-fg-gpu-home-20261014/package-r6.json`）。冒烟：专业/节点两页退出码 0、无 QML 问题、约 60 fps。

## 没做 / 待验证

- 没有真 PS5 可测：保活画面真实码率、HDMI 输出在远程游玩期间是否保持原分辨率/HDR/120Hz、手柄延迟、登录 PIN、RP_IN_USE 退避，都只能由用户或反馈者实测。
- 没有接入真 ViGEm 虚拟手柄测“谁按键就用谁”，用的是 SDL 虚拟手柄。
- `LivePresentationTests` 的 PS5 用例会连接本机已保存的真实配对，没有运行。
- DualSense 触觉音频在同时连着多个手柄时不转发（原有限制：要求只有一个手柄）。
