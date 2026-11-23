# Xbox：新增可选 V2 传输（WebView2 内 Chromium 参考播放器）+ HQ 档位、D3D11VA 选项、丢包恢复、串流 HUD

## 为什么

现有 Xbox 家庭串流（V1，原生 libdatachannel）可用，但在真实家庭网络下抗丢包弱于
参考客户端（XStreaming / Greenlight）；部分 Intel 核显的 D3D12VA H.264 输出会
静默花屏；用户也看不到增强链之前主机/网络实际交付了什么。本 PR 新增**可选 V2
传输**，采用与参考客户端相同的收流栈，保留 V1 作为可切换的替代，并暴露串流统计——
**不改动超分/补帧/渲染链**。

## 改了什么

- **V2 传输（`XboxWebSource`）**：离屏 WebView2 运行 vendored 的 MIT 许可
  `XStreaming-player` 构建产物。Chromium libwebrtc 负责 NACK/抖动/重排；
  `RTCRtpScriptTransform` 在解码**前**导出完整 Annex-B H.264 AU 与 Opus 包，
  零码流转换喂给现有 `CaptureCompressedDecoder`/`CaptureAudioSession`。
  超分、补帧、渲染不变。
  - transform 在 `createOffer`/`setLocalDescription`**之前**挂到 transceiver
    receiver：在 `ontrack`（协商后）挂接收端 transform 会被真实主机 SDP 静默
    绕过（Chromium bug w3c/webrtc-encoded-transform#314 / chromium 467247265）。
    导出用 `worker.onmessage`；ICE 交换等采集完成。
- **保留 V1 可选**：`IXboxSource` 抽象两引擎；`transportEngine`（1=V1、2=V2，
  默认 2）持久化并在对话框提供选择器。V1 保留 libdatachannel 及其丢包恢复。
- **HQ 档位**：1080p HQ（tizen 浏览器身份）、公网 STUN、完整 Teredo 候选展开；
  码率上限；两引擎共用固定 profile。
- **D3D11VA（DXVA）解码选项**：针对 D3D12VA 花屏的 Intel 核显；保留软解兼容开关。
- **串流 HUD**（`Ctrl+Alt+Shift+S`）：分辨率/编码/解码路径、RTT、解码 ms、码率、
  收/解 fps、丢帧、重传恢复。HUD 开关不再被 `VEYRA_ENABLE_MOONLIGHT` 门禁。
- **手柄**：共享 `GamepadBridge.h` 映射（`gamepadFromController` 的精确逆；
  Y 透传，因 Packet.ts 在线上取反；INT16_MIN 钳到 -32767）。震动经
  `ControllerFeedback` 回传。
- **构建**：`cmake/VeyraXboxWeb.cmake` + `VeyraXboxWebEmbed.cmake` 以字节数组
  内嵌播放器 bundle（C++ 构建无需 Node/Qt）。新增依赖：vcpkg `webview2`
  （静态 loader，不随附 DLL；需 Evergreen 运行时）。

## 范围 / 依赖

- `vcpkg.json`、`.gitignore`、`CMakePresets.json` 及所有现有 `VEYRA_*_ROOT`
  默认值与 main **逐字节一致**。libdatachannel/Opus/nlohmann-json/FidelityFX
  光流库均为上游既有依赖，CMake 变量相同。
- 唯一新增包是 vcpkg `webview2` port，获取方式与 libdatachannel/opus 完全相同
  （manifest 外静态安装到 `CMAKE_PREFIX_PATH`）：
  `vcpkg install webview2:x64-windows-static`。`-DVEYRA_ENABLE_XBOX=OFF` 仍为
  默认，无需新 SDK。
- Vendored 产物：`resources/xbox-player/veyra-xbox-player.js`（MIT）。出处+重建：
  `resources/xbox-player/README.md`；许可：`licenses/xbox/xstreaming-player-MIT.txt`；
  声明：`THIRD_PARTY_NOTICES.md`。base `8e178aef`，补丁 `e4c12bd`（fork 将公开）。

## 取舍 / 影响面

- V2 增加一个 WebView2 进程与 base64/WebMessage 一跳（约 1-3ms/帧）及 Chromium
  自适应 jitter buffer；换来参考级抗丢包。V1 留给偏好进程内路径的用户。
- 解码/生成帧走同一增强图；SR/FG/导出/虚拟摄像机行为不变。
- 仅 Xbox 面：不改 `remoteplay`/`moonlight`/`sink`/`beauty`/`gfx`。
  （跨切面的超分可用性迁移 + AMD 光流启用另开 follow-up PR。）

## 验证

- `veyra_xbox_tests`：**119 项 0 失败**（恢复上游 V1 loopback + 新增 HQ
  profile/resilience 用例）。`veyra_xbox_web_gamepad_tests`：**30 项 0 失败**。
- 真机（Xbox Series，H.264 D3D11VA）：V1/V2 均进入 `streaming` 且有解码帧+Opus；
  V2 光流补帧保持开启（`Create/Dispatch result=0`）；引擎切换+HUD 经
  `scripts/ui-check/xbox-*.qml` 验证。
- `tools/wv2-probe/` 离线探针确认完整 Annex-B AU（带内 SPS/PPS/IDR），base64
  通道在 1080p60@15Mbps 零丢失。

## 未覆盖

- 未做真机与 XStreaming 的端到端延迟 A/B；特定机型的观感仍取决于主机对
  不同客户端的编码决策。
- Intel Arc 原生 XeSS MFG（不打补丁的 3X/4X）不在本 PR 范围。