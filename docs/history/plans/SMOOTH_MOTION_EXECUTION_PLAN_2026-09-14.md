# Smooth Motion 软件内切换执行方案

最新状态：用户报告本机 Smooth Motion 有效且稳定，随后明确简化普通版方案为“只提供开启说明，不管理驱动、不限制叠加”。当前实现见 [普通版说明记录](../reports/SMOOTH_MOTION_HELP_2026-09-14.md)。下文为已被新决定取代的历史设计，不再执行其中的强制互斥、DRS 写配置或专用切换入口。

## 1. 目标与技术判断

专业模式“补帧方式”增加“Smooth Motion · 驱动实验”。用户可从Veyra发起配置，算法由已安装的NVIDIA驱动执行，不要求用户手工搬DLL。首次目标是稳定2X预览，不承诺3X/4X、导出补帧或低于现有FG的延迟。

推荐路线：**管理Veyra专属驱动配置 + 复用当前D3D12呈现路径**。没有找到公开的Smooth Motion纹理输入/生成帧输出SDK；NVAPI的DRS接口管理配置，不等于提供算法调用接口。仅设置成功不能算补帧成功。

官方说明Smooth Motion在相邻已渲染帧间生成一帧，通过NVIDIA App对兼容DX11/DX12/Vulkan程序开启，支持RTX40/50系列。Veyra当前是Windows x64/D3D12、CreateSwapChainForHwnd、默认FLIP_DISCARD，具有试验基础，但不是已验证兼容性。[NVIDIA功能说明](https://www.nvidia.com/en-us/geforce/news/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/)、[启用说明](https://nvidia.custhelp.com/app/answers/detail/a_id/5621)。

已有DLSS/XeSS同样属于AI帧生成。新增方案的意义是比较驱动负责补帧的效果和调度，不是给现有非AI算法升级为AI。

## 2. 链路与互斥

```text
视频 / 采集卡 / PS5
  → 解码与颜色转换
  → NR、SR（沿用普通/低延迟模式的顺序）
  → Veyra按真实源帧时间提交D3D12画面
  → NVIDIA驱动Smooth Motion推算中间帧并安排显示
  → 显示器
```

- NR、SR保留；现有NR所需光流保留，不因关闭内部FG把NR的guidance一起关掉。
- Smooth Motion与内部DLSS FG、XeSS FG互斥。内部图不创建FG feature、XeSS presenter、生成帧队列；退出实验模式时恢复用户之前的内部FG选择。
- 严格区分“源帧/内部处理目标”和“驱动期望显示倍率”。例如30fps输入时Veyra仍按30个原帧提交，不给实时调度器塞60fps目标、假60fps PTS或重复帧，避免错误过载/跳帧。
- 原生60fps输入可探索120fps显示；采集60Hz承载30fps游戏时可复用现有60转30设置，否则重复帧会影响插帧机会。是否改善由实测决定。
- 驱动处理在Veyra提交之后，Veyra无法通过现有生成纹理接口取得这些帧。截图/导出仍走当前软件路径，不宣称包含Smooth Motion结果。

## 3. 开发顺序与节点

### A：先验证驱动真的接管Veyra

1. 从已完成采集修复的提交建立codex/smooth-motion-experiment分支和checkpoint，保留当前便携版对照。
2. 记录GPU、驱动、Windows/HAGS/VRR/HDR状态、屏幕刷新率、Veyra EXE真实路径及原程序配置。HAGS/VRR不当作未经验证的硬要求，也不自动改系统开关。
3. 先用NVIDIA App或只针对Veyra的DRS配置做一次关闭/开启对照，**不复制、不替换、不加载私自指定的NvPresent64.dll**。
4. 第一组采用本地固定30fps、无NR/SR/FG、SDR、120Hz或更高刷新率屏幕；先验证真实插帧，再加NR/SR，最后验证采集和PS5。
5. 初次探测包含默认窗口、视频全屏；遇到失败先查驱动配置、实际呈现频率、交换链、窗口/DWM路径。只有证据表明确实需要，才试现有直播兼容交换链；不直接把全局显示架构换掉。

完成门槛：程序稳定运行，并有驱动实际插帧证据。DLL出现、DRS读取ON或帧率乘二均不能作为证据。若驱动不接管Veyra，记录失败路径，本阶段不增加一个假装可用的用户选项。

### B：封装程序配置管理

建议新增`include/veyra/runtime/SmoothMotionProfile.h`、`src/runtime/SmoothMotionProfile.cpp`及单元测试。NVAPI开发文件仍放忽略的本机依赖目录，系统NVAPI由受限绝对路径加载；不把SDK头文件或驱动DLL提交Git/打进Release。

公开DRS调用路线：NvAPI_Initialize → DRS_CreateSession → DRS_LoadSettings → FindApplicationByName/查找或建立独立profile → GetSetting/SetSetting → SaveSettings → 新session重新Load/Get验证 → DestroySession。回滚使用保存原值或DeleteProfileSetting恢复原先未设置的状态。[NVIDIA公开NVAPI头文件](https://github.com/NVIDIA/nvapi/blob/main/nvapi.h)。

社区设置定义取自[Profile Inspector CustomSettingNames.xml](https://github.com/Orbmu2k/nvidiaProfileInspector/blob/2f50c388b3a4d661cade66b32746bec096d1eee1/nvidiaProfileInspector/CustomSettingNames.xml)，本轮核对的master提交为2f50c388b3a4d661cade66b32746bec096d1eee1：

| 字段 | ID | 用途 |
| --- | --- | --- |
| Smooth Motion Enable | 0xB0D384C0 | 程序级开关0/1 |
| Enabled APIs | 0xB0CC0875 | DX12位；不存在通常允许全部，不盲目新写全API值 |
| Debug Bars | 0xB01B8B02 | 仅开发验证生成帧标识，结束恢复 |
| Debug Log Level | 0xB053C379 | 仅隔离诊断，结束恢复 |

这些设置ID来自社区资料，不当作官方稳定SDK契约。实施时再次验证当前驱动支持及枚举值；不照搬仓库中所有翻转节奏、帧率限制和延迟选项。

配置管理必须实现：

- 只作用于Veyra，不触碰全局profile或OBS、剪辑软件、游戏。核对DRS按EXE名/路径匹配的实际行为；不能假定API接受完整路径就一定按完整路径隔离。若现有profile关联其他应用，明确报告冲突，不静默更改共享profile。
- 首次写入前原子保存原状态：profile/app标识、设置是否存在、继承来源、原值、计划写值和事务阶段。存用户数据目录，不是源码或便携默认配置。
- SaveSettings成功后重新读取确认；失败回滚，不修改“已应用”状态。
- 恢复时区分“原来没有设置”和“原来明确关闭”。仅在当前值仍等于我们最后写入值时自动恢复，避免覆盖用户后来在NVIDIA App中的修改。
- 多实例互斥，处理EXE改路径/升级、驱动更新、应用异常退出后的未完成事务。不能靠每次启动无条件重写来解决冲突。
- 先以普通权限尝试；若实际返回权限错误，再提供明确的受限配置操作/用户自行设置路径，不让播放器整体长期管理员运行。

完成门槛：新建/已有profile、未设置/关闭/开启、外部修改、保存失败、权限失败、崩溃恢复、双实例均有测试；mock只证明配置事务，不能证明GPU补帧。

### C：软件内选项与重启状态

涉及`include/veyra/engine/EnhancementSettings.h`、`apps/veyra/SettingsWindow.cpp`及设置序列化、`EngineController.cpp`、`VideoPresenter.cpp`、`PresentSink.cpp`，以实际符号搜索定位，不把逻辑继续堆进AppShell。

“补帧方式”并列：DLSS帧生成 / XeSS帧生成 / Smooth Motion · 驱动实验。默认维持原设置。选择Smooth Motion后显示2X目标，隐藏3X/4X，关闭内部FG资源。

切换按需重启**整个Veyra进程**作为第一版契约：驱动可能在创建设备/进程启动时决定是否接管，仅重建graph或swapchain不能未经验证就宣称即时生效。不要自动打断正在观看的采集或PS5；按钮明确提示“已配置，下次启动生效”，允许用户主动“保存并重启”。恢复播放入口不保存/泄露PS5密钥，不自动冒充用户重新配对。

状态至少区分：

1. 不支持 / 读取失败。
2. 请求开启，写入中。
3. 配置已写入，待重启。
4. 驱动配置已开启，实际生效未确认。
5. 当前会话有可信证据确认生效（只有实现可靠检测后才显示）。
6. 配置失败 / 检测到外部修改。

退出Smooth Motion并改回内部FG也要按重启规则处理：同一会话不能因UI选项切回就立刻叠加两种FG。GUI保存的请求设置与当前进程实际运行设置分别显示。

悬浮说明建议：“让显卡驱动在最后补一张帧，NR和超分照常干活。切换可能要重启；和其他补帧一起开，就容易变成大家抢方向盘。”

### D：统计、音频、截图与直播

主面板“处理产出”继续展示Veyra真实处理/提交的数据。驱动生成帧没有可靠计数接口时，显示“驱动显示帧率：暂不可测”，不能填理论2倍；已有光流/NR/SR GPU计时继续有效，内部FG耗时显示“驱动处理，暂不可测”。“总增强处理耗时”标明不包含驱动Smooth Motion，不填0冒充免费处理。

现有声音补偿只跟随软件可测时间，不重复叠加已有软件延迟。驱动额外延迟初期无法可靠取得，不自动套用1/源FPS补偿，不追逐驱动显示估计逐帧暂停音频。先测实际音画差；若用户能稳定复现偏差，再复用已有手动偏移功能校准。自动同步不包含未知驱动延迟的边界必须写明。

截图仍保存Veyra已经处理的原帧；导出仍使用现有可控补帧后端。Smooth Motion预览选项不能偷偷改变导出算法或输出帧数。

OBS/WGC、游戏捕获和显示器看到的帧可能处于不同捕获阶段，不能预先承诺录制包含驱动中间帧。分别检查WGC录制的实际独立帧、屏幕输出以及音画同步；若只对本地观看有效，明确标注，不以屏幕看起来流畅推断直播也有2X。

## 4. 验证矩阵与停止条件

所有自动/手工短测单次≤300秒，用户剪辑时不执行GPU测试。固定同一输入、增强参数、显示刷新率和观察区间，比较关闭 / 现有DLSS2X / XeSS2X / Smooth Motion，不同时叠加。

| 项目 | 需要的证据 |
| --- | --- |
| 基础插帧 | 30→60、60→120场景，驱动debug bars/可靠计数与外部显示观察互相核对；标识消失不自动判定未生效 |
| 跟随NR/SR | NR/SR计时、实际处理FPS、GPU负载、生成帧伪影、额外显示延迟分别记录 |
| 稳定性 | 日常/专业、全屏、拖拽/resize、暂停/seek、切源、断流/重连后无旧帧回放或死锁 |
| 过载 | 30fps源只能处理15–25fps时音频不断、队列有界、不虚报60fps、不叠加内部补帧 |
| SDR/HDR | SDR先通过；原生HDR和HDR转SDR分开测试，没测的组合不开放或明确不可用 |
| 可恢复 | 开启→重启→关闭→重启，驱动配置复原，正常DLSS/XeSS重新生效 |
| 软件捕获 | OBS WGC/录制能否获得生成帧独立记录；BitBlt旧限制不归因于Smooth Motion |
| 硬件 | 首测本机RTX5070；RTX40由用户/反馈者另验，不用5070通过冒充4060通过 |

不以游戏内FPS、Present调用次数、DLL加载、GPU利用率升高单独证明插帧。对外声称延迟改善需明确测量口径；没有屏幕端测量时只报告内部耗时和显示观察，不能用内部计时当按键到屏幕延迟。

若出现长期旧帧、闪烁、UI/视频交换链冲突、不可回滚配置，停止开放用户选项，保留现有补帧。不得为了“接上”引入外部注入框架、修改驱动DLL或全局重设显示配置。其他项目在不同架构中的兼容故障只作为风险线索，不能直接当成本项目结论。

## 5. 交付与当前下一步

节点A实机可行性、节点B配置事务、节点C产品切换、节点D统计/捕获/验收分别创建Git存档并更新WORKLOG。只提交自身源码/测试/文档；任何NVAPI SDK、驱动DLL、测试媒体、运行配置不进入源码Git。若参考/复用第三方实现，先核对许可证并追加THIRD_PARTY_NOTICES；本方案目前只引用公开接口与设置定义。

当前唯一下一步：**等用户不剪辑时，以已安装驱动给Veyra做一次可回滚的程序级Smooth Motion可行性测试**。确认真的补帧后再做完整软件内开关。尚未授权或执行本轮新发布；此文档不是功能已完成证明。
