# 英伟达低延迟：执行与验收

用户授权：完善已有Reflex接入，名称改为“英伟达低延迟”，三档“关 / 开 / 开＋增强”。本地分支`codex/nvidia-low-latency-20261028`，main基线4351bbd，存档`checkpoint/pre-nvidia-low-latency-20261028`。不合并main、不push/Release；私有配置和本地文件测试，不改驱动/SDK/运行库或真实配置。

实施合同：保留通用低排队能力；NVIDIA控件三档分别不调用、原生低延迟、原生低延迟+Boost。配置保留显式三档，旧lowQueue=true迁移为开，旧关闭维持关闭。不能用实际回退状态覆盖用户保存的选择。非NVIDIA及提供方独占呈现不伪装为Reflex已启用。

源帧Sleep在选择输入前，每个实际处理源帧最多一次；等待/过滤输入复用准备ID。模拟/提交标记围绕输入和增强提交，真实Present正常标记；生成帧使用out-of-band标记，不重复Sleep/模拟标记。直接DXGI下验证DLSS/VFG与增强档；XeSS保留自己的低延迟调度，不套第二个节拍器。返回值失败时停用原生调用并显示回退，关闭、图重建、源切换、退出清除低延迟/Boost。

直接使用NVAPI，不引入Streamline。ABI沿用NVIDIA/nvapi固定87dca625e83fd89a983e19b904e5f3a580da90d2的MIT公共接口；参考NVIDIA公开Streamline D3D12 `setLatencyMarker` 的IUnknown队列目标用法，仅作为契约参考，不接入其运行库。[Reflex入口](https://developer.nvidia.com/performance-rendering-tools/reflex)、[NVAPI](https://github.com/NVIDIA/nvapi/tree/87dca625e83fd89a983e19b904e5f3a580da90d2)、[D3D12参考](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/platforms/sl.chi/d3d12.cpp)。

验证：产品/相关目标构建；三档状态/持久化/迁移/无效值/关闭与失败回退；真实GPU文件在无补帧、DLSS及VFG下开关/增强、暂停恢复/seek/切源；原生API与生成帧标记成功、正常速度与完整退出。以软件排队/驱动接口证据报告，不宣称已测量点击到光子或采集卡固有延迟。每测试≤300秒、构建并发2/≤900秒，产物统一E盘对应nvidia-low-latency-20261028任务目录，复用既有构建与一份运行目录。

## 实际实现

- PresentationSettings新增独立三档字段，沿用通用低队列布尔字段。NVIDIA选择开/增强时启用低队列，关时两者停用；其他显卡保留旧通用低队列控件。已有lowQueue=true仅在NVIDIA上迁移为开，不自动开启Boost；非法保存值回退关。
- 全局JSON presentation.nvidiaLowLatency持久化，不混入画质预设。列表/节点共用NvidiaLowLatencyEditor。状态取实际有效设置；驱动失败及XeSS呈现所有权有明确说明，用户选择仍保存。
- NvAPI_D3D_SetSleepMode设置low=1、boost=0/1、minimumIntervalUs=0，不叠加原生限帧。System32 nvapi64.dll保持进程生命周期固定，设备关闭前撤销模式，保留旧驱动回调安全规则。
- 输入前Sleep并分配单调源ID，等待/丢弃输入复用准备ID；暂停丢弃未消费准备ID，避免把暂停时间算入下一帧准备。模拟结束/渲染开始在增强提交前，正常渲染结束与真实Present分别标记。
- 独立呈现队列NotifyOutOfBandCommandQueue=RENDER_PRESENT；复制工作用带外9/10标记，生成Present用11/12，真实Present仍4/5。使用公开SetLatencyMarker的队列IUnknown目标，每个源帧只Sleep一次，生成帧无额外Sleep/模拟编号。支持直接DXGI的DLSS、VFG和FSR；XeSS保留提供方所有权与XeLL，不强叠第二个调度器。
- 驱动调用失败立即停止原生路径，保留低队列并更新实际状态；撤销失败不能伪装已关闭。失效的旧模式/图的在途ID不再向新模式报告。

## 实测与失败记录

本轮跨至洛杉矶2026-10-29凌晨。真实GPU为本机RTX5070；输入为私有1080p30文件。没有占用采集卡，也没有进行RTX30/40持卡、真实VRR/光学延迟或长时稳定性认证。

- 产品及相关目标构建退出0，主要批次73.8/72.1秒、增量9.1/14.8/15.7秒和测试目标4.1秒，MSVC/Ninja并发2；命令为现有tmp/claude-sr-stage-20261028/build/build.py指定目标。准确命令与日志路径见WORKLOG和任务回执。
- RepairContract 293项0失败，含关时零调用、真实三档参数、6X只Sleep一次、带外标记、无效值、错误回退、撤销失败重试与旧ID隔离。故障路径为注入API单元验证，不冒充真实驱动故障实测。
- GPU：关、开、开＋增强的无补帧路径；开+DLSS2X；增强+DLSS6X/VFG2X/FSR2X；增强+NR/SR/DLSS2X；XeSS提供方回退；暂停保留Boost/关闭/seek/恢复/三档切换/resize/源替换/退出。最终结果按FINAL.json列出的实际回执判定。
- 一个DLSS2X回执：162次成功Sleep、160张真实Present、159张生成Present、956个带外标记，撤销状态0。说明Sleep跟随源帧而非所有显示提交，不代表屏幕实际显示了319张不同图像或实测了点击到光子。
- 三档实际GUI、旧true迁移、列表/节点共享、关闭保留VFG、Boost重启恢复通过；无QML错误，原配置SHA未改。截图tests/nvidia-low-latency-20261028/controls.png为真实QML控件；原生视频子窗口不在该QML抓图内。
- QtQuick117通过/0失败/1既有跳过；i18n2704条、缺失0、占位符0。状态变化额外进入UI快照比较，不改既有刷新频率。

必须保留的失败与处理：

1. 首版使用NvAPI_D3D12_SetAsyncFrameMarker，队列通知及模式启用成功，但第一次带外标记返回-1；清零内部Present字段后仍失败。两轮日志在native-dlss-1-2、native-dlss-zero-1-2和对应gpu控制台；产品当场回退，没有假报成功。最终移除该调用与多余ABI，改为NVIDIA公开D3D12实现使用的SetLatencyMarker(queue)；同样的9–12标记实际返回0，之后矩阵通过。没有忽略失败码或改SDK/驱动。
2. 首次GUI脚本向addEffect传入无效短名fg，列表三档与VFG已通过，节点夹具失败。改成现有正式键vfg-fg，用新的profile-ui-retry复验通过，原失败JSON/日志保留。
3. NR+SR+DLSS冷启动的首帧等待超过夹具原15秒界限，之后组合以正常速度真实出帧、原生模式/NR/SR/FG均通过，整体仍按失败保存。仅此新增组合的测试等待改为30秒，不改产品超时或旧guard，另目录复验；该时间包含模型初始化，不能冒充Reflex提高/降低延迟的证据。

## 交付与保护

分支本地提交、main保持4351bbd，不push/Release。复用tests/claude-sr-stage-20261028/app，以新文件替换EXE/QML，不写穿供体硬链接；97个运行组件与正式供体及其1730文件manifest复核。独立交互入口tests/nvidia-low-latency-20261028/启动英伟达低延迟测试.cmd，不使用原用户配置。

reports/logs/tests/tmp对应任务目录保留必要回执、原失败证据和脚本；收尾仅清本轮唯一数字TEMP目录中的重建缓存，实际逻辑删除量记录FINAL，不将逻辑字节冒充物理净回收。原8K NR/VFG整机卡死组合未复测，Lecram继续暂停。

## Claude复查修正（2026-10-29）

用户请Claude复查本分支后选择“修完再测效果”。复查在本机实测确认1个bug和2个界面问题，另从代码看出1个隐患；四处修正已提交到本分支，未合并main。

1. **非英伟达运算显卡上低延迟队列关不掉（bug，实测复现）**：保存过开/开＋增强后把运算显卡切到AMD核显，旧“低延迟队列”开关关掉后引擎仍`requested=false effective=true`。现由`PresentationSettings.h`的`syncNvidiaLowLatencyQueue`统一归属：英伟达显卡上三档决定低排队（关不会再留下隐藏低排队），其他显卡由普通开关决定，英伟达选择只保留到下次回到英伟达显卡；`VideoPresenter::configurePresentation`只在英伟达适配器上把三档折算为低排队，其他适配器把它当关。启动、切换运算显卡和改档位都按同一规则归一；其他显卡上英伟达控件显示“关”且变灰。
2. **英文/日文/繁中界面状态里中文片段不翻译（实测）**：原因是状态由带“ · ”尾巴或含“；”的字面量拼接，运行时按分隔符拆开后查不到。现在状态行由不含分隔符的片段统一以“ · ”拼接；新增/改写17条翻译，“英伟达低延迟：关”、XeSS、回退提示以及同一行原有的显示同步/屏幕队列片段都能完整翻译。
3. **专业页“已开启”摘要仍写“低延迟队列”（实测）**：英伟达显卡上改为“英伟达低延迟 · 开/开＋增强”，驱动回退或XeSS接管时显示“英伟达低延迟 · 当前未生效”。
4. **未补帧时也要求带外队列通知成功（代码隐患）**：通知失败时，未补帧改为退回设备内标记、照常Sleep；补帧仍必须有带外队列，否则不启用。

另：`ReflexSession`在关闭日志里记录驱动Sleep实际耗时（`sleepMeanMs/sleepMaxMs`），用于判断“开”档到底让源帧等了多久；`PresentationPacingTests`新增`latency-screen`/`latency-screen-sr`屏幕采集延迟对比场景（档位-1关、0仅低排队、1开、2开＋增强）。

### 修正后验证

- 构建：五目标78.6秒，增量16.4/4.4秒，退出0；新增代码无新警告。EXE/测速程序/1个QML/翻译以新文件写入`tests/claude-sr-stage-20261028/app`，97个运行组件与2.1.1供体一致。
- RepairContract 300项0失败（新增归属规则4项、通知失败3项）；i18n 0失败（新增两条英文状态）；QtQuick 117/0/1既有跳过。
- 真实GUI：AMD核显上关低排队后`requested=false effective=false`，英伟达控件显示“关”；切回英伟达重开后开＋增强恢复并由驱动启用。英文界面关/开/开＋增强在自动与垂直同步下状态均无中文残留，摘要显示“NVIDIA Low Latency · On + Boost”。原配置SHA不变，Main.qml复原，无QML错误。
- GPU回归9组全部通过：plain开/增强、DLSS 2X/6X、VFG 2X、FSR 2X、XeSS回退、NR＋SR＋DLSS、生命周期。

### 效果实测（本机RTX5070，屏幕采集代替采集卡）

- **驱动几乎不让程序等**：所有用例里`NvAPI_D3D_Sleep`平均约0.002毫秒、最长0.13毫秒（文件播放、各补帧后端、NR＋SR＋补帧、60fps窗口采集都一样）。Veyra自己的调度已经不让帧排队，Reflex没有可削减的排队。
- **GPU处理时间不变**：窗口采集各档GPU处理p50都在0.68–0.74毫秒；本机显卡空闲也停在P0满频，增强档无从加速。
- **无补帧窗口采集有与本功能无关的旧问题**：呈现时间按第一帧锚定，第一帧读得多晚，整场每帧就固定多等多久；8轮里这段固定等待p50为3.1–52毫秒，而读到GPU完成始终不到1毫秒；打开低排队时，等待超过一帧间隔还会把下一帧的读取再推迟约15毫秒。采集卡（无补帧时按就绪呈现）和串流（逐帧重新锚定）不受影响。因此无补帧的绝对延迟不能拿来比较档位，只比较读帧与GPU阶段：四档无差别。
- **补帧窗口采集（每帧重新锚定，可直接比较）**：60fps窗口采集、显示同步自动，档位-1/0/1/2各两轮（ABCD DCBA，每轮10秒）。DLSS 2X：采集到呈现p50为关16.88–17.03、仅低排队16.89–16.96、开16.87–17.01、开＋增强16.94–16.98毫秒；SR 4K＋DLSS 2X：关18.03–18.05、仅低排队17.93–18.00、开18.01–18.04、开＋增强18.06–18.15毫秒。档位差别都不到0.2毫秒，在两轮波动之内；其中约16.5毫秒是补帧必须等下一帧的固定前瞻，GPU处理（含SR）约1–2.4毫秒，各档相同。
- **垂直同步下同样无差别**：DLSS 2X窗口采集、显示同步改垂直同步，p50为关16.91–17.05、仅低排队16.87、开16.89–16.91、开＋增强16.91–16.99毫秒。
- **结论（仅本机）**：在文件播放、补帧、SR＋补帧、窗口采集（自动/垂直同步）下，开与开＋增强相对关或原低延迟队列都没有可测的延迟收益；驱动Sleep几乎为零说明Veyra调度本身已不排队。增强档在会降频的显卡上可能缩短1–4毫秒级的GPU处理但增加功耗，本机无法验证。未测采集卡、串流主机、30/40系和屏幕光电延迟。
