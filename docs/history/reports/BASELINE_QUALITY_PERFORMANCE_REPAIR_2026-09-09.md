# 基础画质与性能修复执行文档 — 2026-09-09

> 后续执行状态：用户取消实卡A/B后，原生转换与调度修复已实施并通过离线GPU/软件短测。见 [实际修复记录](BASELINE_REPAIR_IMPLEMENTATION_2026-09-09.md)。本文下方为开工前审计与原计划，不能当作最新“未实施”状态。

状态：**排查已有实证，产品修复尚未实施；Phase 7 继续 in_progress，基础画质验收重开。**

本文件对应用户最新反馈：OBS 原始采集预览更清晰，Veyra 出现色彩断层；Magpie 叠加增强后体感与 GPU 占用明显更好。用户要求先查原因、交接给下一位 Agent 修复，并授权必要时复用开源源码。本轮不继续堆功能、不重做前端、不加重导出完整性检查。单次测试不超过 300 秒，可以进行多次独立短测。

本文件覆盖旧交接中“先做统计窗口”的优先顺序：**先取得基础画质失真的同帧证据并修复，再优化同条件增强性能。** 用户体感是缺陷报告，不可因一次旧 gate 通过而驳回；同样不能把尚未证实的猜测写成根因。

## 1. 接手时先看这六条

1. 当前源码 HEAD：`181a047269a761d93b5e408c4b0008fa8477c89d`，分支 `agent/veyra-v1-loop`。开工时已有未提交交接/loop 文档和两份竞品方案；保留它们，不 reset/clean。
2. **确实发现采集设计缺口：** `CaptureCardSource.cpp` 强制 DirectShow 输出 RGB32，丢失原生 YUY2 与颜色来源信息；OBS 当前实测是直接接收 YUY2。这是第一条需要用同帧像素证实/排除的失真链。
3. **没有证明整个 GPU bypass 都错误。** 本轮真实 RTX GPU 窄测，256×64 RGB 多色阶图与 BGR0 红蓝细线经共享图关闭增强后的最大 8-bit 通道误差均为 0。此测试未经过实卡转换、窗口缩放、DWM 或显示器。
4. **当前找到的 Magpie 日志不支持“同样三项全开输出 4K”。** 日志中的 SR/NR/FG 多为 2560×1440，较早 SR 为 1535×971→2276×1440。不能由这些数据推算 3840×2160 下的优势倍数。继续排查 Veyra，不能用此差异替基础画质缺陷开脱。
5. **DLL 多不等于速度快或功能完整。** Veyra 已有可工作的 D3D12 NGX 视频 SR；Magpie 走另一套 VFX/CUDA 接口。没有证据证明 Veyra 少打包了其当前后端运行所必需的 DLL。不要为了“补全依赖”先塞一套 VFX SDK。
6. `Likely7/Veyra-NRVideo` 的 `v0.0.1` **已公开发布**。2026-09-09 本轮 `gh release view` 返回 `isDraft=false,isPrerelease=false,publishedAt=2026-09-09T11:00:36Z`。不要再向用户说尚未发布。本轮未改动远端 Release；后续不要把旧版标为画质已验收。

## 2. 本轮证据与边界

本地证据目录（gitignored）：`logs/obs-magpie-rootcause-20260909/`。

| 证据 | 内容 |
| --- | --- |
| `evidence/magpie.log`、`evidence/magpie.1.log` | 用户实测产生的原始日志快照，避免下次启动轮转覆盖 |
| `evidence/magpie-config.json` | 用户最后保存的效果组快照；不可当作较早所有测试的配置 |
| `evidence/obs.log` | OBS 2026-09-09 19:22–19:55 会话；含真实采集类型/分辨率/帧率 |
| `evidence/magpie-build-manifest.json` | Magpie 0.6.6，源码 commit `9824d758b162ad3c5b5acc81e2e14c83f138e13d` |
| `upstream/Magpie/` | 上述精确 commit 的 SR/NR/FG/VFX/光流/Renderer 源码及 LICENSE，仅供本地对照；未执行其脚本或编译 |
| `upstream/obs-win-dshow.cpp` | OBS commit `012c6c23c73283ee5591ae00d08af14d9eeb8279` 的官方采集实现；这是源码参考，不宣称与用户安装 OBS 完全同版 |
| `build_probe.py`、`baseline_probe.cpp`、`build_probe.cmd` | 本轮诊断入口：复用当前产品库与现有测试函数，不改生产源码 |
| `build-probe.log`、`baseline-probe.log`、`baseline-result.json` | 窄测构建/执行与探针 EXE 身份 |
| `baseline-images/256x64-off.png` | GPU 基础图输出；诊断回读仅用于审计，不加入正常采集路径 |
| `audit-manifest.json` | 本轮保存的日志、代码、二进制/Shader 身份与状态，见生成记录 |

本轮命令与实际结果：

```powershell
git status --short
git log -3 --oneline
python logs/obs-magpie-rootcause-20260909/build_probe.py
gh release view v0.0.1 -R Likely7/Veyra-NRVideo --json url,isDraft,isPrerelease,publishedAt
python logs/obs-magpie-rootcause-20260909/finalize_audit.py
git diff --check
```

探针最终构建与运行均 exit 0，输出：

```text
IMAGE_DIMENSION 256x64 nr=0 maxError8=0 nrEvaluations=0 pass=1
CAPTURE_RGB nr=0 maxError8=0 cuts=1 nrEvaluations=0 nvof=0 pass=1
```

首次探针构建包装脚本因只匹配 `/c`、实际 Ninja 使用 `-c` 而 StopIteration；修正命令匹配后完成。不是产品编译或 GPU 故障。单次最终调用约 5.4 秒。

交接整理脚本仅生成本地证据 manifest 并检查 JSON/身份，未运行 GPU；保存 39 项审计文件与 28 项产品/Shader/运行时身份，原版 NR 哈希匹配。只读比较确认既有控制面漂移为 AGENTS.md、README.md；没有更改锁定值。

**未执行：** 新的实卡 A/B、同帧 OBS/Veyra截图、同配置 Magpie/Veyra 性能测试、SR/NR/FG 本轮 Create/Evaluate、完整 delivery gate、独立 Reviewer。当前运行的 Veyra 会话未关闭。不要把用户日志和历史测试冒充本轮受控实验。

## 3. Magpie 实际测了什么

### 3.1 从日志纠正比较条件

OBS `evidence/obs.log`：

- 19:41:01：USB3 Video，1920×1080、60fps、YUY2，buffering disabled。
- 19:41:12 起一段：3840×2160、18fps、YUY2。
- 19:42:09：改回 1920×1080、60fps、YUY2。
- OBS 画布/输出设置是 2560×1440，显示器日志为 2560×1440、100Hz、8-bit SDR。这不代表每个来源都经过编码设置中的 NV12；不能把 OBS 编码输出格式误当预览纹理格式。

Magpie `evidence/magpie.1.log`：

- L1206–1217：NR 1535×971；DLSS SR 1535×971→2276×1440；FG backbuffer 2276×1440、2×；目标矩形 2276×1440。
- L1816–1819：NR 2560×1440；SR 2560×1440→2560×1440；FG 2560×1440、2×。
- L2154–2157：同为 2560×1440，SR 仍为 1:1，光流参数有所变化。

Magpie `evidence/magpie.log`：

- L1155–1166：NR 2560×1440、未开启 NR 输入降分辨率；VFX quality=4，input/output format=28；FG 2560×1440、2×；目标矩形 2560×1440。
- L1531–1541：最后一段 NR 2560×1440 + FG 2560×1440 2×，该段没有 SR 初始化；最后保存的“DLSS5+超分+帧生成”效果组实际上只有 NR 和 FG 两项。
- L1575 以后多个 NR GPU 窗口均值约 9.7–10.1ms；最终呈现 ring 日志 captured 约 35–37fps、queued 约 70–73fps。queued 是提交统计，不是独立测得的物理显示帧率，也不必然等于 HDMI 内容变化率。

4K 是 1440p 的 2.25 倍像素。这里不能据此推导线性耗时比，但应明确它们负载不同。“Ultra”是质量名称，不会自动把输出改为 4K；效果组名字也不能证明内部功能已启用。旧助手曾把界面设置、文件清单和实际处理等同，这些说法不应继续沿用。

### 3.2 可借鉴的实现（已核对精确源码）

| 模块 | Magpie 实际实现 | Veyra 当前实现 | 后续动作 |
| --- | --- | --- | --- |
| 视频 SR | `RTXVideoDenoiser.cpp:106–231`：`NvVFX_CreateEffect("VideoSuperRes")`、RGBA U8 CUDA图像、D3D11映射/Transfer、`NvVFX_Run`，最后显式 stream synchronize | `VideoSrBackend.cpp:10–41`：D3D12 NGX `Feature_Reserved16`，`Input1/Output` 与 `VSR.QualityLevel` | 接口不同；用同帧/同尺寸/同质量比较后才决定要不要增加 VFX 后端。不要宣称对方零复制、零等待 |
| 游戏 SR | `DLSSSRUpscaler.cpp:140–156`：Balanced、preset J、低分辨率 motion、AutoExposure；输入输出来自效果链 | `DlssSrBackend.cpp:90–104`：线性 FP16、IsHDR+AutoExposure、输出尺寸 motion | 核对资源语义，不机械复制标志。先做同尺寸成本与画质对照；SR flags 影响质量也影响成本 |
| NR 缩放 | `DLSSNRFilter.cpp`：Lanczos2 抗混叠降采样，FP16有符号残差，Catmull-Rom 分离两遍上采样 | `NrDownsample.hlsl` 区域平均、`NrResidualComposite.hlsl` 2×2边缘引导残差 | 纹理、渐变、光晕、闪烁同源评测；更复杂滤波也可能更慢/振铃，不能只看锐度 |
| 处理顺序 | 用户早期日志 NR 在1535×971，SR之后才到2276×1440；效果组允许顺序配置 | 固定 SR→NR→FG，实时NR会再降采样并回填 | 保持现有顺序作为基线；可另做 NR→SR→FG 实验比较。不能未经比较就替换默认或把较低NR尺寸算成算法提速 |
| 光流 | 可选双向；用户日志 profile=2S、grid2、SLOW，零depth | 源分辨率 NVOF、grid/置信度、FlowAdapt给不同consumer | 优先复用已验证的光流语义和时序测试；不加常驻深度来碰运气 |
| 调度 | 有界呈现ring=2，独立前后端发布及fence；仍有CPU等待 | `EngineController.cpp:180–205` 在同一循环等待整批FG，再按PTS呈现，完成后才取下一源帧 | 解耦源/处理/呈现并保留lease和有界背压，不是删除fence |

精确源码入口：

- [Magpie VFX 后端](https://github.com/SAOG0721/Magpie/blob/9824d758b162ad3c5b5acc81e2e14c83f138e13d/src/Magpie.Core/RTXVideoDenoiser.cpp)
- [Magpie NR 与滤波](https://github.com/SAOG0721/Magpie/blob/9824d758b162ad3c5b5acc81e2e14c83f138e13d/src/Magpie.Core/DLSSNRFilter.cpp)
- [Magpie 呈现调度](https://github.com/SAOG0721/Magpie/blob/9824d758b162ad3c5b5acc81e2e14c83f138e13d/src/Magpie.Core/Renderer.cpp)
- [OBS DirectShow 输入](https://github.com/obsproject/obs-studio/blob/012c6c23c73283ee5591ae00d08af14d9eeb8279/plugins/win-dshow/win-dshow.cpp)

## 4. 基础画质：已确定的事实和待证实根因

### Q1：采集前置颜色转换不受控（P0，首查）

`src/source/CaptureCardSource.cpp:95–114`：SetFormat选择上游YUY2后，SampleGrabber强制 `MEDIASUBTYPE_RGB32`，再用 `RenderStream` 自动连接。中间允许插入解码/颜色转换filter，但当前没有记录实际插入的CLSID与各pin media type。回调收到的是已转好的8-bit BGR0；随后只设置 `color_range=JPEG`，矩阵/transfer由 `ColorMetadata.h` 默认推断为709/sRGB。

**确认的缺口：** 转换并非 Veyra 可验证的一次显式YUY2→RGB；上游range/matrix/chroma位置/transfer无法从当前packet追溯。默认猜测被包装成完整RGB图后，后续GPU再正确也无法恢复之前丢失的颜色和细节。

**尚未证明：** 是哪个具体filter把这张卡转坏、用了601还是709、是否压缩了范围。不要写成已确认“601误用”或“6-bit量化”。

OBS源码 `win-dshow.cpp:417–438,553–575,1169–1174` 明确把YUY2作为YUY2传给 `obs_source_frame2`，保留每行 `width*2` 和显式颜色参数。可复用其capture格式/元数据处理；不要移植整套OBS界面。

### Q2：显示缩放质量与颜色域（P0/P1）

`src/engine/VideoPresenter.cpp:43–51` 和 `shaders/PresentBlit.hlsl` 使用线性采样器对已编码的RGBA8结果作窗口缩放。`src/gfx/PresentSink.cpp:111–117` 为8-bit UNORM交换链；当前没有显式检查/选择输出色彩空间，也没有最终量化抖动。

已确认只有双线性窗口缩放，缩小时缺少专用抗混叠滤波。它能解释部分细节/边缘观感差，但**不能仅凭这点断言就是用户所见色带的根因**。8-bit SDR本身并非bug；RGB无增强窄测已保留256级输入。直接改10-bit或加deband不能代替找到错误发生的位置。

NR实时回填期间发生的色带应另查 `ParityEncode/Decode`、8-bit Proxy/Neural、残差上采样/饱和裁剪，不可与“所有增强关闭”混为同一问题。

### Q3：缺少真正贯穿采集到显示的画质门禁（P0）

已有 `CaptureSourceTests.cpp` 检查回调、mailbox、读者生命周期、帧龄，没有同帧颜色准确度。已有 `captureRgb(false)` 在**转换后的BGR0入口**注入合成帧，因此即使上游DirectShow转坏它仍然通过。本轮的maxError=0也只能证明这段。

当前证据能把问题范围缩小为：**优先采集原生→RGB转换，以及GPU结果→最终窗口显示；若只在NR开启时出现，则进入NR分支单查。** 色带第一责任节点仍待原始样本确认；禁止把候选原因写成完整闭环根因。

## 5. 性能：代码中确实存在的成本

| 项目 | 位置与事实 | 修复边界 |
| --- | --- | --- |
| 输出尺寸固定冲4K | `ResolutionPlan.h:20`：SR开启就按3840×2160盒子放大；FG也处理base尺寸，不跟随1440p显示窗口 | 明确显示/输出目标，提供源/1440p/2160p或适配窗口；4K仍可选，导出保持选定原生尺寸，不静默降画质 |
| CPU采集RGB扩展与重排 | `CaptureCardSource.cpp:53` RGB32全帧mailbox复制；`EnhanceGraph.cpp:698–717` BGR0逐像素CPU重排为RGBA，再upload | 原生YUY2为2字节/像素，RGB32为4；GPU上传原格式并转换可省CPU重排和部分带宽。别把原始YUY2说成需要NVDEC硬解 |
| 同线程处理+呈现 | `EngineController.cpp:180–205` 完整FG结果等待和deadline等待阻止下一次graph提交 | 有界生产/消费解耦，记录ready/lease/consumer fence；过期只丢允许丢的采集帧/子帧 |
| 逐帧多pass与冗余副本 | `EnhanceGraph.cpp:956–958` 总是复制input/base reference；NR原生尺寸仍经过downsample/残差等pass；源和working同尺寸仍有blit | 根据功能做pass计划与资源复用；相同引用可alias，但不能破坏同帧对比、FG历史或在途资源 |
| 常规事件同步写盘 | `src/base/Log.cpp:write` 每条格式化、锁、写文件和flush；VSR与SR每次Evaluate也记录成功 | 错误/身份/设置事务保留；常规帧事件有界缓冲/聚合，测吞吐与证据丢失计数 |
| 统计跨配置 | `TimingWindow`/lateness/sourceFrames跨revision；每帧为多组1200样本排序 | 按实际applied revision重建统计窗口，低频刷新，区分GPU阶段/CPU等待/源吞吐/提交率 |
| FRUC reset风暴 | `EnhanceGraph.cpp:1072` reset触发worker重建；drop再reset可形成反馈 | 先计原因和耗时，验证轻量reseed等价后替换；不是关闭reset或吞掉错误 |
| GPU/CPU依赖混合测时 | FG阶段包含等待/IPC；不能用单阶段NR表盘说“总延迟” | 使用同批次时间轴；P95相加无意义；GPU利用率并非画质/延迟指标 |

过去实卡日志的低VSR+实时NR约59.51源fps，高VSR+原生NR约30fps，是历史同卡证据。可用来选诊断起点，不能直接与本轮Magpie1440p数据算速度倍数，也不能以“5070不够”结束调查。

## 6. 下一位 Agent 的执行顺序

### R0：保存现场，建立可比输入（必须先做）

- 读取本文和最新AGENTS/HANDOFF，记录git dirty、EXE/Shader/runtime SHA、适配器/驱动/屏幕/设置revision。
- 保留本轮OBS/Magpie快照；用户运行时的设置文件不擅自改写。每个新测试独立run-id，进程有不超过300秒的外部上限。
- 使用固定静止测试信号经HDMI进同一张卡，含黑阶0–32、完整灰阶、近黑彩色渐变、饱和色块、1/2像素彩色细线、斜线与文字。另用同一个有帧号的移动视频测试时序。
- OBS和Veyra必须协商相同卡/针脚/1920×1080 60 YUY2或3840×2160 18 YUY2；确认源输出SDR、范围、画面比例和裁剪一致。不要把不同时间运动画面直接相减。
- 单GPU测试串行。读取用户日志无需抢占设备；需要实卡自动化时先确认进程占用，必要时使用文件重放完成独立部分。

### R1：四段抓帧，锁定色带首次出现的位置（P0，第一条实施任务）

新增仅诊断启用的 `CaptureColorProbe` 和 `CaptureColorContractTests`，正常播放不开启回读。生产输入元数据扩展留在共享source/pipeline接口。

抓取同一个静止图/确定的帧号：

1. **A 原生**：DirectShow源输出pin的YUY2/NV12原始sample、实际stride、orientation、色彩元数据及provenance。
2. **B 现有转换**：当前自动filter输出BGR0，用同一份原生sample重放给转换链，或静止源顺序采集；明确标注是否严格同帧。
3. **C GPU结果**：RGB ingress线性FP16和无增强videoFrame，保持源分辨率1:1。
4. **D 显示**：实际VideoPresenter backbuffer与OBS同尺寸预览/源截图。backbuffer正确而窗口错误时，查DWM、acrylic视频隔离、缩放/DPI、ICC/HDR/驱动视频颜色处理；不再修改前面已正确节点。

每段输出尺寸/格式/颜色契约、逐通道最大误差/RMSE、灰阶单调性、每段灰阶distinct levels、黑白端点、连续平台长度、彩色细线对比度；PNG无损保存，不用JPEG/社交软件缩略图。

建立独立参考：原生YUY2按确定的601/709+full/limited转换，与明确配置的FFmpeg及OBS交叉核对。中性色块、ramp内部远离色度边界可用≤1–2码值误差起步；不同chroma重建滤波的边缘误差单列，不能用全图宽阈值掩盖range错误。

**退出条件：报告必须写出“A/B/C/D哪一步首次偏离”，并保存失败样本。若未找到，不准宣布色带已修复。**

### R2：按证据修采集与无增强路径（P0）

涉及 `CaptureCardSource.cpp/.h`、`ColorMetadata.h`、`FramePacket.h`、`EnhanceGraph.cpp`、新YUY2转换shader、`CapturePanel.cpp`。

- 常态优先原生YUY2/NV12，显式协商/回读media type；关闭任意RGB转换filter自动插入。支持RGB32作为有日志的兼容路径，MJPEG单独明确解码；保留格式枚举和4K18。
- 类型解析支持VideoInfo/VideoInfo2、stride、负高度/方向、sample长度检查；YUY2的422色度不能先转NV12丢成420。保留latest-frame mailbox+读者所有权，不持有驱动单sample导致饥饿。
- 增加设备颜色设置：自动/601/709、自动/limited/full；自动推断显示assumed，设置按设备模式保存并实时安全应用。传递已知元数据，别对未知值装作确定。
- GPU直接消费packed格式，重排/范围/矩阵集中一次完成；旁路保持输入码值和细节。关闭全部增强时不运行NR、SR、FG、光流或无用的残差路径。
- 明确区分video signal transfer和display transfer；不要仅看到BT.709标签就把所有视频强制套同一幂函数。用R1参考图决定等价契约。
- 所有修改必须同时回归图像/视频/采集，不复制三套转换实现。

**验收：R1坏样本恢复、1:1颜色误差可解释、无额外色度降采样；真实1080p60/4K18能开关/重连，audio/PTS不退化。**

### R3：修最终显示（P0/P1，依R1结果）

- 1:1保证像素中心正确；缩小时用抗混叠缩放，放大时提供与OBS相当的bicubic/Lanczos路径或明确显示当前过滤器。
- FP16颜色尽量保留到最后一次输出量化；只有需要RGBA8的后端边界才转换。先修错误，再按渐变证据决定是否加入轻量最终dither。
- 明确SDR swapchain色彩空间/支持查询；确认视频alpha=1且不被Desktop Acrylic调色。换壁纸/移动到底色不同窗口上，视频区像素应保持一致。
- A/B比较必须同一有效画面尺寸与缩放方法；导出PNG正确而屏幕差，优先检查此阶段。
- 测试窗口缩放/专业模式切换/100%与125%或150%DPI/全屏，视频几何与黑边保持；现有窗口崩溃回归不能被破坏。

### R4：配置与性能基线（P1，画质正确后）

首先修revision统计与实际分辨率展示。明确source、SR输入输出、NR、flow、FG、present texture、窗口/物理屏幕尺寸；不再只写“超分4K”。

最小矩阵：全关、SR低、NR原生、NR实时、SR+NR、SR+NR+DLSS2X；再选代表性场景比较4X和FRUC2X/4X。1080p60与4K18分开；文件15fps可用于15→60的4X验证。

与Magpie先对齐**两端实际相同输出/NR尺寸/源帧率/后端/倍率/内容**，包括一组1440p和一组真实2160p。在1440p显示器上内部4K再缩回1440p必须标明，不叫物理4K显示。重复/缺帧和切镜样本不能只取流畅段。

每次10–30秒验证可运行，然后60–120秒稳态测量；预热单列。记录CPU采集/重排/提交/等待、各GPU阶段、源真实处理率、有效子帧率、提交间隔P50/P95/P99/max、过期数、reset耗时、GPU功耗/频率/温度。GPU 60%与99%只作辅助，不能当主验收。

### R5：优化已测瓶颈（P1）

一次只改一个可回退模块：

1. 原生capture格式GPU转换，减少CPU全帧copy/reorder。
2. 按功能裁剪pass/分配；同尺寸旁路用资源引用，去掉无消费者的重复副本；保持对比lease。
3. 有界处理/呈现解耦，呈现线程等待deadline不阻塞下一个源帧的GPU提交；资源只能在consumer fence完成后复用。设置/seek/drop用epoch作废，不能凭线程数声称并行。
4. 常规日志批量写入、统计低频计算；错误和关键状态保留。
5. 修FRUC reset重建风暴；单独量化IPC、GPU等待和算法成本，必要时试官方支持的CUDA互通，保持失败回退。

**验收：** 同源同质量输出无退化，阶段GPU/CPU时间和呈现长间隔至少有明确可复现改善，帧龄不因隐藏大队列增加；源丢帧、有效生成数、用户体感一起报告。不能把降低模型尺寸/强度叫同等质量的纯性能优化。

### R6：有限复用Magpie/OBS（用户已授权）

Veyra根LICENSE已经是GPLv3，用户本次明确允许必要时copy/复用开源代码。可以按兼容许可证直接移植**具体模块**，不用再坚持已失效的闭源禁抄假设。

该最新用户决定记录在本文与HANDOFF；本轮不修改锁定的AGENTS或CONTROL_HASHES。接手者应结合仓库现有GPLv3 LICENSE与用户本次明确复用授权理解旧“闭源Veyra”的条件，而不是从旧措辞推导出必须重新征求同一授权。

推荐优先级：OBS原生capture类型/颜色契约 → Magpie残差缩放/光流与生成有效性处理 → 呈现节奏机制 → VFX视频后端（仅同条件实测值得时）。保持Veyra现有Win32 UI、三入口共享引擎和D3D12主干；D3D11模块需明确interop代价，不为了复制方便把产品改成必须先开OBS。

每个复用项创建来源记录：仓库URL、固定commit、原文件/函数、许可证和copyright、复制范围、修改说明、单独回归证据。保留原作者声明，更新 `THIRD_PARTY_NOTICES.md`；禁止改名抹掉归因。公开说明仍讲Veyra功能，但许可证归因不能因“README不提别的项目”而删除。

源码GPL不覆盖其NVIDIA SDK/DLL。现有固定身份运行时规则保留；本次授权不是把Magpie社区patch版NR替换进Veyra、改DLL或把SDK提交Git的授权。先用已批准的原版NR测调用/颜色/滤波；若确需比较另一runtime，作为独立变量单独处理。

## 7. 必须新增的可失败门禁

以下名字是**待实现目标**，不要声称已经存在或通过：

- `CaptureColorContractTests`：native YUY2/NV12/BGR0、601/709、full/limited、stride/方向、未知元数据；测试真实生产转换函数。
- `BaselinePresentationColorTests`：全关RGB/YUY2渐变→present读回；1:1与缩放、alpha隔离、near-black和chroma细线；同时测试NR=off与总开关off的实际applied状态。
- 同源性能报告器：按revision与run-id隔离统计，输出阶段耗时/提交间隔/帧龄/有效FG，解析真实日志而非GUI理论数。

之后运行与变更有关的现有窄测，再运行现有统一 `scripts/gates/delivery.ps1` / phase7入口；**现有delivery通过不能代替新增基础画质检查。** 完整命令以脚本参数为准，单次进程有300秒上限；导出完整性检查保持原状。

已经存在且本轮成功运行的最窄诊断命令：

```powershell
python logs/obs-magpie-rootcause-20260909/build_probe.py
```

它基于现有编译库生成旁路探针。生产代码改动后先构建对应库再重跑；切勿把旧库结果当新源码通过。

## 8. 交付要求与停止条件

- 第一份代码交付必须含“色带首次出现节点”的失败/修复样本与命令；不要先改皮肤、换显卡、加锐化/deband掩盖。
- 文档区分：用户观察、本机日志、源码事实、本轮执行、仍待验证；未验证的FPS与性能倍率不进入README/Release。
- 不因用户着急清掉失败日志、改门槛、删除必要fence/reset；不自动添加深度模型或OCR。
- 相同问题三个不同方案仍无新证据时，保存样本与失败指纹，不循环随机改参数。
- 最终需要单独的新上下文只读复核与用户实卡验收；此文不授予自己签署Reviewer PASS。
- 当前 `loop/CONTROL_HASHES.json` 有早期AGENTS/README锁定值；发布阶段这些文件已改过。接手先报告实际漂移及授权来源，不通过改manifest/gate伪造preflight通过。本次未改控制hash。
- 已发布0.0.1的状态处理与后续版本发布单独执行；本轮没有推送、替换、删除Release或修改用户运行时。

**下一条唯一实施任务：R1，添加诊断用原生采集与转换后/显示后同帧抓取，查明损失首次出现的位置，并以坏样本驱动R2/R3修复。**
