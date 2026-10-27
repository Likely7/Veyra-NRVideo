# WASAPI录音端点接入（2026-09-15）

状态：**已实现本地候选，未发布；本机USB3采集卡端点已通过静音回归，反馈者设备与听感未验收。**

用户授权“把这个加入”。在DirectShow视频采集旁新增WASAPI共享模式录音输入，保留原独立DirectShow与内置音频路径。仅本地开发，不发布、不改运行组件、不自动选择麦克风、不提供系统回录。

## 实施合同

- 枚举active eCapture端点，显示来源标签，用endpoint ID精确绑定；默认不监听，不使用GetDefaultAudioEndpoint。已保存DirectShow连接串继续兼容。
- 独立产品输入类拥有MTA采集线程、事件和COM资源；PCM交给共享CaptureAudioSession，复用重采样、音量、声道及输出。采用设备mix format，不声称原始bit-perfect或绕过Windows APO。
- GetBuffer设备时间/QPC为样本起点，不把回调到达时间冒充采样时刻；SILENT补足真实静音包，TIMESTAMP_ERROR按连续帧位置估计并明确计数，断点重置历史。GetBuffer/ReleaseBuffer同线程配对；队列、单包和重连有界。
- WASAPI源PTS映射到本进程steady-clock轴，视频在采集回调处建立PTS→host映射供呈现通知转换。只估计软件链延迟，不宣称消除HDMI音视频固有延迟；原DirectShow共图同步不变。
- 输入故障保留视频，显示HRESULT/错误；只重连同一ID，最多3次，停止可中断等待。输入重新打开时重新协商格式、创建音频epoch，不留悬空回调。切换设备仍通过关闭旧会话再打开。
- 当前媒体/音频修复继续保留，不能用新增输入路径宣称火堆/口哨沙沙声根治。

## 验证与交付门槛

离线覆盖连接串、时间戳/帧连续性、静音及异常包；枚举不启动录音；仅明确指定USB采集卡端点才做短时静音输入测试，不保存PCM。构建RemotePlay ON且保留patched FFmpeg/dav1d；重跑既有音频专项。实卡拔插、用户设备和同源听感未执行时必须标明。文档和WORKLOG记录结果，提交本地main保持工作区干净。

API依据：[GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)、[Device Formats](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats)。独立实现，不复制第三方源码。

## 本轮实际实现与证据

- 新增 `WasapiAudioInput`：枚举活动 `eCapture` 端点，绑定用户选择的 endpoint ID，使用共享模式事件采集；没有 loopback flag、默认端点选择或设备替换回退。
- 采集卡连接串仍为 `capture2:`，WASAPI 音频模式使用 `-3` 和编码后的端点 ID；DirectShow 内置/独立音频以及旧 `capture:` 均保留。UI 标签明确 `[WASAPI]` / `[DirectShow]` 来源。
- `IAudioCaptureClient::GetBuffer` 的QPC时间转换到进程steady-clock轴；设备位置跳变、数据断点和坏时间戳分别处理。静音包按帧数补零，空包、超界包和非静音空指针拒绝；`GetBuffer` 与 `ReleaseBuffer` 同线程配对。
- 设备失效只重试所选 endpoint，最多三次；不会切换到默认麦克风。停止事件可中断重试等待，恢复后重新建立PCM epoch和共享 `CaptureAudioSession`。
- 代码构建：`cmd.exe /c out\build\veyra-build-x64-release.cmd` exit 0，最终 `[30/30] Linking CXX executable veyra.exe`；首次构建的 `cguid.h` 头文件顺序错误已修正，失败日志保留在 `logs/wasapi-input-20260915/build-first.log`。
- `veyra_wasapi_input_tests.exe --offline`：时间轴、连接串、静音包和异常包 **18项通过**；`--invalid`：无效ID不回退、三次重试、停止可中断 **exit 0**。
- 本机 `USB3 Digital Audio` WASAPI endpoint（`{0.0.1.00000000}.{73a0923c-3eaf-4f12-945e-c8a0271e60b7}`）3秒静音输入收到超过20个PCM块，48kHz/32-bit float，停止并重开通过；DirectShow视频+WASAPI音频组合测试 exit 0；DirectShow原路径回归 exit 0。
- 旧音频连续性专项仍保留；本轮未重跑完整GPU delivery、RTX Create/Evaluate或用户反馈者设备。所有本机端点测试均静音，不能把通过结果写成沙沙声已根治。
