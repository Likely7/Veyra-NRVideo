# BMD 黑屏跟进：驱动交帧兼容候选

用户在 2.0.6 后继续报告 BMD 黑屏，提供 `veyra-qml(43).log`，并确认 OBS 的视频采集设备与 Blackmagic 来源都可显示。当前任务只修这项采集兼容，不沿用上一轮关机请求，不合并 main、不推送或发布。

## 已确认和未知

- 日志七次连接均能持续出帧，增强关闭仍黑；HDYC 已原生直连，旧 crossbar 修复在两入口都没有找到上游。38 次 YUV/RGB 黑采样、6 次 ARGB 全白采样，不能把后者当真实画面恢复。
- 上轮报告只验了合成接口、本机 USB 和 GPU 颜色，没有 BMD 真卡验收。本轮不重复宣称实卡已修好。
- 对照 OBS libdshowcapture 固定提交 `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`：其接收 pin 不提供分配器/缓冲要求，ConnectDirect 不强塞 media type，终端暴露 renderer 标记。Veyra 原生接收器主动提供通用 MemoryAllocator、3 个缓冲的要求及 N4 上游建议。此为真实接口差异，尚不是已证实现场根因。
- 卡型号、物理输入、实际信号制式仍未知；驱动 GetFormat 是配置，不能当信号检测。禁止盲切 HDMI/SDI、盲遍历制式、隐藏黑帧或将黑场认定失去信号。

## 本轮实现与验证

1. 仅识别为 Blackmagic/DeckLink 的原生视频接收使用 OBS 式驱动分配器协商：无接收端 allocator 提案、无硬编码缓冲要求/上游建议，保留驱动 NotifyAllocator 的实际分配器供日志读取；标记真正的终端 renderer。普通采集与音频策略保持。
2. BMD 保留驱动声明的 DIB 方向，不再额外尝试负高度 SetFormat；SetFormat 后让输出 pin 提供连接类型，并严格核对实际 subtype/尺寸/stride/方向/颜色/帧时长，变化则明确失败。不能以兼容为名偷偷换格式。
3. 仅本轮 BMD 候选提供进程级 `VEYRA_BMD_DSHOW_LEGACY=1` 对照开关，使用相同 EXE、运行库、媒体模式和私有配置比较。无自动重连或黑场触发切换。ReceiveCanBlock 保持真实 S_OK，Stop/flush/动态模式检查继续生效。
4. 真实 COM allocator/sample 测试覆盖驱动提供 buffer、只读样本、不改原字节、短帧/动态变更/停止/flush/重连和 COM 身份；现有普通 video/audio 分配器、格式/颜色、GPU ingress 与本机 USB 1080p60 回归。每测试 ≤300 秒，不做压力或并发 GPU 测试。
5. 从当前 main `dbfceab` 隔离施工，复用产品相同的已验 `71723e0` 缓存构建输入，重新编译两个产品 source 及相关测试，链接同一组产品 object；构建输入先后 SHA 核对。所有新产物在 `E:/项目/Veyra/{reports,archives,build,tests,logs,tmp,test-packages}/blackmagic-followup-20261009`。
6. 本地候选、对应源码、真实命令/日志/失败记录、简明实卡步骤交付。38 原工作树和 495 未提交文件按不可变 start 保护；旧 guard/start 不改。运行库、SDK、模型不进 Git，不改驱动、用户配置或其他算法。没有子 Agent。

## 参考

- [OBS CapturePin / terminal filter](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/capture-filter.cpp)，LGPL-2.1-or-later，Copyright (C) 2023 Lain Bailey；复用协商逻辑，保留 Veyra 的 RAII、状态/布局检查与分配器诊断。
- [OBS direct connection](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device.cpp)。
- [Microsoft GetAllocator](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-imeminputpin-getallocator)：输出 pin 决定实际 allocator；NotifyAllocator 后 GetAllocator 返回已选择 allocator。
- [Microsoft IAMFilterMiscFlags](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-iamfiltermiscflags)：source/renderer 应提供类型标记；此标记不是像素正确证明。
