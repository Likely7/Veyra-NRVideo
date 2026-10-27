# HDR 全增强与多声道施工记录

2026-09-14，基线 edabd3c，分支 codex/hdr-multichannel；回退点 checkpoint/pre-hdr-multichannel-20260914。用户授权实施，PS5 真实多声道不纳入本轮，未授权新发布。

## A：HDR 核心原型（已通过首轮 GPU 数值测试）

保留线性 BT.709/scRGB HDR 基底，NR 处理固定 203nit 映射副本后合成有界变化；近黑/压缩高光衰减，不从 SDR 逆造原始高光。Video SR 同样保留基底并比对同采样坐标代理。DLSS FG 颜色标志与 RGB10/PQ 资源同时切换；XeSS 使用 HDR10 交换链；原帧/生成帧/对比参考格式统一。默认效果全关不变，运行组件未替换。

命令：`cmd /c out\release-1.1.0-build.cmd`（125 步及测试增量构建成功），`out/remoteplay/product-repair/veyra_hdr_enhancement_tests.exe 0/2/4/5` 分别执行。每项不到 30 秒。

- shader 零残差 1024 像素完全一致，包含黑位、近黑、203/1000/4000nit 与广色域负分量。
- mode2：NR+DLSS SR+DLSS FG，NR/SR各20次，18张有效生成，1000nit参考色块998.932nit。
- mode4：NR先行+Video SR+DLSS FG，同上20/20/18与998.932nit。
- mode5：NR+DLSS SR+XeSS FG，20/20/16，RGB10 HDR10 初始化成功，参考色块998.932nit。
- 证据：logs/hdr-multichannel/shader.txt、dlss-hdr.txt、vsr-nrfirst-hdr.txt、xess-hdr.txt。NGX Create/Release success 0x1，SEH0；XeSS Init0。
- 初次构建脚本用了 cmd 不识别的正斜杠路径，未构建即失败；改反斜杠成功，日志 out/hdr-build.log/out/hdr-build2.log。

以上是合成输入与数值/接口测试，不是 HDR 实屏画质验收或所有 NR 版本组合验收。无 PS5/采集实卡新测试。未发布。

## B：输入、5.1、导出、截图及状态（软件实现完成，硬件验收待用户）

- 文件和采集的 HDR 判定依据 PQ/HLG transfer，不再把 P010 或 BT.2020 标记本身等同 HDR。P010/P016 采集识别扩展 Windows 色彩元数据；设备漏报时提供手动 PQ/HLG，仍限制明确的 YUV HDR 合同。HDR RGB/YUY2、BT.2020 CL 不冒充已支持。HLG 参考 1000nit / gamma1.2，转换为统一线性 BT.709/scRGB。输入契约意外变成 SDR 时拒绝错误解释，需重新打开源。
- NR、DLSS SR、Video SR、DLSS FG、XeSS FG 及 NR先行两种顺序保持一套图。原版、Community、Ampere 三运行版本在 RTX5070 的 HDR组合已执行，不能据此宣称 RTX30 验收。
- 文件 PCM、采集 PCM、WASAPI、补偿、增益与淡出保留声道布局，统一按音频帧计数。WAVEFORMATEXTENSIBLE 支持 PCM16/24/32及float32、side/back掩码；未知多声道布局拒绝猜测。采集优先尝试设备实际枚举的多声道格式，仍使用直接连接与10ms协商。输出端不支持时执行明确矩阵降混，不只截取前两声道。PS5保持既有双声道。
- HEVC Main10导出：GPU RGB→P010、NVENC D3D12输入fence、BT.2020/PQ VUI及容器标记；完整逐帧验证新增位深与颜色检查。HLG统一输出PQ。不重新伪造处理前的mastering/MaxCLL。原音轨封装复制，六声道保持。H.264 HDR拒绝，XeSS FG依旧仅预览。
- 截图：HDR保存FP16 scRGB JPEG XR，SDR继续PNG/JPEG；工具栏识别实际JXR路径和完成状态。仅截图读回像素，正常预览/导出不新增像素回读。
- 详细面板分别显示实际颜色链路、输入/输出声道与降混；PS5/采集/导出工具提示及中英文README同步。默认效果全关、运行组件、配对及用户设置不变。

### 本轮关键发现与修正

1. 新NVENC VUI赋值最初使用整数字面量，MSVC要求枚举；改为明确UNSPECIFIED枚举，随后构建通过。
2. 多声道采集排序最初遗漏AudioFormat头文件，deleter的const类型无法move-assign；修正拥有者类型，原生连接测试通过。
3. 首份生成的视频夹具缺少PQ/BT2020 primaries的有效位流标记。产品正确拒绝将其按SDR处理；用FFmpeg hevc_metadata补齐**测试素材**的描述后重新测试，不削弱产品防错检查。
4. 硬解测试最初在首帧前检查实际硬解状态；FFmpeg此时尚未分配解码面。改为验证每张实际AVFrame为D3D12，与产品行为一致。
5. 截图首轮仅验证JXR自回读，未覆盖GPU→CPU拷贝；代码复查发现FP16每行仍按4字节宽度复制。修复为bytesPerPixel，并新增独立GPU原图→JXR逐像素比较，覆盖右半幅、负分量和4000nit。首轮自回读结果**不能作为完整截图通过证明**；以下final-mode记录才是有效证明。
6. NativeCaptureSink的上游缓冲建议仍限定nBlockAlign≤8，六声道会退回驱动默认。改为按1–8声道/16、24、32位校验，新增6ch float的11520字节/10ms协商检查。
7. 两个旧契约测试把P010等同HDR、把扩展transfer16误称PQ并一律拒绝。按现在的颜色合同修订：P010本身不是HDR，15为PQ、16为HLG，明确元数据必须保留。新增完整BT2020元数据和声道掩码变更重连测试。测试命名空间/参数个数编译错误均已修正，不作为功能通过证据。

### 实际命令及结果

所有相对路径以仓库根为基准；构建输出在 `out/remoteplay/product-repair`，日志在 `logs/hdr-multichannel`。测试夹具、抓帧、JXR、日志和DLL均被Git忽略。

- 构建：`cmd /c out\release-1.1.0-build.cmd`，最终增量构建通过。中间失败和修复记录为 `out/hdr-build4.log`至`out/hdr-build19.log`；部分编译单元沿用仓库现有`/WX-`和第三方头文件警告，不宣称零警告。
- `veyra_pipeline_tests.exe`：52 checks，0 failures；`veyra_capture_color_tests.exe`：0 failures（`capture-color-final.txt`），包含多声道DirectShow连接、10ms协商、side/back掩码重连与PQ/HLG元数据。
- `veyra_hdr_color_tests.exe`：30种采集颜色GPU用例及16个HDR组合（PQ/HLG、planar/P010、full/limited、HDR输出/SDR映射）全部pass=1；`hdr-color-final.txt`。
- `veyra_hdr_enhancement_tests.exe 0/1/2/3/4/5/6`：分别执行。原版NR20次；有SR时SR20次；DLSS有效生成18，XeSS生成16。mode3/4验证VideoSR与两种顺序；mode6验证DLSS SR的NR先行。`mode-*.txt`、`final-mode-*.txt`。
- `veyra_hdr_enhancement_tests.exe 2 1`与`2 2`：Community/Ampere，在RTX5070上20/20/18、约999nit通过；`runtime-1.txt`/`runtime-2.txt`。未在RTX30/40执行。
- `veyra_hdr_enhancement_tests.exe 2 0 out/hdr-audio-fixtures/pq-tagged-51.mp4 0/1`：分别软件/D3D12硬解；20/20/18，原帧和生成帧高亮边界通过。HLG硬解同样通过。`file-hdr-hw0.txt`、`file-hdr-hw1-v2.txt`、`file-hlg-hw1.txt`。
- 最终截图独立比较：1024像素黑/近黑/广色域/4000nit与2073600像素FP16输出逐像素误差0；3686400像素RGB10→scRGB的最大误差0.00243568（scRGB单位，约0.195nit），符合FP16量化。文件也单独无损回读校验。`final-mode-0/1/2/5.txt`。
- `veyra_multichannel_tests.exe out/hdr-audio-fixtures`：28 checks，0 failures；44.1/48/96k、side/back、PCM16/24/32格式解析、六个独立声道音调（含中置/LFE，其他声道泄漏0）、音量/淡出同步及帧单位PTS通过。`multichannel-final.txt`。
- `veyra_capture_audio_tests.exe --jitter --5.1`：5秒30/35ms处理延迟波动，新增重锚0、欠载0、丢失0、p95偏差24.81ms。`--endpoint-loss --5.1`：2次重试后恢复。`--drift-fast --5.1`、`--drift-slow --5.1`：各120秒，p95偏差4.27/4.23ms，稳态没有反复重锚、队列峰值89.65ms。均静音，当前物理输出是2.0，不代表5.1扬声器验收。
- 首轮`audio-timeline.txt`含4项失败：旧测试把保留下来的单声道源交给默认立体声renderer。逐项核查日志后修正调用顺序，按`pipe.pcmFormat()`创建endpoint；同样修正player_probe调用者。最终重跑结果见`audio-timeline-final.txt`及后续补充，首轮不能作为通过证据。
- 产品导出命令示例：`veyra.exe out/hdr-audio-fixtures/pq-tagged-51.mp4 --nr --fg --no-sr --hevc --export-out out/hdr-audio-fixtures/export-pq-nr-fg-v2.mp4`，30源帧→60输出帧；`--nr --video-sr 1 --fg --hevc --max-frames 12`导出4K，12源帧→24输出帧（含既有明确的CFR尾保持），完整验证通过。
- FFprobe确认Main10、10bit、BT2020/PQ、6ch；独立FFmpeg逐帧解码参考高亮约994nit（原始编码夹具约1004nit）；HLG→PQ NR导出约1004nit。`export-final-check.json`、`export-highlight.json`、`export-*-probe.json`。导出前后解码六声道PCM SHA256一致（`export-audio-check.json`），没有丢失音轨或降混。
- 8秒真实播放器命令带`--nr --fg --smoke-screenshot --smoke-save ...`：文件6ch→当前2ch设备明确降混，工具栏PNG保存，failed=false；`ui-hdr.txt`。当前Windows桌面HDR未启用，**这项UI测试实际走HDR→SDR，不冒充HDR实屏**。
- 总交付短测：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root "C:/Users/123/Desktop/Veyra DLSS Video Player" -BuildDirectory out/remoteplay/product-repair`。首轮46.89秒通过，`logs/delivery/f6ad0f28d1de46c2a2bc7fd82723cee2/result.json`；截图/缓冲末轮修正后的结果另附本节末。

### 用户验收与边界

桌面 `Veyra PS5 测试版.lnk` 已核对指向本次构建的 `out/remoteplay/product-repair/veyra.exe`。官方1.1.1及其他实验快捷方式不是这一版。没有新发布、push或运行库替换。

用户下一步：在Windows中开启HDR，用PQ/HLG素材或确实输出HDR的采集卡，比较效果开关前后的黑位、高亮和色彩；分别试NR/SR/DLSS/XeSS及两种处理顺序。用真实5.1源与配置为5.1的HDMI/USB输出端，确认面板6→6，并逐个检查前左/前右/中置/LFE/环绕定位、暂停seek与长时间连续性。采集卡只枚举2声道时无法凭软件取得真实5.1。HDR截图需HDR看图器；HDR导出选HEVC。

尚未执行：HDR屏实测亮度/视觉验收、真实5.1扬声器、采集卡5.1与HDR实卡、RTX30/40、PS5重新连接、OBS/WGC的HDR捕获及跨HDR/SDR物理显示器移动。不承诺所有素材画质改善、不承诺全开原生4K实时、不支持Dolby/DTS码流直通或Atmos对象。NR/VideoSR是保留HDR基底的代理增强，非原生HDR模型。

运行库：patched FFmpeg `avcodec-63.dll` SHA256仍为 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`，没有回退PS5 slice补丁。增强运行组件保持原有身份，不进入源码Git。

HLG参考：[ITU-R BT.2100-2](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-S%21%21PDF-E.pdf)。该转换是1000nit参考实现，不是基于现场显示器测量的自动调光。

## B 最终软件验收记录

- 最终交付门槛：`logs/delivery/e344611bab454e2d9a23c510aac08215/result.json`，23/23，44.67秒，status=software_short_gate_passed。与当前EXE SHA256一致：`9F28BE16AC94AA33F013C7A29E32E8CE49753C445BE10AA22E572C948EEFD045`。
- HDR H.264拒绝测试exit1，正式文件和partial均未产生，`reject-h264.txt`。
- `audio-timeline-final.txt`全项PASS、进程exit0；另外文件`--underrate`和`--jitter`各exit0。51/60持续欠速下4.007秒墙钟、3.989秒声音，新增音频暂停0、欠载0。1×/2×/4×视频节奏抖动均没有丢PCM/静音补洞。证据`file-audio--underrate.txt`、`file-audio--jitter.txt`。
- 修复后的JXR独立逐像素检查、采集6ch/10ms协商、完整HDR/SDR颜色检查及最终交付短测均以当前代码通过。`git diff --check`通过；无SDK、运行库、模型、用户配置或媒体进入Git。
- XeSS验证的是HDR10初始化、真实SDK生成计数与软件原帧数值；本轮没有独立回读XeSS驱动交换链生成帧作逐像素比较。实际生成帧观感仍需用户验收。
- 对外状态：本地开发版可测，非发布版、非全部硬件验收完成。下一步只需用户进行上节列出的HDR屏/采集卡/5.1实机验收，收到日志后针对问题修复；未经新授权不发布。


## C — 2026-09-14 手动 SDR 预览输出

用户要求在 HDR 输入下自行选择 SDR 显示。采集面板增加“转为 SDR 显示（所有预览，立即生效）”，默认关闭；输入自动选项改为“自动识别 SDR / HDR · 设备元数据”。这是全局预览显示设置，作用于文件、采集和串流；不改变源 PQ/HLG 元数据，不修改 Windows HDR，不关闭 NR/SR/FG。视频导出仍按独立的 HDR 合同，截图跟随当前预览。

实现：EnhancementSettings.forceSdrPreview 与 useHdrPreview 统一启动、实时事务、显示器状态轮询和尺寸变更的输出判定，避免强制 SDR 时被 2 秒轮询反复恢复 HDR。实时切换沿用原子 drain/reset/rebuild 与失败恢复；关闭总增强时单独提交显示设置。采集面板保持开关同步，关闭后重开不丢；设置通过 v12 预设/最近应用设置保存，旧 v1–v11 默认关闭。新增 display-color 日志报告实际 hdrInput/hdrOutput/forceSdrPreview。

修改文件：apps/veyra/ui/{AppShell.cpp,CapturePanel.cpp,CapturePanel.h}、include/veyra/engine/EnhancementSettings.h、src/engine/{EngineController.cpp,PresetStore.cpp}、tests/unit/{RepairPresetTests.cpp,UiContractTests.cpp}、双语 README 与本记录/WORKLOG。没有改变 SDK、运行库、颜色 shader 或导出流程。

实际检查：
- `cmd /c out\release-1.1.0-build.cmd`：最终 exit 0，out/force-sdr-build-verified.log。首次链接失败 LNK1104 是旧 veyra.exe 仍运行，占用输出；用户确认已关闭后向残留进程正常发送关闭请求，进程退出，再构建通过。原失败记录 out/force-sdr-build.log 保留。
- `veyra_repair_preset_tests.exe out/force-sdr-presets-final.v1`：exit 0，48 组旧预设迁移、新开关 roundtrip；out/force-sdr-presets-final.log。
- `veyra_ui_contract_tests.exe out/force-sdr-ui-final`：exit 0；检查 8 种输入/显示器/强制 SDR 组合、关闭增强保留显示选择、持久化与已有布局合同；out/force-sdr-ui-final.log。
- `veyra_hdr_color_tests.exe`：exit 0，30 采集颜色 GPU 用例与 16 PQ/HLG、planar/P010、range、HDR/SDR 输出数值组合通过；out/force-sdr-color.log。测试末尾销毁未提交列表的诊断不是测试失败。
- `python out/force_sdr_ui_check.py`：真实产品加载 PQ 文件，总增强关闭，开关默认关闭，连续切换四次、关闭面板重开保持、控件在窗口内均通过；out/force-sdr-ui-live-result.txt。out/force-sdr-live.log 记录 revisions 3–6 Applied、source kept open、hdrInput=true 始终保留；18 秒 smoke 30 帧、failed=false，exit 0。素材约 1 秒，后半段为末帧显示，不冒充 18 秒持续运动视频。
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair`：23/23、45.20 秒，logs/delivery/6a7c3d4a27e140ec9145e8e016bc7453/result.json。实际 NR CreateFeature id18 result0x1、SEH0；60 次 Evaluate、NVOF 非零运动，SDR4K导出等原有回归通过。
- `git diff --check` 通过。EXE SHA256 9D3DFFA2D397F00F512B962A349453430CD3F0039ECF55FAB9961279291B2C76。patched avcodec-63.dll 仍为 0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。

当前 Windows HDR 关闭：本轮 UI 实测均 hdrOutput=false；开关从 HDR 交换链切到 SDR 的物理显示验收、跨 HDR 屏移动及 HDR 实卡仍未执行。GPU 色调映射数值通过不等于 HDR 显示器观感通过。下一步用户从桌面“Veyra PS5 测试版”打开采集面板，在实际 HDR 输入/显示器上开关比较；不用重连采集卡。未发布或推送 GitHub。
