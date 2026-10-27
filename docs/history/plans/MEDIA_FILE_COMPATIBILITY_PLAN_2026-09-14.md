# 媒体文件兼容性计划（2026-09-14）

## 结论

“MOV 不支持”和“AV1 不支持”不是同一件事。MOV 是容器，AV1 是容器内的视频编码；同一个 `.mov` 也可能是 ProRes、H.264、HEVC、AV1 或其他编码。不能按扩展名承诺可播放。

本机随 1.2.0 发布的 patched FFmpeg n9.0.1 配置已经包含 `avformat` 和原生软件解码器。对实际随包 `avcodec-63.dll` 调用 `avcodec_descriptor_get_by_name` / `avcodec_find_decoder` 的结果为：H.264、HEVC、AV1、VP9、ProRes、DNxHD、MPEG-2、MPEG-4、VC-1 均能找到解码器；配置也保留 MOV/MP4、Matroska/WebM、AVI、MPEG-TS 解封装器。文件选择框已经列出 MP4/MKV/MOV/AVI/TS，且保留“所有文件”。

这不等于产品已经可靠支持这些素材。1.2.0 基线的普通文件播放曾强制 `preferHardwareDecode=false`，全部走软件解码；本分支已改为先尝试共享 D3D12VA，并在首帧导入契约不满足时回到软件。图在创建前只从 `AVCodecParameters` 推断色彩与位深仍然不够，许多 MOV/AV1 文件的真实像素格式、HDR 信令、色度位置要到首帧才确定；失败 UI 只显示“无法打开视频”。因此用户看到“不支持”时，可能是软件 AV1 性能不足、HDR/10-bit/4:2:2/4:4:4 输入契约不完整、音轨初始化失败，或文件本身损坏，而不是 FFmpeg 完全没有 AV1/MOV。

1.2.0 随包的 patched FFmpeg 构建配置禁用了 `libdav1d`。对实际 AV1 MP4 做软件探针时，原生 AV1 解码器枚举成功，但首包返回 `-40 / Function not implemented`，所以“能枚举”不能当成“能播放”。本轮已在项目外依赖目录用同一套 PS5 slice 补丁重建 FFmpeg，启用 `libdav1d 1.5.4`；新构建的 AV1 30 帧、ProRes-MOV 30 帧软件探针均通过，H.264 D3D12VA + NR 探针也通过。该新依赖尚未替换 1.2.0 发布资产，完整便携包和对应源码包审计仍是下一步。RTX 30 及更早显卡通常不能把 AV1 解码交给硬件，不能假装它们会获得 RTX 40/50 的 AV1 硬解表现。

## 范围与不承诺

本节点只扩展**本地文件输入**的容器、视频解码、色彩契约和诊断。不会把“可打开”宣传成每种编码参数、每块显卡、每个损坏文件都稳定；不会把 Dolby Vision、HDR10+ 动态元数据、ProRes RAW、alpha、交错视频、DRM、加密蓝光、无损多轨编辑工程或任意外挂字幕列入本轮承诺。

导出保持独立：当前仍是 NVENC H.264/HEVC MP4，HDR 是 HEVC Main10/PQ。输入能播放不代表其原音频可无损装进 MP4；现有导出会拒绝 MP4 不兼容音频，不能悄悄丢音轨。

## 实施顺序

### A. 先做媒体预检和真实错误报告

现有 `veyra_media_probe` 和文件源日志在创建增强图前读取：容器名称、视频 codec/profile、像素格式/位深、尺寸、帧率与 VFR、色彩范围/矩阵/transfer/primaries、色度位置、音频 codec/声道布局，以及 FFmpeg 能否找到软件解码器和 D3D12VA 硬件配置。

预检结果产生明确的支持结论，而不是靠扩展名：

- `可播放（硬解）`：第一帧验证为可导入的 D3D12 NV12/P010，并且共享设备、fence、平面 SRV 都有效；
- `可播放（软件解）`：解码器与 CPU→图像转换契约都有效；
- `可播放但有边界`：如 SDR 10-bit 将明确写出当前转换策略；
- `拒绝`：给出具体 codec/pixel format/色彩元数据/损坏数据/音频布局原因和 FFmpeg 错误码。

UI 在打开失败时显示上述原因；日志完整记录 FFmpeg codec id/name、profile、实际首帧 format 和硬解回退理由。这样用户可以把一段诊断发回来，不再靠“这个格式不支持”猜。

### B. 把文件解码改成安全的 Auto 硬解→软件回退

普通文件改为默认尝试 D3D12VA，不把硬解当必需项。只有 AVCodecHWConfig 表明该 codec 可以 D3D12VA，且首帧的共享纹理是本图已经支持的 NV12/P010 时才保留硬解；否则从开头原子重开为软件解，重置音频、PTS、增强历史和 UI 状态。

不能像现在这样在图中第一次碰到不认识的硬件纹理才失败。D3D12VA 失败、设备不支持 AV1、10-bit 纹理格式不符、驱动返回异常，都必须走可观察的软件回退，并显示“软件解码（原因）”。不会使用 CPU 每帧 readback，也不会把失败的硬解帧与已处理帧混在同一会话。

### C. 明确首发兼容矩阵

先以合成样本与真实用户样本验证，再写入 README：

| 输入 | 计划目标 | 关键条件 |
| --- | --- | --- |
| MP4/MOV/MKV：H.264、HEVC | 完整播放 | 8/10-bit 4:2:0；HDR 需完整 BT.2020 + PQ/HLG 信令 |
| MP4/MOV/MKV/WebM：AV1、VP9 | 完整播放 | Auto 硬解，不支持时软件回退；性能按设备区分 |
| MOV：ProRes、DNxHD/HR | SDR 完整播放优先 | 先验证 4:2:2/4:4:4 的 `sws_scale` 输入转换；不承诺 alpha/RAW |
| AVI/TS/M2TS/MPEG/WMV | 逐项预检后播放 | 允许选择，不以扩展名误报支持；按 codec/交错/VFR 结论显示 |
| Dolby Vision / HDR10+ / ProRes RAW | 明确拒绝或 SDR 基础层 | 不伪造动态元数据，也不假装原生 HDR 保留 |

打开框改为按“常用视频”“专业/摄像机容器”“所有文件”分组，加入 `.webm .m4v .m2ts .mts .mpg .mpeg .wmv .flv .ogv .mxf`，但文案写“列出不等于保证可解码”。MOV 不再是特殊格式。

### D. 修复颜色与位深的创建时机

文件来源改为先 decode/probe 至首个有效视频帧，解析实际 `AVFrame` 的 pixel format 和色彩 metadata，再创建 EnhanceGraph。图描述增加文件 `inputBitDepth`，不再只给采集卡填写 bit depth。

- SDR 10-bit/12-bit：保留明确输入范围和矩阵；本轮先允许转换到图的工作格式，状态栏显示是否发生精度转换。
- HDR PQ/HLG：只有真实首帧具有 BT.2020 NCL + BT.2020 primaries + PQ/HLG 才进入现有 HDR 图；缺失/矛盾 metadata 需要用户选择“按 SDR / 按 PQ / 按 HLG”，默认不猜。
- 4:2:2 / 4:4:4：软件转换时记录色度抽样与下采样；第一版不把它说成无损输入保留。ProRes 4444 alpha 直接忽略 alpha 前必须在 UI 说明，优先在本轮拒绝而不是静默丢透明度。

### E. AV1 性能路径，先量化再改依赖

基准以 1080p60、1440p60、4K30/60 AV1 各跑硬解和软件解，记录 decode P50/P95、掉帧、CPU、GPU、首帧、seek、音画同步。当前软件线程上限为 4；只有基准证明 AV1 受 CPU 解码限制时，才做 codec/分辨率感知的有上限线程策略。

本轮已经完成了依赖评估：原生 AV1 路径对真实样本失败，因此 `libdav1d 1.5.4` 接入 FFmpeg。重建保留了 PS5 H.264 32→256 slice patch，配置仍是 LGPL；独立 prefix 的 `veyra-local-build.json` 记录五个 FFmpeg DLL 和 `dav1d.dll` 的 SHA-256、版本、许可证与来源。仍未把它直接替换进 1.2.0 发布包：发布前必须把 dav1d 对应源码/port、版权与 SPDX、运行库清单、完整便携包和 AV1/MOV 回归一起审计。

### F. 音频和导出不要借机回归

播放音频使用 FFmpeg 解码再进入已有 PCM 管线；预检必须单列“视频可播放、音频不支持”的状态，不能因为某条音轨失败把视频误报为不支持。导出仍默认 MP4，遇 PCM/Opus/FLAC 等不能封装的音频时给出两个显式选项：转 AAC（需实现重编码并测试同步）或选择 MKV 容器（需新增导出合同）。此项不混入首个“AV1/MOV 可播放”补丁。

## 验收与回退

1. 不带增强的每个矩阵样本连续播放、暂停、seek、重开、音频同步；同一素材分别强制软件和 Auto 路径。
2. AV1 SDR、AV1 HDR、MOV H.264/HEVC、MOV ProRes、MKV/WebM VP9/AV1、TS 需要实际 `ffprobe` + Veyra 日志双证据。测试媒体只存本地/测试路径，不提交源码仓库或 Release。
3. 8/10-bit、4:2:0/4:2:2/4:4:4、VFR、负 PTS/edit list、无音轨/多声道音轨分开覆盖；对不支持项验证错误文本和无资源泄漏。
4. 再打开 NR、SR、DLSS/XeSS FG 做至少一条 SDR 和一条 HDR 回归；导出、截图、PS5、采集卡必须跑现有 delivery，确保本节点没有改变它们的输入路径。
5. 每次解码路径切换、seek、首帧色彩改变都原子 reset 图历史。硬解首次帧不符合 NV12/P010 时回退软件，不允许半会话继续。

当前代码已完成首帧颜色/位深探测、普通文件 Auto 硬解到软件回退、硬件纹理导入契约和具体解码日志；实际新 FFmpeg 的 AV1/ProRes 探针证据见 `docs/WORKLOG.md`。若用户样本是 Dolby Vision、ProRes RAW 或受 DRM 保护，则仍明确拒绝并说明原因，不做不可靠的兼容层。新 FFmpeg 尚未成为正式发布资产，不能把本地验证写成 1.2.0 已支持。

## 用户 MOV 黑屏补充验证（2026-09-14）

用户样本 `C:/Users/123/Videos/2026-08-11 21-29-44.mov` 已实际跑通。该文件是 H.264 + AAC 的 QuickTime MOV；视频-only 副本和软件/D3D12VA 探针均通过，黑屏只在带 AAC 的播放器启动路径复现。原因不是容器或视频 codec，而是 AAC 编码器 priming 造成的合法负首帧 PTS（约 `-1.3ms`）被音频启动逻辑误判为空队列，导致 renderer 未锚定、音频主时钟未释放，文件调度停在首帧前。

`WasapiAudioSink.cpp` 已改为用实际音频预填充时长区分空队列与负 PTS，并在 PTS 有限时照常锚定。原始文件无增强 10 秒播放器回归：`smoke frames=542`、`realPresented=540`、`failed=false`、约60fps；软件解码30帧、D3D12VA12帧和音频时间线完整测试均通过。完整证据和局限写入 `docs/WORKLOG.md`；尚未替换正式 Release 资产。
