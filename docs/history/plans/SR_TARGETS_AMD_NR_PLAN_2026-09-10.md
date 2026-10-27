# 2K / 8K 超分与 AMD NR 接入方案

> 当前续接：[后端切换、真实帧率与音画同步修复方案](CONTINUATION_REPAIR_PLAN_2026-09-10.md)。同日后续已实现SR目标、XeSS预览与AMD光流，AMD NR本体仍未接入。下方研究阶段的“尚未实施”、S0下一步及Phase标签属于历史状态，不再作为当前执行顺序；固定来源和技术约束继续作为研究依据。

日期：2026-09-10。状态：方案与来源核查完成，产品尚未实施。Phase 7 仍为 `in_progress`。

本文件补充 [XeSS 补帧与 AMD 光流方案](XESS_AMD_OPTICAL_FLOW_PLAN_2026-09-10.md)。用户继续要求增加 2K / 8K 超分选项，并寻找让 AMD 显卡运行 DLSS5 的方法；本轮沿用上一请求的“先找方法、写方案”范围，没有把研究结果写成应用已支持。

## 1. 结论

1. 超分目标增加 `2K (2560x1440)`、保留 `4K (3840x2160)`、增加 `8K (7680x4320)`。2K 在本产品中特指 QHD，不是影院 DCI 2048x1080。目标分辨率和 DLSS / RTX Video SR 算法档位独立，旧预设默认仍为 4K。
2. **AMD 运行 NR 确实有公开项目，不能再笼统说不可能。** 已找到 HIP 网络重写、ZLUDA CUDA 兼容和原生 D3D12/HLSL 网络重写三条路线。它们不是把 FSR 改名成 DLSS5，也不是只使用 AMD 光流。
3. 推荐以 **MIT 的原生 D3D12/HLSL 网络重写版作为 RDNA4 接入候选**，先做独立正确性实验；ZLUDA 保留为另一条离线兼容实验。现成 HIP 游戏 mod 没有公开稳定 SDK，且当前许可证禁止打包分发，不作为 Veyra 默认依赖。
4. AMD NR 不等于 AMD 支持 NVIDIA 的 SR、NVOF、FRUC、DLSSG 或 NVENC。完整 AMD 组合还要有自己的超分、运动估计、补帧和编码能力。原来的 NVIDIA 路线继续保留，不能把现有调用统统指向一个“AMD DLL”。
5. 现有公开数据不支持承诺 AMD 原生 4K NR 60fps。下面的速度都是作者记录，**本轮没有复现**；本机也没有这些网络移植已经验证的 RX 9070 XT。

## 2. 找到的项目和真实边界

| 项目 | 已核对的方法与作者证据 | 对 Veyra 的价值与限制 |
| --- | --- | --- |
| `danielblnc/DLSS-NR-on-AMD` | 作者称从头写 HIP kernels / memory layouts，以 FSR 游戏输入驱动网络；RX 9070 XT 约 1080p 33fps；公开最新 Release 为 v0.2.17 | 很可能是用户记得的项目之一。证明有 AMD 网络执行路线，但仓库只有说明、许可和 notices，没有核心源码或可直接调用的 SDK |
| `RedDukeDev/dlss5-image-enhancer-zluda` | 自定义 NGX CUDA host + 自己的 ZLUDA/LLVM fork + HIP，加载 NVIDIA NR 网络；作者目标 RDNA4 / RDNA3，RDNA3涉及精度模拟 | MIT 的兼容层代码可审查。作者明确说明性能很差、偶发空白图、首次编译很久；当前不能作为实时采集承诺 |
| `lmxxf/dlss5-on-amd-9070xt-porting` | D3D12 / SM6.10 HLSL wave-matrix，FP8 E4M3，71-block 网络；作者称 RX 9070 XT、1080p《剑星》27-28fps，网络约30ms | 核心代码 MIT，适合去掉游戏接入层后封装为自有 NR backend。当前核心写死1920x1152 padded geometry，尚不是任意尺寸库 |
| `zmodelerlover/dlss5-neural-amd` | ReShade 调用第三方 AMD runtime；作者仅验证 RX 9070 XT / PCSX2。1920x1080 backbuffer、宽高0.5 scale、1 pass，约15-16ms/job，3932/3960帧有新校正 | 可参考颜色准备、残差回填和成功统计。0.5是宽高都减半，不能记成1080p网络耗时。接入依赖特定已修补runtime与私有偏移，不采用其加载路径 |
| `eikkapine/DLSS5-AMD-Video` | 独立离线视频，调用上面的HIP runtime；作者120帧1920x1080样本共408.98秒，3.41秒/帧，输出24fps | 证明离线视频包装存在。3.41秒包含提取、隔离worker、推理、编码与验证，不能当纯GPU推理时间；24fps是文件播放率，不是处理速度 |
| `MatheusGViana/dlss-5-amd-project` / `Vodkaman23/DLSS-NR-UE5-Opti-DLL` | 前者宣布最后一次实验更新；后者为简短说明和四个DLL。未找到根目录的明确项目许可 | 保留为社区对照线索；没有可直接复用且身份/许可明确的普通SDK。本轮只查树和文本，不下载包或DLL |

需要纠正的两类宣传：

- danielblnc 的 [issue #130](https://github.com/danielblnc/DLSS-NR-on-AMD/issues/130) 有“Native 4K 60+ FPS”标题，但正文同时启用 FSR、帧生成与多项配置，缺少每帧 NR 内部尺寸/完成计数。评论也有帧率下降与效果较弱的反馈。它是用户帖，不是作者的等条件 benchmark，不能覆盖 README 的约1080p33fps记录。
- HLSL 重写项目的约42dB PSNR，是作者 fast chain 对其自身 exact reference chain 的结果，不是 Veyra 同源与 NVIDIA 官方完整游戏链等价的证明，更不是主观画质提升指标。

## 3. 2K / 4K / 8K 的产品合同

### 3.1 尺寸和界面

专业面板分为三个相邻控件：`超分辨率` 开关、`目标分辨率` 三段选择、`超分算法/质量` 现有选择器。沿用当前主题、实时生效、滚轮只滚页面和还原默认行为；底栏 SR 快捷开关继续使用已保存目标，不每次重置到4K。

| 选择 | 16:9 输出边界 | 相对4K像素量 | 默认策略 |
| --- | --- | --- | --- |
| 2K | 2560x1440 | 约0.444倍 | 常用较轻目标 |
| 4K | 3840x2160 | 1倍 | 旧预设/内建默认 |
| 8K | 7680x4320 | 4倍 | 可选实验目标；是否可用取决于实际后端与资源 |

使用含入目标矩形的等比放大，沿用偶数输出尺寸和不主动缩小原图的语义。例如 1448x1086 到2K得到1920x1440，到8K得到5760x4320。竖图不会被拉伸成横图。原始图片长边大于目标框时保留原尺寸、SR旁路，不以“选了2K”为由拒绝/截断大图。

目标、实际SR输出、NR内部、FG处理、窗口/交换链尺寸分别记录。8K纹理显示在2K屏幕只是缩小预览；不能叫屏幕原生8K。实时NR仍按明确标注的约1080内部处理并回填；原生NR是独立选择。

### 3.2 现有代码要改哪里

| 文件 | 已确认现状 | 实施内容 |
| --- | --- | --- |
| `include/veyra/pipeline/ResolutionPlan.h` | `make` 写死3840x2160；单纹理有效上限16384 | 新增强类型SR目标及其边界，默认4K；所有来源共用同一计算函数 |
| `include/veyra/engine/EnhancementSettings.h` | 只有SR开关和`videoSrQuality` | 新增`srTarget`、有效值校验和完整比较；不复用质量字段编码尺寸 |
| `src/engine/PresetStore.cpp` | 当前schema4，支持旧schema1-4 | 升级schema5，追加目标；旧文件按4K解释，未知值拒绝且保留原文件 |
| `apps/veyra/SettingsWindow.cpp` | 开关写着“超分辨率 · 4K” | 独立目标选择；每次通知只改目标字段；隐藏页/草稿不能覆盖NR档位或FG倍率 |
| `src/engine/EngineController.cpp` | 初始化、实时重配、metrics分别调用ResolutionPlan | 三处传同一目标；按最终尺寸重建；首帧Evaluate验证后才提交applied，失败恢复旧图与参数 |
| `src/engine/VideoExportJob.cpp` | 共用ResolutionPlan，但入口仍有`>3840/>2160`早退 | 冻结目标并传到输出；移除产品硬编码早退，换为实际来源/纹理/后端能力检查，精确解释不支持原因 |
| `src/sink/NvencD3D12Encoder.cpp` | 当前直接初始化所选codec，没有查询最大宽高 | 添加开作业时一次能力查询；保留现有CFR、音轨、partial和完整性检查，不增加逐帧扫描 |
| `src/engine/EngineControllerImage.cpp` / `TiledImageProcessor.cpp` | 大图分块仅NR、保留源尺寸；tile实现明确拒绝SR | 单独接全图SR输出规划，或明确告知该分块输入尚不适用所选SR；不能勾选成功却静默忽略SR |

输入图片本来不足8K但SR后超出大图阈值的情况也要覆盖。现有大图分支只根据输入面积分流，所以不能以“已经有大图分块”推断8K增强已经安全。

### 3.3 8K 后端验证与显存

一张7680x4320 RGBA16F纹理约253.1MiB，RGBA8约126.6MiB。当前`EnhanceGraph::createResources`还会创建2张真实输出、6张生成输出、source/base参考和多张motion/depth/residual纹理；关闭FG也仍分配生成纹理。它们不是NR网络的全部显存，不能用一张输出纹理的大小估算整条链。

施工前后都必须检查：

1. 枚举真正存活的资源及别名，用`GetResourceAllocationInfo`计算本应用分配；结合`QueryVideoMemoryInfo`和网络工作区测得峰值。考虑播放与导出并存、重配期间旧资源是否尚存活，不靠Windows显存换页维持假可用。
2. 保持有界帧池和当前4-6 command slots；能按功能省掉未使用资源时，再同步修好descriptor有效性与实时开关重建，不能简单删纹理后留下空SRV/UAV。
3. 首测SR-only的1080p到2K、1080p到8K、4K到8K。DLSS SR与RTX Video SR分开验证Create、Evaluate、实际纹理尺寸、非黑输出和细节；D3D12支持16K纹理并不证明这些SDK支持8K。
4. 官方RTX Video SDK 1.1的VSR接口接受输入/输出矩形；本次查到的指南与公开页面没有足以承诺任意8K输出的能力表。不能从UI设置成功推出后端支持，也不能自动以4K增强再普通缩放冒充8K SR。
5. 8K NR与8K FG分别试；8K SR成功不能推导全组合成功。资源不足或后端拒绝时保留原会话，显示实际原因。不静默改4K、不切错算法、不降低用户所选原生NR。
6. 本次先保留现有`FG=base/output`语义。未来XeSS可在显示尺寸补帧，必须清晰标为显示补帧，记录“8K SR / 2K显示FG”等真实尺寸；不得暗中改变现有DLSS/FRUC的FG尺寸。

8K导出优先验证HEVC，但最终按GPU/codec的`NV_ENC_CAPS_WIDTH_MAX`、`HEIGHT_MAX`和初始化结果决定。H.264不可用时明确要求选择支持的codec，不能悄悄改编码格式。NR按导出实际目标尺寸执行；若8K原生NR不满足能力/显存，就拒绝该组合，不偷换成实时1080NR输出。

## 4. AMD 接入首先要解除的架构耦合

当前不是只有NR DLL需要适配：

- `D3D12DeviceContext::initialize`自动枚举只挑NVIDIA；`readAdapterInfo`也以`isNvidia`作为成功条件。AMD即使关闭增强也无法沿这条产品路径正常建立设备。
- `EnhanceGraph::initNgxFeatures`在非`noFeatures/noNgx`路径统一创建NGX core、查询DLSSG并加载/创建NR对象；只将`nr=false`或FG关闭不足以建立纯跨厂商图。
- `initNvof`、DLSS SR、RTX Video SR、DLSSG、FRUC和NVENC分别有NVIDIA依赖。AMD OF只能替换运动估计，XeSS只能增加显示补帧，不能解开其它依赖。
- 当前共享shader为`cs_6_0`；原生AMD网络使用SM6.10预览DXC、Agility与FP8矩阵操作，不能把现有所有shader编译器全局替换后直接发版。

建议增加的最小边界：

```text
FrameSource -> 统一颜色 / PTS / 内容取样
             -> SR backend
             -> NR backend + 同帧 Guidance
             -> FG backend / XeSS presenter
             -> Display / WIC / Encoder backend

NVIDIA: 既有 SR + NGX NR + NVOF + DLSS/FRUC + NVENC
AMD:    FSR1候选 + D3D12 NR候选 + AMD OF + XeSS2X候选 + AMF候选
```

这是后续接入目标，不是现有能力清单。设备按实际LUID和后端能力选择；同一会话优先所有模块在同一物理GPU执行。不能用“AMD显示、RTX后台计算”当AMD独立支持，也不默认在本机核显与5070之间搬运大纹理。

NR backend只拥有网络资源、参数与reset。颜色规范、SR→NR→FG顺序、残差强度与保护区、真实FPS、source/sink仍属于共享engine。新增`NrBackend`不要求造一套通用插件市场或复制播放器。

## 5. 三条 AMD NR 路线的取舍

### A. 原生 D3D12 网络：优先技术候选

采用Kien的MIT网络核心为参考/可归因复用对象，不加载其ReShade add-on，不拦截游戏FSR。MIT只覆盖作者代码，不覆盖NVIDIA模型权重；现有Runtime Pack对指定DLL的批准也不自动包含提取后的模型文件。先把网络从游戏宿主中剥离到可选的`AmdNativeNrBackend`或独立薄诊断worker，保留NVIDIA后端的默认行为。

首个闭环固定为真实1920x1080内部NR，上下按网络合同反射填充到1920x1152。`native_actual_network70.h`具有网络`Create/Run/RecordUnsubmitted`、共享工作区与GPU资源输入，说明不必依靠游戏注入才能组织调用。但大量层尺寸/ViT token数和权重布局写死，不能传入任意width/height就宣称完成2K/4K/8K网络。

具体步骤：

1. 独立固定该网络的源码、shader、工具链与数据布局版本。保留版权/notice；不复制`Development`的大量dump、游戏测试数据或二进制。
2. 用官方D3D12能力查询验证linear algebra及具体FP8/FP16形状；上游README的“tier10”对应`D3D12_LINEAR_ALGEBRA_TIER_1_0 = 0x10`，不是第十等级。Tier1成立也不能代替E4M3、矩阵形状、模拟标记和PSO创建检查。
3. SM6.10/DXC/Agility只进入候选target或进程，固定匹配版本。以官方Microsoft文档和实际PSO结果为准，不能仅凭显卡名称假定支持；RDNA3/核显不继承RDNA4验收。
4. 首次本地权重准备只可使用用户已有、hash固定的310.8.0.0 DLL。只读它，不改文件，不从游戏/缓存/网盘取另一版。上游README称约16GB“权重与参考dump”不是最终必需模型包大小；必须从真实加载依赖列出最小文件集、shape、hash与峰值内存，不能照单打包16GB。
5. 首先证明全71-block链执行且输出随输入变化，关闭上游skip-block/故障注入/诊断捷径。对照自身重复结果、NGX NR同图及颜色灰阶，再验证运动历史、切镜、UI保护。重复/普通锐化/颜色偏移不算网络正确。
6. 上游有网络颜色encode/decode和temporal feed；逐项核对后端输入输出语义。外层不得无条件再叠一次Veyra parity。保证最终仍回到统一linear纹理，并使用同帧current→previous motion、实际padding尺度和一致epoch。
7. 网络中间buffer/descriptor/temporal资源不能在前一帧未完成时覆盖。上游的64个command-list槽不是64帧lookahead，但也不能原样套成Veyra的排队策略；按依赖重新映射到有界提交，记录实际GPU成本与资源峰值。
8. 如果预览Agility必须隔离到worker，使用同LUID的共享GPU资源和fence、有界2个请求槽。若共享资源或同步无法可靠完成，只维持离线诊断状态，不用正常路径CPU像素回读掩盖。

第一阶段可以标为“AMD实验NR / 1080内部处理”，经原生分辨率底图回填后显示2K/4K/8K；这仍然不是原生4K/8K NR。现有NVIDIA视频导出保持原生策略。AMD完整导出须另有原生网络尺寸/分块画质和AMF验收，不能把该1080候选冒充全三入口完成。

### B. ZLUDA：离线兼容候选

优势是继续消费原NR DLL中的网络，不必先搭完整的权重转换与HLSL层。但它需要单独的NGX CUDA host、ZLUDA/LLVM/HIP，不是把`nvcuda.dll`放进现有D3D12程序即可。

已核对`dlss_cuda.cpp`确实创建D3D12共享texture、调用`cuImportExternalMemory`、通过自定义`ngx_runtime`调用CUDA Create/Evaluate。其逐帧`evaluate`在D3D12→CUDA边界执行`flush_and_wait`，源码说明所用互通路径缺外部semaphore导入。README明确性能差且可能黑图，不能直接搬入低延迟主图。

保留为独立进程的单图/少量连续帧诊断，避免自定义`nvngx.dll/nvcuda.dll/nvapi64.dll`污染现有NGX加载命名空间。上游包含关闭签名检查的进程环境设置、固定私有偏移读取和宽松加载；Veyra不能照抄这些默认行为，仍校验固定原DLL。不得替换System32文件或全局DLL搜索路径。

只有身份相同的原DLL、正确输出、可取消首次编译、有界缓存/显存与连续帧稳定性通过，才考虑作为“慢速离线实验”接入。首次编译必须可观察/可取消，不以超过单次300秒的黑箱测试循环推进。该路线当前不承诺实时30/60fps。

### C. 现成 HIP runtime：不作为默认集成

danielblnc的当前许可证不是开源许可：仅个人非商业使用，明确禁止再分发、修改、patch和重新打包。仓库无核心源码和稳定函数接口。Zmodeler包装层虽MIT，但其README仍称runtime“无许可证”，已落后于原作者2026-09-06加入的许可，不能采用这句话代替原始许可。

如果以后原作者提供独立纹理API和集成许可，可以新增`AmdHipNrBackend`；目前最直接的下游方式是游戏FSR hook、私有RVA或已patch DLL，与Veyra不注入、不改binary的边界冲突。当前用户已批准的NVIDIA Runtime Pack也不能自动覆盖这个作者的runtime与新增权重。

本轮不因此停止其它路线，不向作者发消息、不下载安装器或第三方改版。HLSL和ZLUDA代码研究均可独立继续。

## 6. AMD 的配套模块

| 模块 | 接入选择 | 不可混淆的边界 |
| --- | --- | --- |
| 基础播放/采集/图片 | Win32/D3D12/WIC/现有decode与颜色管线，按GPU能力初始化 | 先验证所有NVIDIA增强关闭时也能在AMD独立打开媒体；解码是否硬件加速另列，不因GPU呈现成功就写硬解 |
| 超分 | 第一候选是官方MIT FSR1 EASU/RCAS，后续再评估XeSS-SR | FSR1为空间算法，不等于DLSS SR、RTX Video SR或神经细节恢复；关闭锐化也应可用 |
| 光流 | 按前一份方案新增AMD FidelityFX OF | 共享运动可靠性校验；不把常量confidence当真实可信度 |
| NR | 本文A路线固定1080网络实验，B路线离线验证 | AMD backend只显示实际支持的参数，不把NGX风格/遮罩/UICorrection键盲目套入 |
| 实时补帧 | XeSS-FG + XeLL，按能力查询，AMD当前同属非Intel最多2X | XeSS是呈现后端；离线导出无公开输出texture接口，不能拿屏幕录制冒充 |
| 视频编码 | 官方AMF D3D12候选 | 官方`AMFContext2::InitDX12/CreateSurfaceFromDX12Native`和`D3D12AMF.h`的state/fence契约提供接入方法；实际codec/8K能力仍需查询、编码与解码验证 |

AMF只替换压缩编码器，继续共享颜色转换、源PTS、CFR判定、音轨mux、任务取消和已有完整性检查。不得另起raw-pipe导出核心。AMD视频导出的FG如需实现，还需要具有输出帧接口的独立后端，不能把NVIDIA FRUC或XeSS显示输出计数当作已支持。

## 7. 施工顺序与可验收结果

| 步骤 | 交付范围 | 必须通过的证据 |
| --- | --- | --- |
| S0 | SR目标模型、统一尺寸规划、旧预设迁移 | 2K/4K/8K、SR关闭、源已够大、4:3/竖图/极长图、偶数尺寸、未知enum、旧schema测试 |
| S1 | 专业UI与engine实时目标切换 | 只改目标不改NR/FG/算法；2K→4K→8K→2K，暂停/播放/总增强关闭、失败回滚、布局/滚轮检查 |
| S2 | 新尺寸实际GPU闭环与导出 | 两种SR分别有真实Create/Evaluate、尺寸与像素；8K资源峰值；图片完整输出；HEVC能力查询/少量帧导出，旧4K路径不退步 |
| A0 | 跨厂商基础设备与按需依赖初始化 | 指定AMD LUID后无NGX/NVOF/FRUC依赖也能看视频、采集回放和图片；NVIDIA路径不退步 |
| A1 | AMD OF、可选FSR1 | 已知位移/颜色/尺寸/复位正确，实际dispatch，不需NR权重即可验证 |
| X1 | 原方案XeSS-FG + XeLL | 先在5070验证代理呈现2X、输入guidance生命周期、真正中间帧、音画与reset，再到AMD复验 |
| A2 | 原生D3D12 NR独立1080闭环 | 合适RDNA4的真实能力、完整网络dispatch、非黑/非原图/非纯颜色偏移、颜色误差与完成GPU时间 |
| A3 | AMD NR进入共享engine | motion/current frame/epoch一致；原有残差保护不重复编码；60传输→30处理、NR-only、XeSS2X分别验证 |
| A4 | AMD完整产品与发布准备 | 原生尺寸/分块策略、AMF视频导出、PTS/音轨/取消、自然画质、依赖与包清单；全部通过后才扩充对外支持表 |

这些是增量实施步骤，不改变旧Phase7整体完成状态。S0-S2不依赖拿到AMD显卡；A0/A1及XeSS的部分工作也不依赖AMD NR模型。A2遇到真实硬件/权重接口问题，不应反复重测旧RTX用例来消耗时间。

每次测试独立外部上限290秒，先10-30秒窄测，确有必要再60-120秒。单次不超过5分钟，不是全部研究/施工合计5分钟。实卡仍由用户验收；软件回放必须标明非实卡。

## 8. 反向审查与停止条件

- 控件出现、capability=true、函数返回成功、画面变色四者都不能独立证明NR正确；要绑定完整网络执行、有效像素、同帧输出与GPU完成。
- 4K/8K输出与1080内部NR必须同时显示。GPU占用、源输入fps、文件输出fps、FG倍率、真实NR完成fps分别统计，禁止混用。
- 光流估算质量不能恢复游戏原生depth/exposure；不以另加深度模型掩盖AMD网络或颜色错误。
- 保持SR→NR→FG，不把第三方游戏在FSR前执行NR的顺序作为默认Veyra路线；有必要比较时单列实验。
- 候选没有可复现收益时保留实验结论或停止集成，不默认追加多pass、锐化、时域平滑来制造“增强感”。
- 出现wrong-device、越界、TDR、黑帧、上一帧残差污染、请求成功而无新网络输出，相关候选保持失败。不要改超时、禁用校验或增加隐藏队列冒充修复。
- 新NR模型或runtime进入Release前仍须逐项列来源/hash/许可并遵守用户发布授权；源码不包含SDK、模型、runtime、dump或测试媒体。本轮只写文档，不修改任何发布策略。

## 9. 固定来源

以下为本轮读取的固定提交；GitHub说明是作者陈述，代码核查和Veyra实测分开。

- [Daniel HIP原项目](https://github.com/danielblnc/DLSS-NR-on-AMD/blob/057c87324bfd8131c45c6b7e7de7d22ab46844d5/README.md)；[原许可证](https://github.com/danielblnc/DLSS-NR-on-AMD/blob/057c87324bfd8131c45c6b7e7de7d22ab46844d5/LICENSE)；[v0.2.17](https://github.com/danielblnc/DLSS-NR-on-AMD/releases/tag/v0.2.17)。
- [Kien的D3D12网络](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/blob/628620a74f3fcd3b5081620bd86308f57fc210a3/README.md)；[核心入口与固定尺寸](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/blob/628620a74f3fcd3b5081620bd86308f57fc210a3/src/native_actual_network70.h)；[MIT](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/blob/628620a74f3fcd3b5081620bd86308f57fc210a3/LICENSE)。
- [ZLUDA图像项目](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda/blob/7026d5115aa940727e31f6be37eb600c7fab1e9e/README.md)；[实际CUDA/D3D12桥](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda/blob/7026d5115aa940727e31f6be37eb600c7fab1e9e/dlss_layer/dlss_cuda.cpp)；[自定义NGX host](https://github.com/RedDukeDev/dlss5-image-enhancer-zluda/blob/7026d5115aa940727e31f6be37eb600c7fab1e9e/ngx_runtime/ngx_runtime.cpp)；[所引用ZLUDA fork](https://github.com/RedDukeDev/ZLUDA/tree/c91676be79d15b57251a7d2fae81483c48409c53)。
- [PCSX2接入与限定测试](https://github.com/zmodelerlover/dlss5-neural-amd/blob/196165ae3a739266022eb0ecb3a78553b3c0434c/README.md)。
- [AMD离线视频及120帧记录](https://github.com/eikkapine/DLSS5-AMD-Video/blob/9303dfaa14c2ca83e237ff18318bdfc0b767f6fb/README.md)。
- [Matheus最后更新](https://github.com/MatheusGViana/dlss-5-amd-project/tree/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707)；[Opti-DLL对应快照](https://github.com/Vodkaman23/DLSS-NR-UE5-Opti-DLL/tree/3eaa79be6e07331ebdaa5273e5a5df908cddb2e4)。
- [Microsoft linear-algebra能力规范](https://github.com/microsoft/DirectX-Specs/blob/b7efb10d133a9b6dc0b00b82774cf799ba48a845/d3d/D3D12LinearAlgebraRuntimeFeatureSupport.md)；[HLSL WaveMatrix](https://github.com/microsoft/DirectX-Specs/blob/b7efb10d133a9b6dc0b00b82774cf799ba48a845/d3d/HLSL_SM_6_x_WaveMatrix.md)。
- [AMD官方FSR1](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/a21ffb8f6c13233ba336352bdff293894c706575/readme.md)；[AMF D3D12 Context](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/blob/c35f613aea2e5057a688c979e75b1cf24253297e/amf/public/include/core/Context.h)；[AMF资源/fence合同](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/blob/c35f613aea2e5057a688c979e75b1cf24253297e/amf/public/include/core/D3D12AMF.h)。
- [NVIDIA RTX Video SDK公开说明](https://developer.nvidia.com/rtx-video-sdk)；[NVENC能力查询官方指南](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#querying-encoder-capabilities)。本机RTX Video SDK1.1原始指南第5.2节亦已核对，开发文件仍在忽略目录。

## 10. 本轮实际执行与交接

执行了`git status`、`rg`/源码读取、`gh api search/repositories`及上述仓库的提交/树/Release/issues查询、`Invoke-WebRequest`读取固定版本文本、`Get-CimInstance Win32_VideoController/Win32_Processor`、`Get-FileHash`和`Get-AuthenticodeSignature`。研究缓存位于gitignored `logs/amd-nr-research-20260910/`，没有运行第三方脚本、下载或加载新runtime。

本机设备为RTX5070和Ryzen7 9700X的AMD Radeon核显，另有虚拟显示适配器。核显PCI device为`1002:13C0`，它不是上游已经验证的RX9070XT；尚未在它上面查询SM6.10/FP8或运行AMD网络，不能因名称含AMD就当目标卡验收。

应用EXE未变：`E97B716B99116BEC942262FFEF1612299CBB2F4B0BDA7C308A5BFF318B3B5157`。原NR仍为指定SHA `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`、165840496字节、310.8.0.0、签名Valid。

本文最初研究阶段没有构建、2K/8K Create/Evaluate、AMD网络执行、实卡测试、阶段gate或独立Reviewer通过结论。后续同日实施已完成 Release 构建；RTX 5070 上 `veyra_experimental_backend_tests.exe sr2k` 和 `sr8k` 各完成三次 DLSS SR Evaluate，并读回确认非黑的 `2560x1440` 与 `7680x4320` 输出；AMD OF 独立短测成功 Create/Dispatch/Destroy 48 次，使用非周期纹理的源图右移2像素得到 canonical `current -> previous` flow `(-2, 0)`，左移2像素得到 `(2, 0)`。这验证的是 AMD 光流方向，不是 AMD NR 网络执行。没有 AMD GPU、AMD NR 网络、实卡、阶段 gate 或独立 Reviewer 通过结论；没有提交、push 或 Release 变更。UI闪白修复的原工作树保留；原生NR性能、FRUC reset像素等历史未决问题没有被本方案关闭。

文档检查通过：两份方案及交接页的本地链接、代码围栏和行尾空白检查；`loop/STATE.json`解析及Phase7状态检查；`git diff --check`。研究文本缓存经`git check-ignore`确认仍被忽略。这些检查只证明文档一致性，不是新后端或画质验收。

检索纠错：RTX Video网页最初使用错误URL得到404，已改用本节有效链接；FSR1的README为小写`readme.md`，按API目录修正后成功读取；若干猜测的本地文件路径不存在，随后按`rg --files`找到实际位置。一次JOURNAL patch因标题不匹配未应用，读取正确标题后才写入。它们不是GPU测试结果。

**下一条实施任务：S0，给共享ResolutionPlan与EnhancementSettings增加2K/4K/8K目标、预设兼容和尺寸测试。** 先完成可独立验证的目标规划，再接UI和8K实际GPU路径，不以AMD硬件缺口阻塞这一项。
