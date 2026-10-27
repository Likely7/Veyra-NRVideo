# 采集卡音频设备选择修复（2026-09-14）

## 结论

Veyra 原先只枚举 `CLSID_AudioInputDeviceCategory` 下的独立音频 filter，连接时也强制绑定独立音频 filter；没有尝试从用户选中的视频 filter 查找内置音频 pin。部分采集卡把 HDMI 音频作为视频 filter 的输出 pin 暴露，OBS 可以通过“使用视频设备音频”打开，Veyra 因此不会在音频列表中出现对应项。

## 本次修复

- 新增视频/音频设备详细枚举，保留 FriendlyName，并读取 DirectShow DevicePath。
- 视频设备枚举时探测其音频输出 pin；UI 对每个已选视频设备都提供“尝试视频设备内置音频”，预扫描成功时标为“已检测”，避免驱动延迟暴露 pin 时被预扫描结果误拒绝。
- 内置音频直接复用已加入 graph 的视频 filter；没有把同一个 filter 重复 AddFilter 或失败时误 RemoveFilter。
- 独立音频设备继续支持，但新 UI 通过 DevicePath 绑定，不再依赖刷新后的全局整数序号。
- 新 UI 使用 `capture2:` 连接串保存视频/音频 DevicePath；旧 `capture:` 连接串继续兼容。
- 音频 pin 查找优先 `PIN_CATEGORY_CAPTURE`，再回退到未提供 pin category 但确实是音频输出的驱动 pin。
- 记录音频绑定模式、pin/media type、PCM 可解析性和 ConnectDirect HRESULT，便于区分“未枚举”和“连接失败”。
- `veyra_capture_tests --list` 现在同时输出视频内置音频标记、视频 DevicePath 和独立音频设备。

## 实际验证

构建命令：

```powershell
.\scripts\build.ps1 -Root (Get-Location).Path -Preset x64-release -BuildDirectory (Join-Path (Get-Location).Path 'out\build\audio-device-fix') -FfmpegRoot 'C:\veyra-deps\ffmpeg-ps5-dav1d-installed'
```

结果：exit 0，`veyra.exe`、`veyra_capture_tests.exe` 及相关库均完成链接。

针对性测试：

- `veyra_capture_tests.exe --list`：exit 0；本机输出 `USB3 Video embeddedAudio=0`，并输出独立 `USB3 Digital Audio`。
- `veyra_capture_audio_tests.exe --jitter`：PASS，500 个 10ms PCM 输入块，追加 reset/underrun 均为 0。
- `veyra_capture_audio_tests.exe --jitter --5.1`：PASS，6 声道输入路径通过。
- `veyra_ui_contract_tests.exe`：PASS。
- `veyra_capture_color_tests.exe`：PASS，failures=0。

## 未验证与边界

- 反馈者的具体采集卡、驱动和 OBS 选择路径尚未在本机复现；不能据此宣称该实卡已经验收。
- 本机设备是“独立 USB3 Digital Audio”路径，没有可供 Agent 实际启动的内置音频卡用于端到端验证；内置路径已编译并由同一 pin 查找/连接逻辑接入。
- Veyra 当前仍只接收可解析的 PCM；Dolby/DTS/其他压缩音频不会因本修复自动获得支持，失败会保留视频并写出详细日志。
- 未修改驱动、SDK、运行时 DLL、模型或发布资产；未 push、未发布。
