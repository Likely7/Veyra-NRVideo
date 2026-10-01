# 美乐威 Pro Capture 低延迟模式（2026-10-01，beta 5）

来源：群友反馈"美乐威采集卡有低延迟模式（逐行），要适配 SDK"。用户要求：做一个美乐威专用的按钮。

## 原理（美乐威官方说明）

- 普通模式：一帧先完整进采集卡内存，再整帧 DMA 到电脑内存。
- 低延迟模式：卡收到 64/128/256 行就开始 DMA，"部分通知"按块告诉软件。整帧在最后一行到达后很快就完整在内存里。
- 官方数据：1080p60 采集延迟约 21.7 → 17 ms，4K60 约 37.6 → 22.6 ms。
- 只有 **Pro Capture（PCIe）** 系列支持；USB Capture 和第一代设备不支持。低延迟占一个通道的两条传输通道之一。
- DirectShow 做不到，只能用美乐威 MWCapture SDK。

参考：<https://www.magewell.cn/developer/28/detail>，<https://www.magewell.cn/developer/15/detail>，SDK 示例 `Examples/Applications/LowLatency`。

## 实现

- `src/source/MagewellCapture.cpp`：运行时加载 `LibMWCapture.dll`（先找软件目录 `runtime\magewell\`，再找系统），
  按官方示例的顺序：`MWRegisterNotify(FRAME_BUFFERING)` → `MWCaptureVideoFrameToVirtualAddressEx(iNewestBuffering, cyPartialNotify=64)`
  → 等到 `bFrameCompleted`。缓冲区页对齐并 `MWPinVideoBuffer`。按 DirectShow 设备路径（PCI 实例）找对应的 SDK 通道，找不到且只有一块卡时用那一块，日志记录两边的路径。
- 接入现有采集卡来源（`CaptureCardSource`）：DirectShow 照常协商格式、音频照常；开了低延迟且设备是美乐威 PCIe 卡（`ven_1cd7`）、格式是 NV12/P010/YUY2/RGB32 时，
  视频改由 SDK 线程写入同一个邮箱帧，DirectShow 的视频帧被忽略。SDK 线程若启动失败或中途退出，DirectShow 视频自动接回，不会黑屏。
  时间轴接在 DirectShow 最后一帧之后继续，不跳到卡的时钟。
- 界面：采集卡对话框"设备"组新增"美乐威低延迟模式"开关。非美乐威 PCIe 设备时开关灰掉并说明；开启后提示一行状态
  （"低延迟采集中 · 一帧从开始进卡到完整进内存 x ms"，或没启用的原因）。偏好 `magewellLowLatency` 保存在用户偏好里，下次连接生效。
- 构建：SDK 头文件放在 `E:/项目/Veyra/deps/magewell/3.3.1.1596`（不进源码库），CMake `VEYRA_MAGEWELL_SDK_DIR`；没有头文件的构建里该功能报"不可用"。
- 许可：SDK 头文件和库的许可允许使用、修改、再分发，需保留版权声明和免责声明 → 测试包带原样 `runtime\magewell\LibMWCapture.dll`（3.3.1.1596，SHA-256 `360259C4…92F9`）
  和 `licenses\magewell\MWCapture-SDK-NOTICE.txt`。SDK 安装程序（未签名）没有运行，用 innoextract 1.9 只解包。

## 验证

- 本机没有美乐威卡。已验证：运行库能加载、`MWCaptureInitInstance` 成功、枚举到 0 个通道时正确拒绝并给出原因（`veyra_magewell_probe`）。
- 本机另一块 USB 采集卡（非美乐威）在开关打开时照常走 DirectShow：118 fps，0 丢帧。
- 采集对话框截图：`tests/stream-fix-20261001/capshot/`。
- **未验证（需要有 Pro Capture 卡的群友）**：通道匹配、低延迟画面、颜色、实际延迟数值、长时间稳定性、拔插与换信号、音画同步。

## 给测试者

1. 采集卡对话框选美乐威 Pro Capture 设备，格式选 NV12（或 YUY2/P010），打开"美乐威低延迟模式"，点"连接并开始"。
2. 对话框里那行状态应显示"低延迟采集中 · … ms"。
3. 关掉开关再连一次对比延迟手感；日志 `logs\veyra-qml.log` 搜 `magewell`（含通道匹配、每 600 帧一次延迟统计）。
