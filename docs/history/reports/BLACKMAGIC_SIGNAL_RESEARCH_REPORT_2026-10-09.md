# BMD 持续黑屏：日志与采集实现对照

本轮确定故障已经发生在收到的采集样本阶段；前次 allocator 兼容修改在现场确实启用了，仍没有恢复画面。可以确定定位层级，但现有日志不能确定具体是物理输入口、制式/像素合同错配还是 DirectShow 驱动兼容。

## 1. 新日志包含两次应用会话

文件 SHA256、分组、每次尝试的行号/参数/像素记录在 `E:/项目/Veyra/reports/blackmagic-signal-research-20261009/log45-analysis.json`。不将用户原始日志或设备路径提交 Git。

| 日志 UTC 时间 | 实际配置次数 | 已记录像素抽样 | 结果 |
|---|---:|---:|---|
| 10-08 15:20 起 | 7 | 44 | 38 次黑场值，6 次 ARGB 全白值；这是原有测试段 |
| 10-09 10:56 起 | 9 | 41 | 全部黑场值；6 次用户连接请求，其中 r210 路径又发生 3 次自动重连 |

今日 9 次配置均记录 `capture-bmd-compat enabled=true legacyOverride=false`。5 次 HDYC 原生连接记录 `connectedContractMatches=true`；上游 allocator 已分别变为 WDM 的 4 buffers 和 Decklink 的 30 buffers。前次候选没有因为用户忘记启用而失效，也不能再把“换 allocator 就能解决”作为结论。

今日测试包含两个 DirectShow 入口、1920×1080 的 25/30fps、3840×2160 的 25/30fps、原生 HDYC 和系统转换 r210。这些是配置值；日志对应 VideoInfo 类型没有证明实际信号逐行，不能用分辨率标签推断真实扫描方式。原生 HDYC 已记录的 33 次抽样都是 `Y=16, U/V 偏差=0`；r210 经系统 RGB 转换的 8 次抽样是 RGB=0。

每次像素抽样为既有 CPU sample 的最多 32×18 个点，不是全帧抓图；日志也只保留前 6 次及每 30 次等稀疏记录，**41 是已记录的抽样数，不是收到的总帧数或完整信号检测**。HDYC 抽样发生于 `CaptureCardSource::Impl::SampleCB` 获取驱动指针后、`writeCaptureFrame` 和 GPU 上传之前。它证明采集上游交来的抽样内容已经是黑场值，后续 NR/颜色/送显不足以解释这个上游现象。

最后一次配置的末条统计已记录收到 819 帧、处理 819 帧、送显 818 次，回调约 24.98fps，增强关闭。这说明处理路径在运行；不代表 HDMI 信号有效或物理屏幕显示正常。r210 路径还存在回调断档/自动重连，不能概括成所有格式始终稳定收帧。

## 2. madVR 和官方软件分别是什么

madVR 桌面软件是视频渲染器；如果用户使用的是 VideoProcessor＋madVR，负责 Blackmagic 采集的是 VideoProcessor。不能拿 madVR 的缩放/HDR 渲染参数来修正尚未收到有效内容的问题。Blackmagic 自带采集软件名为 [Media Express](https://www.blackmagicdesign.com/products/decklink/mediaexpress)。用户暂时不确定具体软件，本轮没有认定其一定使用 VideoProcessor。

[VideoProcessor 自身说明](https://github.com/defl/videoprocessor/blob/7e6fc67da07ecd3dbcf351c073809869c812a06f/README.md)将采集和渲染分开。其[官网手册原件](https://github.com/defl/videoprocessor_website/blob/988aff5bace5f0f95be9835a885beec3674e6802/site/templates/manual.html)特别区分 capture 状态与有效输入，并分别显示 input lock、实际模式、编码/位深；本轮读取官网失败后，读取了作者同源固定提交的 HTML，不能声称成功访问了当前在线手册。

Blackmagic [Desktop Video 手册](https://documents.blackmagicdesign.com/UserManuals/DesktopVideoManual.pdf)说明需核对物理输入连接，Media Express 可发现输入格式变化。Media Express 没有公开可审计的完整源码，本轮不声称检查过其内部采集实现。

## 3. 可以直接核对的关键实现差异

| 项目 | VideoProcessor | OBS Blackmagic 专用来源 | Veyra 当前 BMD 路径 |
|---|---|---|---|
| 采集入口 | DeckLink 原生 API | DeckLink 原生 API | DirectShow WDM/Decklink filter |
| HDMI/SDI | 读取当前配置并按选择设置输入 | 可按用户选择设置输入 | 驱动属性页/现有配置；该卡无 crossbar |
| 制式发现 | 启用自动检测，响应格式回调 | 支持的设备提供 Auto，也保留手动模式 | 枚举模式，SetFormat/GetFormat 和连接合同读回 |
| RGB/YUV/位深变化 | 按实际检测标志重新启用输入 | 自动模式按检测标志重配 | 使用所选 DirectShow 类型，没有原生检测标志 |
| 无信号辨别 | 读帧 no-input 标志及原生锁定状态 | 本轮所审帧回调没有同样的 no-input 判断，不冒称其有 | 只有像素黑场提示，明确不是信号检测 |

固定来源：

- VideoProcessor `7e6fc67da07ecd3dbcf351c073809869c812a06f`，[BlackMagicDeckLinkCaptureDevice.cpp](https://github.com/defl/videoprocessor/blob/7e6fc67da07ecd3dbcf351c073809869c812a06f/src/VideoProcessor-Lib/blackmagic_decklink/BlackMagicDeckLinkCaptureDevice.cpp)：输入/启动约 165–218 行，格式变化约 398–530 行，无信号标志约 622 行，lock/检测模式/位深约 1019–1028 行。
- OBS `e7f0b0d43c538f5d108aa4cbea473d57b3012a1a`，[decklink-device-instance.cpp](https://github.com/obsproject/obs-studio/blob/e7f0b0d43c538f5d108aa4cbea473d57b3012a1a/plugins/decklink/decklink-device-instance.cpp)：输入口/Auto/启动约 428–514 行，实际 RGB/YUV/位深变化重配约 745–784 行。设备是否支持自动检测在 [decklink-device.cpp](https://github.com/obsproject/obs-studio/blob/e7f0b0d43c538f5d108aa4cbea473d57b3012a1a/plugins/decklink/decklink-device.cpp)检查。
- OBS libdshowcapture `c13d4b7b0c66979396ba0a9060c9aafc15bb7b22`，[device.cpp](https://github.com/obsproject/libdshowcapture/blob/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source/device.cpp)仍是另一条通用 DirectShow 路径，不能与 OBS Blackmagic 专用来源混为一条。

上游源文件只存 E 盘 downloads，实际 SHA256/大小/URL 在 `sources.json` 和 `website-sources.json`。没有将第三方 SDK/源码复制进产品。

## 4. 官方合同解释了为什么“有 FPS 仍黑”

[DeckLink SDK 官方手册](https://documents.blackmagicdesign.com/UserManuals/DeckLinkSDKManual.pdf)提供三个不同概念：配置的输入模式、检测到的真实模式、收到的帧是否有效。自动检测仅限声明支持的设备；要显式启用，并响应格式变化回调。`bmdFrameHasNoInputSource` 表示没有检测到输入、该帧无效。

因此驱动持续回调、PTS 按配置递增和 `SetFormat=S_OK` 可以同时出现，而有效画面仍未恢复。Veyra 的 `requestedFps=connectedFps` 只能校验媒体类型配置，无法代替硬件 signal lock。VideoInfo 还可能不含可核验的逐行/隔行信息。它们构成当前采集入口的确定能力缺口。

但这仍不足以证明本例“一定只是模式没选对”。可能存在输入口/子设备不一致、实际制式或 RGB/YUV/位深不匹配、驱动占用或通用采集路径兼容问题。真正黑色内容也能产生这些像素值。用户此前说 OBS 通用与专用来源都可显示；若同一输入同一时刻通用来源也能出画，DirectShow 本身并非天然不可用，需要对比其具体媒体类型与图状态。

不以 HDCP、线材或硬件损坏作为已查明原因；其它软件同输入正常的反馈，使这些方向优先级低于软件输入配置/路径差异。

## 5. 修复应补什么，怎样证实

建议补 BMD 原生输入适配，优先参考 OBS 的许可证适配实现，并保留 DirectShow 手动回退。先获取真实输入 lock/检测制式/位深/当前端口，再依据官方回调重配输入。这样可以消除盲猜格式的兼容缺口，也能在未出画时明确记录是无信号、合同错配还是有效黑画面。

这是一条有源码和官方合同支持的修复方向，**尚未实施、没有 BMD 实卡恢复证明，不保证能仅靠原生 API 解决所有黑屏**。后续合同详见同轮 PLAN：共享现有处理图、容量 1 mailbox，不新增固定帧缓存；格式变化显式 reset，物理输入不盲切、不写 EDID。原生接入能否降低/增加延迟须实卡测量。

VideoProcessor 根目录许可证在 GPL-3.0 外另列非商业限制；不能仅因文件头写 GPL 就把整段采集代码直接搬入 Veyra。本轮仅对照阅读，后续采用 OBS/官方 API 约定并核对所用版本许可证。

本机 PnP 查询没有 Blackmagic/DeckLink 设备记录；`Program Files (x86)/Blackmagic Design` 只有 Blackmagic RAW 目录，不是 Desktop Video 实卡环境。本轮不能运行能区分上述现场原因的硬件测试，不拿本机 USB 或接口测试代替。

## 6. 本轮交付

- 完成日志分组与原始像素位置复核、两条上游采集路径及官方文档对照、可证伪的后续修复合同。
- 产品代码、运行配置、驱动和运行库未修改；不制作测试包，不将上次未成功的候选重新标为修好。本轮无产品改动，因此不重复 GPU/构建测试。
- 最终旧工作树/脏文件/范围核对回执在 `E:/项目/Veyra/reports/blackmagic-signal-research-20261009/final-audit.json`。
