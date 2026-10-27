# 后端切换、真实帧率、链路延迟与音画同步继续修复方案

> 2026-09-11 最新用户收尾决定优先于本文旧范围：彻底移除FRUC；AMD NR搁置；性能/UI保持当前；实卡/音画同步由用户验收；发布暂缓。FRUC及计时诊断软件缺口已完成构建和回归，详见 [收尾实证](../reports/FRUC_REMOVAL_AND_DIAGNOSTICS_2026-09-11.md)。下方FRUC修复、AMD接入及旧下一步仅为历史计划，不是继续执行授权。

日期：2026-09-10。状态：方案已写入，待实施；本文不代表以下功能已经修复。代码基线：`be00e27`。

本轮对应用户提出的五项问题，并承接上一轮没有完成的 FRUC reset、GPU 调度和计时任务。当前执行入口为本文、最新用户指令和 [WORKLOG](../../WORKLOG.md)。旧 Loop、控制哈希和 Phase 排队不再生效；单次测试最多 300 秒，不是全部测试累计五分钟。

## 1. 为什么上一轮没有全部完成

上一轮完成的是 DLSS 采集提交前跳过、帧流账本和部分生命周期修复。FRUC 同步候选失败后撤回是必要的，但随后以部分成果收尾，没有完成原计划 S3/S4，也没有解决用户端的完整操作闭环。这属于未完成交付，不能用联合短测通过替代。

已有验证也存在覆盖缺口：

- XeSS 独立 GPU 用例调用成功，不证明专业页上的选项可见、可点击、可实时切换。四个弹出选择器的通用 UI 回归也没有证明 XeSS 这一具体入口。
- AMD FidelityFX 光流执行成功，不证明 AMD NR 已接入。目前没有 AMD NR provider，不能称为已经支持 AMD DLSS5。
- 源帧完成计数正确，不代表满足用户要看的包含有效补帧的处理产出 FPS。
- FRUC 普通回放通过，不覆盖已知失败的重置后像素检查。
- 旧交接页没有跟上实施，仍把已加入的 XeSS 和 SR 目标称为研究阶段。本轮同步当前入口，旧记录只保留历史用途。

后续每项完成必须同时满足：用户能操作、请求确实生效、真实数据能验证、异常状态可见。缺任一项都继续列为未完成。

## 2. 当前事实与定位

| 用户问题 | 源码核对结果 | 本轮结论 |
| --- | --- | --- |
| 找不到 XeSS FG | `SettingsWindow.cpp` 已创建 ID 208，含 DLSS/FRUC/XeSS；但 y=8 的选择器与 y=12、高30的标题1103重叠 | 已确认布局矩形冲突；是否完全遮住及点击命中仍需运行 UI 验证 |
| AMD / NVIDIA NR 不能分开选 | `EnhancementSettings.h` 只有 NR 开关、参数、尺寸策略；没有 `NrBackend`。`OpticalFlowBackend::AmdFidelityFx` 只控制光流 | 需要真正的 NR provider 接入及切换事务，不能只补按钮 |
| FPS 不符合实际产出 | `EngineController::snapshot()` 用 `processingRate_` 覆盖 `s.fps`，完成事件来自 `resolveGeneration`，只计真实源帧；生成/Present 已有另外的计数 | 并非直接把输入 FPS 填进处理 FPS，但未统计最终处理产出；完成观测还受呈现 worker 推进影响 |
| 实时状态区信息不足 | `WorkspaceChrome.h` 固定画 NR 圆环和源帧处理，其他指标分散在 `TelemetryWindow.cpp` | 改成集中、紧凑的实时状态区，使用同一份指标快照 |
| 处理后音画不同步 | `CaptureCardSource.cpp` 用 DirectShow `RenderStream(...Audio..., nullptr, nullptr)` 自动接音频 renderer；视频走独立 GPU/FG/Present | 采集没有可控 PCM 延迟队列与共同呈现时钟，具有音频先走、视频迟到的结构 |

XeSS 的本地官方 SDK 3.0.2 在 `xefg_swapchain.h` 将 `framesPresented` 定义为 “Number of frames sent to be presented during the last call.” 当前 `XessPresenter::afterPresent()` 累加该值。它是 SDK 报告的送呈现数量，不是本应用测得的逐帧 GPU 完成或屏幕扫描数量。

文件播放已有 `AudioPipeline`、WASAPI `AudioRenderer` 和音频主时钟，不能按采集直通问题一并推倒。需要核对首缓冲与 `IAudioClock` 锚点、暂停/seek、欠载及持续 GPU 过载时的共同推进。

## 3. 后端开关与切换合同

### 3.1 专业模式控件

增强页按独立含义排列：

1. NR 增强开关。
2. NR 实现：`NVIDIA NR` / `AMD NR（实验）`。
3. NR 处理尺寸：实时档 / 原生档，仅列当前 provider 真正支持的模式。
4. 超分开关、算法与 2K/4K/8K 目标，保持现有功能。
5. 当前 NR provider 实际支持的参数和保护区域。

补帧页按固定行排列：页标题、`补帧方式`、后端选择器、倍率、`运动估算`、光流后端及其参数、内容节奏。DLSS / FRUC / XeSS 必须在独立可见的位置切换；XeSS 当前为预览 2X，不提供假 3X/4X。

修改 `SettingsWindow.cpp` 的局部布局表，取消本页追加控件时反复推移 y 坐标的脆弱方式。区分 combo 折叠可视高度与下拉列表高度，使用同一组行高计算布局、滚动范围与命中区域。保持现有毛玻璃和自绘选择器；鼠标滚轮只滚页面，不改参数。选项不可用时保留名称并显示原因，不能直接隐藏。

`NVIDIA NR` 是当前固定身份的实验 Feature 18 路径，不把“原版”改称官方公共 SDK 集成。`AMD NR（实验）` 是另一种网络执行实现，既不是 AMD 光流，也不是在 NVIDIA 下拉项上换名称。

### 3.2 配置事务

在共享设置中增加 `NrBackend`，沿用已有 desired/applied revision、校验、预设迁移和失败回滚。NR 开关、provider、内部尺寸、光流和 FG 分开保存。旧预设缺省为当前 NVIDIA 路径；未知枚举拒绝或明确迁移，不能索引越界。

统一向 UI 提供 `requested / applied / capability / reason`，后端状态至少区分关闭、切换中、预热、运行、不可用、失败。只有成功初始化并得到对应 provider 的有效输出后，才能显示“运行”。SDK DLL 存在或配置写入成功不够。

切换在源帧边界提交，取消旧异步工作、等待已用资源的消费者 fence、推进 epoch 并统一 reset。创建失败保留上次可用会话，UI 指出请求未应用；不能用户选择 AMD/XeSS 后悄悄继续 NVIDIA/DLSS 并显示已切换。更换物理适配器需要重开设备，不能伪装成同一 device 的无缝 provider 切换。

XeSS 涉及代理交换链和 XeLL，验证 DLSS -> FRUC -> XeSS -> 关闭 -> DLSS 的资源释放与重建。呈现结果只计一次，切换关闭时不能残留旧 SDK 生成计数。XeSS 不支持导出；保留明确的预览能力边界。

### 3.3 AMD NR 本体

承接 [AMD NR 研究方案](SR_TARGETS_AMD_NR_PLAN_2026-09-10.md) 的固定来源。首选候选仍是 MIT D3D12/HLSL 网络核心，固定提交 `628620a74f3fcd3b5081620bd86308f57fc210a3`。本轮没有重新联网审计上游，不把旧研究当新增实测。

实施闭环：

1. 复核固定提交、许可证、最小依赖和本机编译条件。只复用网络核心并保留归因；不加载 ReShade add-on，不把 SDK、模型、权重或 dump 放进源码 Git。
2. 在实际 device 上查询 SM6.10、D3D12 linear algebra、所需 FP8/FP16 类型和矩阵形状，验证对应 PSO 创建。以能力和结果为准，不能仅用 GPU 品牌判定。
3. 独立实现 `AmdNativeNrBackend`，封装网络创建、记录、完成资源和 reset；优先同 device/queue。确需不同 Agility 环境才采用隔离 worker，资源与请求均有界。
4. 按现有研究先支持 1920x1080 内部网络和1920x1152 padding；完整执行网络，核对时间历史、颜色 encode/decode 和输出随输入变化。不能默认叠加两次 parity，也不能把普通锐化或原图回传当 NR。
5. 接入共享 `EnhanceGraph` 的 NR 节点，继续 SR -> NR -> FG。当前帧、motion、尺寸、epoch 和资源 fence 一致；参数仅暴露网络实证支持的部分。
6. 用户可选择 AMD 实现；实际不支持的硬件/模式给具体原因。本候选暂不能兑现原生4K/8K NR时，必须显示实际内部1080尺寸，不能静默冒充原生模式。图片和视频导出沿用现有原生策略及检查，未通过的 AMD 组合明确不可用。
7. 在合适的目标 AMD GPU 上完成网络与产品路径验证，记录 adapter LUID、完整执行、GPU时间、显存峰值、同图对照与连续帧reset。只有这些通过才称 AMD NR 功能完成。

现有本机证据是 RTX5070 与 Ryzen9700X 核显，不是上游验证的 RX9070XT。没有目标硬件执行就明确“AMD 实机未验证”。灰色选择器和 capability 拒绝只是交互/错误处理完成，不能代替第3至7步。

ZLUDA 保留离线技术候选；不把逐帧 CPU 同步或黑图路径接入默认实时图。HIP 二进制路线沿用已核对的限制，不假定外层 MIT 授予其再打包权。现有 Runtime Pack 授权也不自动涵盖新增模型/运行时；本次不扩大发布范围。

## 4. 实际帧率统计

### 4.1 指标定义

| UI 名称 | 统计事件 | 不允许的替代 |
| --- | --- | --- |
| 处理产出 FPS，主值 | 最近一秒完成的真实输出帧 + 有效生成输出帧，同窗去重；两部分都必须已 GPU-ready | 输入FPS乘倍率、CPU提交、Evaluate次数、重复/无效/预热帧 |
| 源帧处理 FPS | 真实源帧输出完成 | 采集回调或文件标称FPS |
| 有效补帧 FPS | DLSS/FRUC实际有效且GPU完成的中间输出 | 请求倍率、候选数量、仅API成功 |
| 呈现提交 FPS | 实际成功送入呈现的真实/生成帧事件，分别保留分量 | 显示器刷新率、理论目标FPS、失败Present |
| 采集输入 FPS | 采集回调实际到达事件；标称模式单列 | 固定设备参数直接当实测 |
| XeSS 提交 FPS（SDK） | 每次 Present 对应的 `framesPresented`，按present ID和窗口只累计一次 | 本应用GPU完成率或屏幕扫描率 |

“处理产出”反映算出了多少，“呈现提交”反映送出了多少。已经完成但后来过期的有效生成帧属于已做的计算，必须同时进入过期/未呈现统计；不能靠高产出数遮住低呈现。页面主值旁始终保留呈现提交值及过载状态。

XeSS 的生成帧在 SDK 代理交换链内部，目前没有本应用可独立读取的逐帧输出 fence。启用 XeSS 时主位置改为明确的 `输出提交 FPS（XeSS SDK）`，另保留源帧处理 FPS；`处理产出总计`显示不可独立测量。不能把两种口径混在一个无标签的“实时FPS”里。将来新增可信完成事件后才扩展测量。

### 4.2 数据实现

- 扩展 `FrameFlowWindow` 与 `FrameMetrics`，所有事件携带 session、applied revision、epoch、source sequence、batch、real/generated index和相关fence。完成、取消、过期、提交各计一次。
- 统一采样时钟和窗口宽度，避免把两个不同起点的FPS相加。最近1秒事件窗口，UI每250ms刷新；窗口未满时标明采样中。保留支持1000fps的容量，不限制60。
- GPU-ready观测从呈现等待中解开：不能等到某生成帧的计划显示时间才记录上一批处理完成。复用非阻塞fence检查与既有等待机制，不增加忙轮询或每帧CPU等待。
- fence轮询是主机观测完成的时间，不能伪装成GPU精确完成时刻。记录观测滞后；GPU阶段耗时继续用timestamp。状态机改造后检查突发计数是否来自轮询被阻塞。
- session/设置切换清空窗口，旧事务完成不能混入新设置；epoch reset时明确窗口边界与预热状态。高频Drop不能让UI不断把极短窗口外推成高FPS；样本不足就显示采样中并保留丢帧计数。
- pause/stop一秒内归零，图片单次处理显示单帧耗时，不显示持续播放FPS；没有样本、阶段关闭、失败不可测、有效0分别表达。
- 清理 `PlayerSnapshot::fps` 的歧义命名和内部旧平均值赋值；底栏、状态区、诊断窗与日志消费同一快照，不能各算一套。

示例仅用于验收算术：60个真实输出+40个有效生成输出，应显示处理产出100；其中仅30个生成输出送呈现，则相应呈现最多90，不能显示240。这个例子不是当前软件实测值。

## 5. 总延迟与分链路计时

### 5.1 总延迟的可测边界

主指标命名为 `软件总延迟`。采集真实帧采用该帧回调进入本进程至对应 Present 返回的时间差。同步显示测量边界；它不包含能准确还原的采集卡硬件前段，也不等于HDMI到屏幕发光延迟。

生成帧没有独立采集回调。记录父帧A/B、生成PTS、B到达至送呈现时间，并按有效的源PTS到QPC映射计算其时间线年龄；不得直接拿B的arrival冒充生成帧完整年龄。无法建立映射时显示未测，不能用固定 `1/f` 替代lookahead。

文件模式显示 `软件驻留时间`（该帧进入处理至Present返回）与 `PTS迟到`（相对音频主时钟），不把解码预取驻留全部叫交互延迟。非实时导出继续使用吞吐/进度口径。

XeSS可测的总延迟终点是代理 Present 返回；SDK内部呈现排队和实际扫描若没有公开测量则显示未知。不要把这个时刻作为已经上屏。

### 5.2 阶段与统计

| 阶段 | 测量方式 |
| --- | --- |
| 采集等待 / 解码 | 回调、read/decode开始结束的QPC；硬解GPU耗时有独立支持才报告 |
| GPU槽位 / CPU提交 | slot租约与submit边界QPC、等待次数、in-flight峰值 |
| 输入颜色、SR、光流、NR、残差合成 | 现有D3D12 GPU timestamp，绑定当前frame identity |
| FG | DLSS各子帧及整批区间；FRUC有证据的实际执行完成区间；XeSS内部耗时不可得则未知 |
| 输出转换 / blit | 相关GPU timestamp，不漏算也不重复计入颜色项 |
| GPU就绪等待 | 提交/观测与消费者等待分开，不称GPU执行耗时 |
| 补帧等待下一源帧 | A/B的实际到达与PTS关系，单列算法等待与配置的lookahead数量 |
| 呈现等待 / Present | deadline等待与API调用分别计时 |
| reset / 重建 | 按原因记录排空、销毁、创建、预热与恢复首个有效输出时间 |

总延迟直接由同一帧的首尾时刻取得，不通过相加上述行计算。CPU/GPU阶段可能重叠，多队列timestamp必须核对各自frequency与校准；各项P95相加也不等于总延迟P95。关闭、待完成、不可测不能全部显示0ms。

状态区默认显示最近一秒均值和总延迟P95，详情列样本数与各阶段P95。聚合来自同一窗口、同一applied配置；过期样本不沿用为当前实时值。逐帧诊断用固定容量环（初值8192条、溢出计数），每秒落一次汇总；错误立即记录。不增加正常视频像素回读。

## 6. 实时处理状态区布局

改造截图中现有区域，不另开必须手动寻找的诊断窗口。删除占据大块面积的固定16.7ms NR圆环，将NR作为链路耗时一行。

结构示意，`--`均为布局占位，不是测量结果：

```text
实时处理状态                   运行 / 预热 / 过载
处理产出        -- fps      软件总延迟     -- ms
呈现提交        -- fps      总延迟 P95    -- ms
源帧处理        -- fps      有效补帧       -- fps
采集输入        -- fps      丢帧 / 过期    -- / --
------------------------------------------------
采集/解码       -- ms       槽位等待       -- ms
颜色转换        -- ms       超分 SR       -- ms
光流            -- ms       NR            -- ms
残差/合成       -- ms       补帧 FG       -- ms
输出转换        -- ms       呈现等待       -- ms
Present         -- ms       音画偏差       -- ms
------------------------------------------------
NR实现 + 实际内部尺寸 / SR目标 / FG方式与倍率
音频同步状态 / 当前补偿量 / 不可用原因
```

文件输入、FG关闭、XeSS、无音轨时采用上述定义对应的行名和状态。输入FPS只作辅助，主值始终具有明确的处理或SDK输出含义。底栏也同步更改口径，不再一个位置叫处理、另一个位置拿输入值代替。

布局实现约束：

- 调整 `ChromeLayout` 和 `AppShell` 对状态区、参数区、播放条的边界预留，不能只往当前固定高度里继续画字。
- 296至420逻辑像素侧栏使用紧凑两列指标；窄窗口按整行重排。短窗口可滚动状态明细，主值和总延迟保留可见；不遮视频、不遮播放条，日常模式保持简洁。
- 数字用固定宽度区域，主值字号约18至22，阶段字号约11至12；小数变化不引起布局抖动。长后端名换行，不截掉重要不可用原因。
- 保留现有真实毛玻璃、透明文字和自绘控件；只刷新变化区域，避免全窗口擦白和原生背景闪烁。
- UI验收必须覆盖100%/150%/200%DPI、最小窗口、最大化、拖动缩放、专业展开、滚动、全屏进出和选择器弹出。

## 7. 音画同步修复

可以修复“Veyra里的音频先播放、画面后处理完成”这类错位，方法是按同一媒体时间线安排声音播放。它不能消除NR计算和FG等待成本，也不能延后PS5/电视/外部耳机绕过Veyra播放的声音。

### 7.1 采集音频

1. 把自动音频renderer直通改成受控音频接收，优先复用现有DirectShow原生sink模式，取得协商后的PCM格式、sample PTS、duration及discontinuity。回调只写有界PCM环，不进行GPU工作或阻塞等待。
2. 明确DirectShow参考时钟及audio/video sample PTS到QPC的映射。新renderer不能无意改变图时钟而让视频回调节奏漂移。设备PTS缺失或时钟不一致时给估算标记，不能报告精确同步。
3. 提取现有WASAPI sink可复用部分，以小范围PCM source接口接入文件和采集；不复制第二套播放器。格式转换/重采样只做必要一次，保留音量、静音、设备选择。
4. 从对应视频PTS的实际送呈现进度估计音频目标播放时刻，结合 `IAudioClock`、当前padding、设备延迟决定PCM何时提交。FG用生成PTS参与时间线，不把整个NR耗时、总延迟和lookahead重复加三遍。
5. 自动补偿初定0至250ms，默认自动；PCM环容量覆盖补偿和小幅抖动但有硬上限，队列满就明确报告并按采集恢复策略重锚，不能无限积压。实际队列量、设备队列量与总补偿分别可见。
6. 微小偏差用有界平滑/缓慢重采样校正时钟漂移；突变在可控边界flush、短淡入淡出并重新锚定，避免一改NR质量就爆音或频繁拉扯音高。不要靠不断增加音频队列追逐持续过载的视频。
7. 设置提供自动 / 手动 / 关闭。手动偏移初定-250至+250ms、1ms步进，正值表示声音更晚，负值只减少已有可用延迟；无法提前尚未到达的声音，显示实际钳制值。滚轮不修改偏移。
8. GPU持续过载、音频已比视频更晚或达到补偿上限时，显示无法完全对齐的原因。默认不增大视频两批上限；更大的视频补偿必须另有显式模式，不能偷偷加队列。

`音画偏差`按同一主机时刻下估算的正在播放音频PTS减去视频呈现PTS计算，正值定义为声音领先。呈现端仍有未知扫描延迟时标为软件估算。设备缓冲已包含的时间不再重复补偿。

### 7.2 文件播放

- 保留音频主时钟与源PTS；解码预取500ms不等于人为听觉延迟500ms，不直接删缓冲追求表面低延迟。
- 核对先填首批真实PCM、启动WASAPI和建立有效clock anchor的顺序。时钟未启动或设备报错时必须是无效状态，不能以默认0作为可信媒体时间。
- pause/resume、seek、EOF、换音频设备和切换后端时，音视频统一暂停、清旧缓存、重锚并恢复。取消旧epoch后不能播出旧音频。
- 暂时欠载允许有界共同rebuffer；若当前画质持续低于实时速度，则明确提示当前组合无法实时播放，保持共同暂停或等待用户调档，不能音频一直前进、视频长期追赶。
- 文件播放和导出不为追帧静默丢真实源帧，不改PTS，不把预览音频补偿写入导出的音轨时间线。导出完整性代码与既有检查保持现状。

### 7.3 同步验收

先用带已知同PTS闪光/蜂鸣的合成素材和可注入延迟的媒体时钟测试，覆盖0/20/60/120ms视频延迟、抖动、60传输30处理、后端切换、暂停/seek、音频设备丢失和补偿上限。

内部目标：在处理能够持续实时且时钟有效的场景，收敛后软件估算A/V偏差绝对值P95不超过30ms；改变配置后两秒内重新收敛，不能持续漂移或无界排队。该数值是待验收目标，不是现有测量结果。首次先用120秒短测，单项仍不超过300秒。

内部计时通过不证明外部听觉/屏幕同步通过。再由用户实卡验收；如进行loopback录音或屏幕捕获诊断，注明其自身延迟边界，不把它当高速相机光子测量。

## 8. 上一轮遗留的调度与 FRUC 修复

### 8.1 FRUC 必须以像素修复为通过条件

沿用 [上轮实施记录](../reports/SCHEDULER_REPAIR_IMPLEMENTATION_2026-09-10.md) 的失败证据：重置后首对可能返回上一真实帧，第二对可能返回上一对插值。诊断性的 `cuCtxSynchronize` 加D3D11完成通知通过6个epoch，但纯GPU completion候选没有通过，最终均已撤回。

下一步建立输入写入 -> CUDA/D3D11映射 -> FRUC执行 -> 输出可见 -> D3D12消费的逐步资源与fence证据。每一步必须指向同一个资源、generation和生产者完成条件，重点核对SDK实际操作的上下文/stream、共享资源映射生命周期及旧输出复用。

先复现 `--reset-pixels`，再一次改变一个同步点；诊断回读只用于测试。生产路径不得采用每帧 `cuCtxSynchronize`、CPU fence wait或提高超时来掩盖错误。轻量reseed必须与fresh worker对比颜色/位移/PTS；fresh worker本身也要过像素检查，不能默认它正确。

仅在reset像素和恢复时间通过后重新接FRUC admission。必须检查跳过 -> 恢复 -> 有效生成的闭环，避免重复重建导致一直预热、有效生成永远为0。失败时保留上一可用路径及明确已知限制，不能把FRUC项目标为完成。

### 8.2 完整调度和已证实的性能浪费

- 完成旧S3：单一GPU所有者推进submit、fence-ready、deadline及present；UI与回调只发命令。不能在锁内等待未来PTS，使GPU-ready统计和新源读取一起停住。
- 采集真实源帧coalesce发生在昂贵SR/NR提交前，只消费仍有价值的最新帧；有意60->30取样与mailbox丢帧分开，正确reset历史。文件/导出不使用此淘汰规则。
- 保持mailbox=1、呈现active+queued<=2、现有有界texture/command slot；已提交GPU工作不能撤销，其租约等消费者fence后再复用。
- 完成旧S1剩余的reset耗时、逐帧固定环和ready观测，供本方案指标使用。
- 旧S4按新证据逐项决定：仅合并确认浪费的小提交，核对NGX/NVOF/FRUC依赖和资源状态。没有收益就保留测量结论，不为完成任务强行合并。
- 同配置对照源吞吐、有效生成、呈现节奏、总延迟P95、slotwait与显存；GPU占用降低但产出/呈现更低不算性能修复。

## 9. 执行顺序和修改边界

| 顺序 | 任务 | 主要文件/模块 | 完成条件 |
| --- | --- | --- | --- |
| R0 | XeSS可见性、独立NR后端设置、能力/实际后端状态 | `SettingsWindow.cpp`、`EnhancementSettings.h`、`PresetStore.cpp`、`EngineController`、`PopupSelector.h` | 用户能找到/选择XeSS，正常/失败切换真实可见，预设兼容；AMD本体仍单独验收 |
| R1 | 统一FPS与链路计时、改实时状态区 | `FrameMetrics.h`、`FrameFlowWindow.h`、`FrameRateWindow.h`、`EngineController`、`EnhanceGraph`、`XessPresenter`、`WorkspaceChrome.h`、`AppShell.cpp`、`TelemetryWindow.cpp` | 数字来自事件，区分GPU完成/SDK提交/Present；所有链路与总延迟集中显示 |
| A1 | AMD NR完整网络闭环及共享图接入 | 新NR provider、按能力初始化、候选独立构建target、共享颜色/reset | 真网络与有效输出；硬件缺口明确列出，不用光流/灰按钮算完成 |
| R2 | FRUC同步、S3状态机、采集coalesce、S4测量决定 | `FrucBackend`、`FrucWorker`、协议、`PresentationWorker`、`EngineController`、`CommandSlotRing` | reset像素与恢复通过，事件/租约一致，无无界排队，性能A/B有证据 |
| R3 | 采集/文件音画同步 | `CaptureCardSource`、`NativeCaptureSink`或音频sink、`WasapiAudioSink`、`EngineController`、专业设置 | 共同PTS/clock、受控补偿、过载/设备异常明确、A/V短测达标 |
| R4 | 集成回归、当前交接与本地存档 | `tests`、`scripts/acceptance`、`docs` | 五项用户需求及R2分别交账，未验收项不被整体通过掩盖 |

首项唯一动作：复现专业页ID208与1103重叠并修补帧页布局，验证XeSS项实际可选、applied后端真正变化。之后按上表推进。A1遇到真实硬件或依赖阻塞时，记录具体缺口并继续R2/R3；最终报告仍保留AMD未完成状态，不能因其它修复通过就写全部完成。

R1的GPU-ready观测与R2状态机存在依赖：先完成可用统计合同和UI，再在R2后复验观测滞后；不能提前宣称最终处理速率已完全消除调度偏差。R3使用R1/R2稳定的时间线；AMD provider接入同一时间线，不额外建计数和音频系统。

## 10. 测试与证据要求

单次外部超时设为290秒，留进程清理余量；不并行抢同一GPU/采集卡。先窄测再集成，不重复跑与改动无关的长测试。

| 检查 | 场景与真实证据 |
| --- | --- |
| 控件和事务 | 三个FG选项逐一点击、NR实现切换、关闭/暂停时修改、旧预设、不可用原因、快速连续操作、DPI/窄窗/滚动无覆盖 |
| FPS事件合同 | 15/30/60/1000事件率，完成与CPU提交错开，重复完成、预热、过期、取消、reset、停流；60+40例子符合定义 |
| GPU后端 | DLSS/FRUC 2X/3X/4X分别验证，XeSS 2X单列SDK呈现计数；AMD NR需目标设备完整执行和像素；API成功不够 |
| 延迟 | 注入已知等待验证首尾总延迟；重叠CPU/GPU阶段不被重复相加；关闭/未知阶段状态正确，旧epoch不污染 |
| FRUC | `--reset-pixels`全部epoch、fresh worker与reseed对照、2X/3X/4X恢复后有效生成；普通live通过不能替代 |
| 调度 | realtime与原生4K高成本档各自固定配置A/B；source/setting切换、scene cut、drop、resize、device lost；界限/租约/PTS正确 |
| 音频 | 合成A/V、可控延迟、补偿边界、时钟漂移、seek/pause/重开、设备丢失、无音轨；内部与实卡结论分开 |
| 旧能力 | 60->30->60、SR2K/4K/8K已有路径、图片、文件播放、NVENC导出和取消；不扩大导出检查 |

已有可复用命令示例，实施时按实际参数和新增target更新记录，不能复制为“已运行”：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
./scripts/acceptance/scheduler-short-test.ps1 -Name continuation-contract -Exe ./out/build/x64-release/veyra_repair_contract_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name continuation-worker -Exe ./out/build/x64-release/veyra_presentation_worker_tests.exe
./scripts/acceptance/scheduler-short-test.ps1 -Name continuation-fruc-reset -Exe ./out/build/x64-release/veyra_fruc_tests.exe -TestArgs @('2','logs/video-sdk-trial-20260909/pan.nv12','--reset-pixels')
./scripts/acceptance/scheduler-short-test.ps1 -Name continuation-xess -Exe ./out/build/x64-release/veyra_experimental_backend_tests.exe -TestArgs @('xess','logs/continuation-repair-20260910/xess')
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root .
```

每次记录提交/工作树状态、EXE和worker哈希、GPU/驱动、runtime身份、输入hash、完整applied配置、命令、退出码、耗时、日志和失败。联合delivery沿用现有23项合同；增加本次针对性用例，不修改其导出完整性门槛。

最终逐项报告R0/R1/A1/R2/R3/R4状态。只有五项产品需求及所承接修复都有对应证据才可声称本计划完成。硬件未测、源码接入未完、失败候选、仅SDK提交的边界必须明确保留；不使用旧Phase或旧通过日志补位。

## 11. 本轮文档交付记录

本轮只做只读源码/本地SDK文档核对、截图查看和修复方案/交接入口更新。没有修改产品代码，没有重建EXE，没有运行新的Create/Evaluate、AMD网络、FRUC或实卡测试，也没有操作正在运行的软件设置。

此前 `be00e27` 的RTX短测和失败证据仍见 [调度实施记录](../reports/SCHEDULER_REPAIR_IMPLEMENTATION_2026-09-10.md)。它们属于历史基线，不能证明本方案新增修复已通过。开始本轮时Git工作树干净；本轮不push、发布或打包运行时。

文档检查：`git diff --check`通过；PowerShell核对7份文档的29个本地链接、代码围栏与新方案行尾空白通过。检查范围包括新方案、README、HANDOFF、WORKLOG、SR/AMD研究、XeSS研究与旧调度方案。没有将检查结果记为产品测试通过。

下一步准备实施R0。方案范围已经确定，不需要重新讨论旧Loop或以五分钟累计额度为理由提前结束。
