# HDR 全增强与 5.1 多声道：可行性研究及实施计划

日期：2026-09-14。研究基线：1.1.1 产品提交146f035，当前main723ad2a。用户要求研究全部实现，包括HDR输入/显示、开启NR/SR/FG后保留HDR，以及5.1音频。本轮为代码与接口调研，不是实现验收；未修改产品、SDK或运行组件，未执行新的GPU Create/Evaluate、主机连接或音箱测试。

## 结论

有可施工路线，不能承诺所有输入、算法、硬件组合均已证明可行。应以“在能力满足的机器上，同时实现HDR画面增强和真实多声道输出”为产品目标，逐一验证而不是只交付HDR旁路。NR不一定非要直接接受PQ：保留HDR原始分支，以映射副本给NR，再将受约束的增强变化合回HDR，存在实现完整HDR输出的可能。这是HDR保留合成，不等同NR模型原生HDR推理，尚未验证画质。

硬边界：音源只有2.0就没有六个独立声道可保留；PS5串流的多声道不能靠把本地channels改为6解决。RTX30的NR实验路线不解锁其不具备的DLSS FG能力；“全开”指NR＋一种SR＋一种FG，仍受各后端实际能力约束。

## 1. 查到的事实

| 模块 | 代码/接口证据 | 结论与剩余工作 |
| --- | --- | --- |
| HDR来源识别 | EngineController.cpp:184只给RemotePlay设置hdrInput；196把任何增强与hdrOutput互斥；EnhanceGraph.cpp:92再次拦截 | 本软件有明确限制，不是显示器必然不能HDR＋增强。改为按实际颜色元数据与后端能力建立图契约，不能只删除保护 |
| HDR颜色入口 | YuvToLinearRgb.hlsl当前原生分支保留BT2020→BT709浮点；增强分支按固定1000nit映射后saturate | 现有增强路径在NR之前已经丢失HDR信息，必须先保留原始HDR工作纹理 |
| NR | ParityEncode使用RGBA8代理；ParityDecode读取原始FP16、代理和NR结果，有亮度恢复；NrResidualComposite叠加变化量 | 有HDR保留合成的现成结构；现有max(rgb,0)、代理高光压缩、色域校正与常量亮度尺度未经HDR验证，不能直接称其已支持HDR |
| DLSS SR | DlssSrBackend.cpp:97已有IsHDR及AutoExposure，输入线性RGBA16F | 官方有HDR接口；仍需验证显示参考亮度、曝光与视频估算guidance，不因设置了标志就通过 |
| RTX Video SR | VideoSrBackend.cpp:30限制RGBA8；本地RTX Video SDK1.1.0 samples/ReadMe.md:36要求P010/ABGR10输入仍为SDR，VSR样例也写input assumed SDR | 10-bit不等于HDR。PDF个别样例描述与ReadMe不完全一致；不能把TrueHDR的SDR→HDR生成宣传为原HDR保留。优先验证SDR代理增强＋HDR基底残差；原生PQ输入仅作为有明确证据后的实验 |
| DLSS FG | DlssFgBackend.cpp:264固定colorBuffersHDR=0；创建已有NativeBackbufferFormat参数 | NVIDIA公开指南要求HDR10/RGB10，不支持FP16/scRGB输入。保留直接NGX集成；参考Streamline文档仅核对格式，不引入Streamline。当前固定DLL仍须实测 |
| XeSS FG | 本地SDK3.0.2 guide:626–636及在线官方指南均要求R10G10B10A2、HDR10/BT2100，且不支持FP16/scRGB | 与现有HDR scRGB显示不兼容，需要FP16工作空间→HDR10 FG资源/交换链适配，不能简单换buffer位深 |
| 音频文件 | WasapiAudioSink.cpp:129固定STEREO，308固定nChannels=2，AudioPcmSource约定立体声 | 需要端到端声道布局，不能只改WASAPI输出的数字 |
| 采集音频 | NativeCaptureSink.cpp:24–30、CaptureAudioSession.cpp:238拒绝>2声道；仅保存WAVEFORMATEX，未处理扩展声道掩码 | 前文“多声道输入会混成立体声”不全面：文件会混音，当前采集入口可能直接拒绝。必须支持WAVEFORMATEXTENSIBLE、完整格式长度与真实channel mask |
| PS5音频 | Veyra ChiakiBackend.cpp:134–149仅允许1/2声道48k；固定Chiaki的opusdecoder.c使用opus_decoder_create | 标准Opus该接口只接1/2声道。未找到当前会话协商多流5.1的证据；不是说Opus编码家族不支持多声道，而是当前串流协议路径没实现/验证 |
| HDR输出/导出 | Presenter已有原生HDR旁路；NvencD3D12Encoder固定NV12；截图走RGBA8 | 显示与导出必须分开验收。HDR视频需Main10/P010与正确色彩信令，普通JPEG不能冒充HDR截图 |

## 2. HDR全增强的候选架构

继续使用同一FrameSource → EnhanceGraph → FrameSink，不复制文件、采集、PS5三套链路。默认全部效果关闭的决定不变。

1. **输入**：文件/采集/PS5解析实际range、matrix、primaries、transfer、位深、chroma位置、mastering与内容亮度信息。首个完整目标是PQ/HDR10；HLG需要独立OOTF及显示环境映射。P010也可能装SDR，不能按像素格式强判HDR；采集卡未暴露元数据时提供明确的手动颜色覆盖，不自动猜成HDR。
2. **工作空间**：保留未截断的浮点HDR基底，统一亮度单位与参考白。明确每张纹理的颜色契约；若采用线性BT709/scRGB，不能无条件裁掉表示广色域的负分量。若用线性BT2020作为基底，则各后端入口/出口显式转换并保留原底，不改变SDR路径。
3. **SR**：DLSS SR走其HDR合同；Video SR先研究代理路线：对固定映射的副本做VSR，与相同坐标/采样核的普通缩放副本相减，得到变化，再合成到HDR缩放基底。必须抑制高光、色域边界的错误增益及缩放残差错位。标注混合实现，不能宣称VSR原生HDR已验证。
4. **NR**：保留HDR基底H；用稳定的映射T(H)给NR，得到N。在明确颜色空间求增强变化，对亮度/色彩分别约束，在高光、近黑、低置信度处衰减，再合回H。NR不能看到被代理压缩掉的高光细节，因此这部分保留原始信息，不承诺NR增强所有高光区域。要求NR关闭/变化为0时恢复H，而不是从SDR逆算HDR。也可单独验证固定NR接口是否支持更高精度资源，但不得凭未公开参数名臆测。
5. **顺序**：分别支持现有SR→NR与用户低延迟NR→SR。基底、副本、残差、运动矢量的尺寸与坐标一起变换，保持同一源PTS/epoch。不能拿旧帧HDR高光套到新帧增强结果。
6. **FG**：完成增强后明确转换一次到PQ/BT2020 RGB10，DLSS FG打开HDR标志并匹配资源/创建格式；XeSS用HDR10代理交换链并正确设置色彩空间。原帧和生成帧使用相同亮度与色域约定；不对PQ数值做线性亮度插值。NVOF/AMD/GPU DIS可以吃独立一致的运动分析副本，不应为了光流把最终HDR压成SDR。
7. **显示**：HDR10输出、正确DXGI color space；UI与字幕按明确参考白合成。两个FG后端对UI格式/alpha规则不同，分别处理，不盲目共用RGB10的2-bit alpha。跨屏、Windows HDR开关、全屏、直播模式变更均走原子重建/reset。
8. **导出/截图**：纳入完整路线单独验证。HEVC Main10/P010、BT2020/PQ标记与正确内容/显示metadata；处理改变亮度后不能无脑复制旧MaxCLL/MaxFALL。截图提供HDR格式与明确SDR映射PNG/JPEG两个方向，不把普通8-bit PNG称为HDR。HDR10+/Dolby Vision动态元数据不由“支持HDR10”推导，须单独识别和说明。

上述NR/VideoSR合成是工程假设，需高光细节、颜色与时域测试；不是已完成的实现或对官方模型画质的保证。不能以挂HDR标签、峰值放大或结果不黑代替验证。

## 3. 5.1完整音频链

引入AudioFormat（采样率、样本格式、有效位数、声道数、speaker mask/AVChannelLayout），由输入协商到输出。优先5.1 PCM，内部布局可扩到7.1；这里不把Dolby Atmos/DTS对象音频或压缩码流透传混为一项。

- 文件保留FFmpeg真实布局。DirectShow完整保存WAVEFORMATEXTENSIBLE，支持6声道PCM/浮点及16/24/32bit容器，验证cbSize、mask位数、block alignment、side/back布局；不可用“有六个声道”替代声道位置。
- WASAPI GetMixFormat/IsFormatSupported协商设备可用布局。优先原布局，设备不支持时明确提示立体声回退，禁止静默截前两个声道。实际5.1需要主机/采集卡/Windows输出端确实提供该能力；HDMI直通5.1不等于USB采集能录到5.1。
- PCM环形队列、pull、增益、淡入淡出、漂移重采样、缓冲计时与补偿统一以“音频帧”为单位，样本数量=帧数×channels，移除硬编码×2和/2。所有声道同步处理，不能为六个声道各建独立时钟或独立停放。
- 保留文件音频主时钟、实时缓冲上限及此前欠速预览策略；效果延迟波动不再触发声音逐帧暂停，多声道不应该凭空增加固定补偿。
- PS5单独研究实际音频header/协商及是否存在多流发送；当前1/2声道路径继续如实输出。如果上游只发送2.0，即使做上混也只能标注虚拟环绕，不能算真实5.1达标。

## 4. 实施顺序与验收门槛

| 节点 | 交付门槛 |
| --- | --- |
| A：最难链路原型 | 在隔离分支验证HDR基底＋NR残差、DLSS SR HDR、RGB10 DLSS/XeSS FG；先证明这些能组合再大改UI。VideoSR代理同样保留专项，不默默删除选项 |
| B：HDR输入与颜色合同 | 文件/采集/PS5，PQ10bit/HLG、软硬解、full/limited、BT2020、SDR混合；色块与高光数值正确；不再受isRemote硬限制 |
| C：NR/SR/FG全组合 | NR三运行版本分别记录；两SR、两FG、两种顺序及开关组合；缺硬件组合明确未测。负值/高光不误截、灰阶单调、原帧与生成帧亮度一致，无NaN/Inf |
| D：5.1 | 六路独立脉冲/音调定位，矩阵串音及LFE/中置检查；44.1/48/96k、side/back布局、输出热拔插、2.0回退、开关增强/seek/过载时连续与同步 |
| E：产品收尾 | HDR/声道状态展示实际输入输出，不只显示请求值；截图/导出、WGC、跨屏、全屏、重连、暂停seek回归；沿用默认全关和设置持久化 |

HDR验收素材：黑位、近黑渐变、203/400/1000/4000nit色块、BT2020边界色、细节高光与移动高光、scene cut。零变化/保护区要求HDR基底保真；NR真实作用下测增强差异和时域稳定，不能只看Create=success。配合用户HDR屏肉眼/必要的测量设备，而非仅凭截图“看起来亮”。

音频验收同时检查逻辑PCM路径和真实5.1输出端；没有多声道设备只能给出软件数据通过，不能宣称听感/扬声器布局验收。测试单项≤300秒，长期验证另由明确分段与用户会话完成。

每个节点建立Git存档并写命令、失败、日志和未执行边界。首次合成实验不得替换已发布的1.1.1，不新增未知DLL；若必须换组件，先做身份与独立兼容审计。性能/显存逐阶段实测：浮点纹理更大但不能据此声称总GPU耗时必然翻倍，亦不保证所有机器4K全开实时。

## 5. 本轮依据及未执行项

已读取本地音频、输入转换、NR parity/residual、SR/FG与Presenter/NVENC代码，现有PS5 HDR方案/执行记录；本机固定SDK及下列官方文档。没有运行第三方应用，没有更换运行库，没有产品构建/运行或HDR/5.1实机测试。本方案不把上次SDR映射后的HDR输入测试当成原生HDR增强证据。

参考（访问2026-09-14，仅行为/接口参考；不复制SDK文件）：

- [NVIDIA DLSS SR Programming Guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf)：HDR/线性输入标志与曝光合同。
- [NVIDIA DLSS FG HDR指南 §11](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md#110-dlss-g-and-hdr)：HDR10格式；仅参考，不改为Streamline实现。
- [Intel XeSS FG HDR指南](https://github.com/intel/xess/blob/main/doc/xess_fg_developer_guide_english.md#hdr-display-support)：RGB10与HDR10合同、FP16/scRGB限制。
- [Microsoft Advanced Color](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)：scRGB/HDR10、参考亮度与显示管理。
- [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible)、[Device Formats](https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats)：声道位置和设备协商。
- [Chiaki opusdecoder](https://github.com/streetpea/chiaki-ng/blob/0e16950165f06e5c3291537c2eeba6e852be7120/lib/src/opusdecoder.c)、[Opus单流解码接口](https://opus-codec.org/docs/opus_api-1.5/group__opus__decoder.html)：当前1/2声道路径；不把它扩大为Opus多流的总体限制。
- 本地NVIDIA RTX Video SDK1.1.0的samples/ReadMe.md:36、VSR样例及Programming Guide §3.3.5/5.2：10bit SDR与TrueHDR转换需区分；未将本地SDK正文、样例或PDF纳入源码。
