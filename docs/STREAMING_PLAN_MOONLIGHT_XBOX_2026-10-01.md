# Moonlight 与 Xbox 串流接入方案（调研 + 设计，2026-10-01）

> 状态：**只是调研和方案，没有写任何代码，没有连接过任何主机、主机账号或主机设备。** 信息来自公开仓库与网页（文末来源）。凡是第三方博客里的数字都标了"第三方说法"，不是本机实测。
> 读者决定三件事：先做哪个、怎么排页面、Xbox 的账号风险要不要担。见第 9 节。

## 0. 结论

| | Moonlight（PC 串流） | Xbox 串流 |
|---|---|---|
| 能不能接入 | **能，而且顺**。协议成熟，核心库是 C、GPLv3，与软件同许可 | **能做，但风险高、不稳**。官方没有开放协议，全靠社区逆向 |
| 主机端 | 用户的电脑装 Sunshine（开源，GPLv3） | Xbox 主机本身，无需装东西，但要开远程功能 |
| 延迟 | 局域网第三方说法 5–15 ms；软件这边能做到"解码完直接进处理链" | 走 WebRTC，延迟取决于实现方式，没有可靠数据 |
| 视频编码 | H.264 / HEVC / AV1，含 10-bit HDR | 目前开源实现只处理 H.264（High 档优先） |
| 登录 | 局域网 PIN 配对，不碰任何账号 | 要登录微软账号，官方没给第三方的登录通道 |
| 建议 | **先做**，约一条完整的垂直功能 | **Moonlight 做完后再评估**，先做一个小型可行性验证 |

最重要的一条产品判断：大多数人玩主机用的是采集卡，采集卡路径已经做到内部延迟 1 ms 左右。Xbox 串流的价值是"不用采集卡"，不是"更低延迟"。Moonlight 的价值更实在：电脑游戏（含 4K60）串到另一台电脑上，画面进入软件现有的处理链，和采集卡、PS5 串流一样使用同一套效果器。

**范围说明（2026-10-01 按用户纠正）：接入的只是"串流核心"（协议、解码、音频、输入、主机与游戏选择界面）。NR / SR / 补帧 / 调色是软件现有的同一条处理链，开关、默认值（补帧本来就默认关）、预设、延迟提示全部沿用现有界面，不为串流另设预设或开关。**

## 1. Moonlight / Sunshine 调研

### 1.1 它是什么

- **Moonlight** 是 NVIDIA GameStream 协议的开源客户端；GameStream 停止后，社区做了开源主机端 **Sunshine**，说同一套协议，支持 NVIDIA / AMD / Intel 编码器。
- 核心库 **moonlight-common-c**：GameStream 协议的 C 实现，**GPL-3.0**；自带 ENet 分支（可靠 UDP 控制通道）和 nanors（FEC 纠错）。README 特别警告不能链接系统的其它 ENet，否则连新版主机会崩。
- 官方客户端 **moonlight-qt**：C++/Qt，**GPL-3.0**。和软件同为 Qt，目录里 `app/backend` 有配对、发现、主机列表的全部逻辑（`nvhttp.cpp` 19.5 KB、`nvpairingmanager.cpp` 13.6 KB、`computermanager.cpp` 36.5 KB 等），`app/streaming` 是会话、音视频、输入。
- Sunshine 2026-05 出了新版（Vulkan 编码等），另有社区分支（如 foundation-sunshine）加强了 HDR 与 AMD 编码。兼容性按"说同一协议"估计，实现前要逐个验证。

### 1.2 库的接口（读自 `Limelight.h`）

- 启动：`LiStartConnection(服务器信息, 串流配置, 连接回调, 视频回调, 音频回调, …)`，同步执行、非线程安全；可用 `LiInterruptConnection` 中断。
- 视频回调 `submitDecodeUnit` 给的是 **Annex-B 码流**（SPS/PPS/VPS/图像数据缓冲链）。每个单元带：帧号、帧类型（IDR/P）、`frameHostProcessingLatency`（主机处理延迟，0.1 ms 单位）、`receiveTimeUs`、`enqueueTimeUs`、`presentationTimeUs`、RTP 时间戳、HDR 标志与色彩空间。解码不了就返回 `DR_NEED_IDR` 请求关键帧。
- 有 **拉取模式**（`CAPABILITY_PULL_RENDERER`：`LiWaitForNextVideoFrame` / `LiCompleteVideoFrame`）：渲染线程自己取最新帧，不需要库里再排一层队列。另有 `CAPABILITY_DIRECT_SUBMIT`（在接收线程直接提交，要求回调不阻塞）。
- 编码格式位：H.264、H.264 4:4:4、HEVC Main / Main10 / 4:4:4、AV1 Main8 / Main10 / 4:4:4。
- 串流配置：宽高帧率、码率、包大小、是否远程、音频声道、`supportedVideoFormats`、色彩空间与范围、加密开关。
- 输入：鼠标相对/绝对、键盘、最多 16 个手柄（Sunshine 15）、手柄到达事件（带类型与能力位）、陀螺仪/加速度、触控、手写笔；震动、扳机、LED、自适应扳机通过连接回调回传。
- 统计：`LiGetEstimatedRttInfo`（往返时间）、`LiGetRTPVideoStats` / `LiGetRTPAudioStats`（丢包与纠错计数）、HDR 元数据。
- **库不做的事**：配对、主机发现、`/serverinfo` `/launch` `/resume` `/cancel` 这些 HTTP(S) 调用。这些在 moonlight-qt 的 backend 里，需要 OpenSSL（证书、RSA、AES）。

### 1.3 延迟

- 第三方说法：局域网 5–15 ms（主机采集 + 编码 + 网络 + 解码），对比 Parsec 10–20 ms、Steam 远程畅玩 10–25 ms；硬件解码能把解码从约 8 ms 压到约 2 ms。**这些不是本机实测。**
- 主机端编码（NVENC/AMF/QSV）约几毫秒，网络（有线）约 1–2 ms，这两块软件管不了。软件能管的是：接收后的排队、解码、上传、处理链、呈现。

### 1.4 与软件的契合点

1. **解码可直接复用**：`CaptureCompressedDecoder` 已经用 D3D12VA 解 H.264 / HEVC / AV1 / VP9，失败回落软件；输入正是 Annex-B。解码后的 GPU 表面直接进处理链，没有 CPU 拷贝。
2. **会话结构有现成模板**：PS5 的 `RemotePlaySessionSource`（后台线程、遥测、音频 WASAPI、断线恢复），新的来源类照着写。
3. **构建有现成模板**：`VEYRA_ENABLE_REMOTEPLAY` + `cmake/VeyraRemotePlay.cmake`（固定版本第三方源码 + 来源校验脚本）。PS5 那套已经带了 OpenSSL 和 Opus，Moonlight 要的加密和音频解码都在。
4. **手柄层有现成模板**：`ControllerInput`（SDL3）。目前语义按 PS 按键，要加一层通用映射。
5. **许可**：GPLv3 对 GPLv3，可以直接移植，义务同以前：逐项记录来源仓库、固定提交、改动，写进 `THIRD_PARTY_NOTICES.md`。

## 2. Xbox 调研

### 2.1 官方与开源现状

- 微软没有开放 Xbox 远程游玩协议。现有第三方客户端都是**逆向**出来的，同一套叫 xHome（家庭串流）/ xCloud（云游戏）。
- 主要开源项目：
  - **Greenlight**（unknownskl，MIT，TypeScript，Electron 类桌面程序）：包装 `xbox-xcloud-player`。
  - **xbox-xcloud-player**（同作者，`package.json` 写 MIT，但 GitHub 没识别出 LICENSE 文件；**复制代码前要向作者确认**）：WebRTC 客户端库，依赖浏览器的 `RTCPeerConnection`。
  - **Kite**（Rust + Tauri，MIT）：Windows 客户端，WebRTC 仍在 WebView 里，用 Insertable Streams 把已编码视频取出来交给 Rust 做录像。其自带技术文档和 README 互相矛盾（文档说没做输入，README 说做了），只能当线索。
  - **XStreaming**（移动端）、**blacklight**（Tauri + Svelte）、**GreenOvercast** 等。
- 这些项目**没有一个是原生（非浏览器）的 WebRTC 实现**，都借用浏览器引擎的 WebRTC。

### 2.2 协议要点（来自上述项目）

1. 登录链：微软 OAuth → Xbox Live 令牌 → XSTS 令牌（`gssv` 受众）→ 请求头 `XBL3.0 x=用户哈希;令牌`。
2. 信令走**微软的服务器**（即使在同一局域网）：`GET /v2/login/user` 列主机；`POST /v5/sessions/home/{serverId}/play` 建会话，返回 SDP offer；`/sdp` 回 answer；`/ice` 交换候选。
3. 媒体走 WebRTC（ICE 点对点或中继）：一路视频、一路音频，外加 **4 条数据通道**：`input`（手柄/键盘）、`control`、`message`、`chat`。
4. 视频编码：`xbox-xcloud-player` 的编码偏好只处理 H.264（High 优先，其次 Baseline）加 VP8/VP9，**没见 HEVC/AV1 处理**。这只是这个库的偏好，不等于主机不支持别的。
5. 分辨率帧率：通用说明是家庭串流最高 1080p60（不是这些项目的实测）。
6. 主机要求：设置里开启远程功能；睡眠模式选"即开"才能被唤醒；从外网用要上行约 15 Mbps、NAT 最好是开放。

### 2.3 三个必须正视的问题

- **登录没有合规通道**。Kite 要求**每个用户自己在 Azure 注册应用**（设备码登录），项目不带共享的客户端 ID；其说明里没有提账号风险。另一类做法是冒用微软自家 App 的客户端 ID（Greenlight 这一类的做法，我没核实）。两种都不理想：前者用户门槛高，后者有账号/条款风险。**这是 Xbox 能不能做的第一道关。**
- **协议随时会变**，而且完全不受我们控制。微软一改，功能就坏，且没有官方渠道通知。
- **条款**：各项目只写了"不隶属于微软，仅限自己的账号和主机、遵守微软服务协议"，没做法律评估，我也不做。产品里只能标"实验功能 / 非官方"，并由你承担是否发布的判断。

## 3. 方案取舍

### 3.1 先 Moonlight，后 Xbox

理由：Moonlight 风险低、收益明确、可本机闭环验证；Xbox 的"登录 + 协议稳定性"两个问题不先解决，做多少都可能白做。Xbox 只做一个小型可行性验证（第 8 节 X0），通过再立项。

### 3.2 不要一次性重构 PS5 的串流代码

PS5 路径已经稳定，串流类型绑在 `RemotePlayConnectDesc` 上。新协议**另写一个并列的会话来源**，先复制这套模式，等 Moonlight 稳定后，再评估抽出公共的"串流会话"接口。避免为了整洁去动已验证的 PS5。

## 4. Moonlight 技术设计

### 4.1 模块

```
src/moonlight/
  MoonlightHttp      HTTP(S) 调用：serverinfo / pair / applist / launch / resume / cancel
  MoonlightPairing   PIN 配对、客户端证书（移植自 moonlight-qt 的 nvhttp/nvpairingmanager）
  MoonlightDiscovery mDNS（_nvstream._tcp）+ 手填 IP
  MoonlightSession   封装 LiStartConnection、回调、统计
  MoonlightSource    实现 IFrameSource（像 RemotePlaySessionSource）
  MoonlightInput     手柄/键鼠 → Li* 输入函数
third_party/moonlight-common-c（固定提交 + ENet + nanors，带来源校验脚本）
cmake/VeyraMoonlight.cmake，选项 VEYRA_ENABLE_MOONLIGHT
```

### 4.2 数据流与线程

```
Sunshine ──UDP──▶ moonlight-common-c ──Annex-B──▶ 接收线程（不阻塞）
   ▲                                                   │ 最新帧邮箱（容量 1）
   │ 输入(ENet)                                         ▼
MoonlightInput ◀── 输入线程（≥250 Hz）       解码线程：D3D12VA（已有解码器）
                                                        │ GPU 表面，0 次 CPU 拷贝
                                                        ▼
                                           EnhanceGraph（NR/SR/补帧，可全关）──▶ 呈现
音频：Opus 多声道解码 ──▶ WASAPI（10 ms 缓冲，沿用采集路径的经验）
```

### 4.3 低延迟设计（沿用这几轮的经验）

1. **每一帧都立刻解码，丢帧放在"解码之后"**。解码后的表面丢掉很便宜；在码流层丢 P 帧会打断参考链，逼主机重发关键帧（`DR_NEED_IDR`），一次丢帧变成一次画质冲击。
2. **丢帧不标"不连续"**。这几轮查出来的教训：把丢帧标成时间线断点，会让 NR/补帧的历史被整个重置。只有真实的解码错误或码流缺口才算断点。
3. **优先用拉取模式**（`LiWaitForNextVideoFrame`），引擎线程自己拿最新帧，库里不再多排一层。
4. **"解码完成"的 GPU 围栏直接唤醒引擎线程**（这次采集路径已经这样做，目前只对物理采集生效，要扩到串流）。
5. **效果器不属于串流接入**：串流只是一个新来源，解码后的画面以和采集卡、PS5 相同的方式进入处理链。处理链的开关、默认值（补帧本来就默认关）、预设、延迟提示都沿用现有界面，不为串流另设。接入要保证的只是：来源不额外增加排队，处理链的延迟行为和采集卡路径一致。
6. **延迟分解面板**：主机处理（库给的 `frameHostProcessingLatency`）+ 网络 + 接收排队 + 解码 + 软件处理 + 呈现。这也是验收依据。

### 4.3.1 验收基准

同一主机、同一分辨率帧率编码，对比官方 moonlight-qt：**软件"接收到呈现"的中位数不高于它 + 1 ms（处理链全关时）**。这个数字要在 S2 实测后再确认合理性。

### 4.4 HDR 与编码

- HEVC Main10 / AV1 10-bit PQ 走硬件解码的 P010 表面，进现有的 HDR 合同（PS5 的 `H265Hdr` 已有先例）。**软件解码路径目前只输出 8-bit NV12**，所以 HDR 要求硬解，硬解失败必须明确提示，不静默降成 SDR 假装正常。
- 主机 HDR 状态走 `setHdrMode` 回调和 `LiGetHdrMetadata`；要核对与 Windows HDR 开关的关系。
- 先做 H.264 / HEVC，AV1 与 HDR 放到 S5。
- **4K60**：`STREAM_CONFIGURATION` 直接传宽高帧率，协议没有固定上限，官方 Moonlight 提供 4K 与 60/120 fps 选项。实际能否跑满取决于主机编码器、网络带宽和本机解码；本机 4K60 HEVC/AV1 硬解的耗时没测过，S2 里实测。4K 画面进处理链的开销与 4K 采集卡相同，不是串流特有的问题。

### 4.5 输入

- **手柄**：SDL3 读取，**独立输入线程**（现有 `ControllerInput` 的 SDL 调用在界面线程，串流要更高频、更稳）；`LiSendControllerArrivalEvent` 声明能力，震动/扳机由连接回调回到手柄。陀螺仪要主机明确要求才发。
- **键鼠**：捕获模式下用 Raw Input 读相对鼠标；QML 窗口里视频窗口是穿透的，输入在主窗口侧拿。捕获期间软件自己的快捷键会和游戏冲突，所以约定：捕获时只保留一组带修饰键的保留键（沿用 Moonlight 习惯：Ctrl+Alt+Shift+Z 释放鼠标，Ctrl+Alt+Shift+Q 退出串流），F11、Home、Alt 等全部交给游戏；释放捕获后恢复。
- 全屏"Home 快速调节面板"在串流里同样可用（释放捕获时），这是软件的独有优势：边玩边调 NR/SR。

### 4.6 安全与存储

- 配对产生的客户端证书和私钥、主机密钥：DPAPI 加密保存在 `%LOCALAPPDATA%\Veyra\moonlight`，与 PS5 凭据同一原则；日志不得出现密钥、PIN、证书。
- 只在局域网自动发现；手填地址时校验格式（沿用 PS5 的 `validHost` 思路）。
- 远程（外网）串流先不做；库有 `streamingRemotely` 开关，留到 S6。

### 4.7 引擎接入点

- `IFrameSource` 直接实现；`SourceReadStatus::Waiting` 用于"暂时没有新帧"。
- `EngineController::run(..., remoteRequest)` 现有的 `isRemote` 分支按 PS5 写，要么扩成"串流来源"，要么加一条并列分支；**评估后决定，不在方案阶段拍板**。
- 引擎里和采集相关的几项延迟优化（围栏唤醒、背压）当前部分只对物理采集生效，要逐项检查哪些应该对串流生效。

## 5. Xbox 技术设计（仅在 X0 通过后）

### 5.1 三种做法

| | A 内嵌网页引擎 + 取编码帧 | B 原生 libdatachannel | C 原生 libwebrtc |
|---|---|---|---|
| 做法 | 用 WebView2 / QtWebEngine 跑现成的 JS 协议库，用 Insertable Streams 读出已编码的视频帧，丢给软件自己的硬解，不让浏览器解码 | 用 C++17 的轻量 WebRTC 库自己收 RTP、自己做抖动缓冲和关键帧请求 | 用谷歌的完整 WebRTC 栈，注入自己的解码器 |
| 优点 | 最快出原型；协议细节都在现成 JS 里 | 延迟最可控，包小，最贴合处理链 | 抖动缓冲、带宽估计、丢包恢复都现成 |
| 缺点 | 多一层浏览器运行时和跨进程传输；JS 里的一跳增加不可控延迟 | 缺带宽估计和抖动缓冲，要自己写；和微软服务器互通性未知；**许可 MPL-2.0（我凭记忆写，使用前要核对）** | Windows 上体积和构建都很重 |
| 判断 | 用来做 X0 的可行性验证 | **量产首选，前提是 X0 证明互通** | B 不通时的后备 |

- 现成数据点（第三方）：另一个开源桌面串流项目用浏览器解码约占 33 ms（总约 50 ms），其路线图里的原生客户端就是为了压这一段。说明浏览器解码确实是延迟大头，做法 A 必须绕开浏览器解码。
- 解码之后（硬解 → 处理链）与 Moonlight 完全相同，共用同一套实现。

### 5.2 X0 可行性验证要回答的问题（不写产品代码）

1. 登录：有没有一条不冒用微软第一方客户端 ID、又不要普通用户自己注册 Azure 应用的办法？（没有就停。）
2. 互通：B 方案（libdatachannel）能否与微软的信令与 WebRTC 端点完成 SDP/ICE/DTLS/SCTP 握手，并收到视频。
3. 编码：实际协商出的是什么编码、分辨率、码率。
4. 延迟：收到 RTP 到出画的耗时，与做法 A 对比。
5. 稳定：关键帧请求、断线重连、主机唤醒。

**X0 需要你本人在场**：用你自己的微软账号和 Xbox 主机，由你授权并操作登录；我不会代为登录、不会碰账号令牌。

### 5.3 输入与账号存储

- 手柄包格式从 `xbox-xcloud-player` 的输入通道代码读取（MIT，先确认许可）；同样走独立输入线程。
- 令牌用 DPAPI 加密存盘，日志脱敏；提供"退出登录并清除"。

## 6. 前端方案

### 6.1 总体原则

- 沿用软件现有的设计语言：`VPage`、`VCard`、`DLayer` 对话框、`VRow`/`VSelect`/`VSeg`/`VSwitch` 设置行、`PerfOrbs`；**不新造控件**，除非必须（主机卡片、游戏卡片是新的两个组件）。
- 串流前的"设置"和串流中的"观看"分开：设置在页面/对话框里做；一旦连上，立刻进入现有的播放页（极简 / 专业 / 节点都能用），不另造播放界面。
- 上面的线框图是**版式示意**，不是最终配色和尺寸。

### 6.2 页面与流程

**① 首页**（`HomePage.qml`）
- 来源卡片现在是 4 张（打开视频、采集卡、PS5 串流、屏幕捕获），一排 164 px 宽。加两张变 6 张，共约 1044 px，在 1280 窗口内放得下，窄窗口换成两行。
- 新卡片：**PC 串流**（副标题显示"已保存 N 台主机"或"Sunshine 主机"）；**Xbox 串流**（标"实验"，仅在设置里打开"实验功能"后显示）。
- 另一种做法：合成一张"游戏串流"卡，点开后选 PS5 / PC / Xbox。好处是首页干净，坏处是改变 PS5 现有入口。**默认建议加卡片、不动 PS5**，见第 9 节。
- "继续上次"那一行已经有 `lastSource`，新增 `moonlight` 类型，点一下直接重连上次的主机与游戏。

**② PC 串流页**（新页面，三栏，对应线框图 2）
- **左栏 主机列表**：自动发现 + 手填 IP。每台主机一张卡：名称、地址、状态点（在线 / 未配对 / 忙 / 离线）。操作：配对、删除、刷新。
- **配对对话框**：软件生成 4 位 PIN 显示出来，提示"到主机的 Sunshine 网页（本机 47990 端口）的配对页输入这个 PIN"；配对成功自动关闭，失败给出可操作的原因。
- **中栏 游戏网格**：选中主机后拉取游戏列表，显示封面（复用现有的缩略图服务）；正在运行的游戏带"运行中"标记，操作有"继续"和"退出游戏"。首项常是"桌面"。
- **右栏 串流设置**：只放串流核心参数——分辨率与帧率（列出档位，含 4K 60）、码率（默认自动，可手调）、编码（自动 / H.264 / HEVC / AV1）、HDR、音频声道（立体声 / 5.1 / 7.1）、手柄（按键映射、仅观看）。效果器不在这里，连上后用软件现有的页面调。底部一个主按钮"开始串流"。
- 设置按主机分别保存。
- 首次使用、没有主机时，主区域显示"怎么装 Sunshine"的三步引导，带官网链接；软件不捆绑 Sunshine。

**③ 串流中**（复用播放页，对应线框图 3）
- **顶部胶囊**（`TopDock`）在串流时多一个延迟小胶囊：往返时间、丢包率、鼠标捕获状态和释放键。
- **专业 / 列表模式**的耗时区多一块"串流延迟分解"：主机 / 网络 / 接收 / 解码 / 软件处理，一条彩色分段条加各项毫秒（线框里的数值只是示意）。
- **控制条**（`CineBar`）新增：断开、退出主机上的游戏、释放鼠标。断开和退出是两个不同动作，文案要写清"断开后游戏继续运行"。
- 全屏 Home 快速调节面板照常可用。

**④ 设置页新增"串流"分组**
默认预设、统计显示默认开关、保留键、清除所有配对和证书。

**⑤ Xbox 对话框**（X0 之后）
登录状态 → 主机列表（含电源状态、唤醒按钮）→ 分辨率 / 码率 / 手柄 → 连接。顶部固定一条"非官方实验功能，仅限自己的账号和主机"的提示。

### 6.3 桥接接口（沿用 `ps5*` 的写法）

```
veyra.moonlight            状态对象：hosts、active、busy、status、stats
moonlightScan() / moonlightAddHost(addr)
moonlightPair(hostId) → PIN 文本 / 结果信号
moonlightApps(hostId) → 游戏列表
moonlightConnect(hostId, appId, options) / moonlightDisconnect(quitApp)
moonlightSet(key, value)   设置按主机保存
moonlightForget(hostId)
```

### 6.4 测试

- QML 组件测试（与现有 `tst_components.qml` 同路）：主机卡状态、配对对话框流程、游戏网格"运行中"、设置行。
- 用录制的 `serverinfo` / `applist` XML 固件做协议解析与配对的无网络单元测试。
- 真实截图检查（和这几轮一样，按页面逐屏对照线框）。

## 7. 风险与未知

| 风险 | 影响 | 处理 |
|---|---|---|
| Sunshine 各分支对协议的扩展不一致 | 个别主机连不上或缺功能 | 以官方 Sunshine 为基线，其它分支单独列兼容清单 |
| 输入捕获与软件快捷键冲突 | 误触发或按不了 | 保留键方案 + 释放捕获时才响应软件快捷键 |
| HDR 路径依赖硬解 | 部分显卡无法 HDR 串流 | 明确提示，不静默降级 |
| D3D12VA 对 HEVC/AV1 的支持因显卡而异 | 部分显卡只能 H.264 | 按能力选择并告知 |
| Xbox 登录方式 | 可能整个停摆 | X0 先验证，不通就不做 |
| Xbox 协议被微软改动 | 功能随时失效 | 标实验功能，版本里写明 |
| 本机只有一张 RTX 5070 | 其它显卡（解码、HDR）未验 | 如实标"未验"，由持卡用户验收 |
| 我无法在没有用户在场时完成的验证 | 实际串流、Xbox 登录 | 明确列为需用户在场的项 |

## 8. 分阶段计划

工作量用相对大小（S 小、M 中、L 大）表示，不写具体周数，因为取决于验证中遇到的问题。

### Moonlight

| 阶段 | 内容 | 交付与验收 | 需要用户 |
|---|---|---|---|
| **S0** 准备 | 定范围；固定 moonlight-common-c 与 moonlight-qt 的提交；核对各依赖许可；写入 NOTICES 草稿；准备一台 Sunshine 主机（可以是你的电脑本机回环） | 决策清单确认；来源校验脚本 | **同意下载并安装 Sunshine**（下载须你授权） |
| **S1** 协议层 (M) | 移植 HTTP/配对/发现；固件单测 | 无网络单测全过；与本机 Sunshine 成功配对、列游戏 | 在 Sunshine 网页输入 PIN |
| **S2** 会话与画面 (L) | 会话来源、解码、音频、遥测、引擎接入 | 处理链全关时，延迟对比 moonlight-qt（第 4.3.1）；延迟分解日志 | 无（本机回环即可） |
| **S3** 输入 (M) | 手柄、键鼠捕获、保留键、震动 | 本机回环下手柄/键鼠能控制；输入线程频率实测 | 你在场操作输入 |
| **S4** 前端 (M–L) | 首页卡、PC 串流页、配对对话框、设置、串流中 UI | QML 测试通过；逐屏截图与线框对照 | 你确认版式 |
| **S5** 质量 (M) | HDR、AV1、4K60 实测、丢包与重连、睡眠恢复、1 小时稳定性 | 各项实测记录；未验项如实列出 | 有第二台电脑或不同显卡更好 |
| **S6** 可选 | 外网串流 | — | — |

### Xbox

| 阶段 | 内容 | 判定 |
|---|---|---|
| **X0** 可行性验证 (S–M) | 第 5.2 的五个问题；一个一次性的验证程序，不进产品 | 登录方案和 libdatachannel 互通两项任一不通，则停止并如实汇报 |
| **X1+** | 原生会话、输入、前端（按 X0 结论定，前端对应第 6.2 ⑤） | 另行立项 |

## 9. 需要你决定的事

1. **顺序**：是否同意先做 Moonlight，Xbox 只做 X0 验证？
2. **首页**：加卡片（6 张、不动 PS5），还是合成一张"游戏串流"卡？我默认建议加卡片。
3. **Sunshine**：S0 要下载安装 Sunshine 做本机回环测试，你是否同意我下载？（我不会自己去下载。）
4. **Xbox 风险**：你是否接受"非官方、依赖逆向协议、登录方式未定"作为立项前提？以及 X0 需要你本人用自己的账号和主机在场操作，是否接受？
5. **许可**：`xbox-xcloud-player` 缺 LICENSE 文件，使用其代码前是否由你联系作者确认？（仅在 X0 通过后才需要。）

## 10. 来源

- moonlight-common-c：https://github.com/moonlight-stream/moonlight-common-c ；接口头文件 `Limelight.h`（读取于 2026-10-01）
- moonlight-qt 源码结构：https://github.com/moonlight-stream/moonlight-qt
- Sunshine（LizardByte）：https://app.lizardbyte.dev/Sunshine/ ；社区分支 foundation-sunshine：https://github.com/AlkaidLab/foundation-sunshine
- Greenlight：https://github.com/unknownskl/greenlight ；xbox-xcloud-player：https://github.com/unknownskl/xbox-xcloud-player
- Kite：https://github.com/DRHATL95/kite（含 AZURE_SETUP.md、TECHNICAL_DETAILS.md）
- Xbox 远程游玩通用说明（第三方）：https://www.digitalcitizen.life/how-xbox-remote-play-works-and-why-it-feels-different-from-cloud-gaming/
- libdatachannel 与 libWebRTC 对比：https://tensorworks.com.au/blog/a-brief-comparison-of-libdatachannel-and-libwebrtc/
- 原生串流项目的延迟拆分（第三方）：https://github.com/jrf63/desktop-streaming
- 软件内的参照实现：`src/source/RemotePlaySessionSource.cpp`、`src/remoteplay/ControllerInput.cpp`、`cmake/VeyraRemotePlay.cmake`、`qml/Veyra/HomePage.qml`、`qml/Veyra/DialogHost.qml`（PS5 对话框）
