# Veyra 2.0.8：低延迟链路、游戏模式与远程手柄

本次更新围绕实时游戏体验：**减少采集与补帧链路的额外等待，让增强画面也能直接操作游戏，加入可变刷新和采集卡画面搭配远程手柄的使用方式。** 同时合入 Claude 与 Codex 自 2.0.7 发布后的界面、采集、呈现、串流控制与稳定性修复。

## 下载与升级

- [NVIDIA 完整便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.8/Veyra-2.0.8-NVIDIA-win64-portable.zip)
- [AMD 完整便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.8/Veyra-2.0.8-AMD-win64-portable.zip)

按 Veyra 实际运算的显卡选一个包，完整解压到新目录，运行 `veyra_qml_ui.exe`。不要只替换 EXE，也不要把旧版整套运行库覆盖进来。

**直接晋升已通过用户测试的两个包，仅改变外部下载文件名，没有重新构建或重新打包。** ZIP、EXE、QML、着色器、运行组件逐字节保持；包内 `2.0.8-test` 显示名、文件夹名和原测试清单标记保留，Windows 文件版本为 2.0.8.0。

## 1. 首要更新：延迟链路优化

从取输入、提交 GPU 工作到呈现整理等待顺序，减少额外排队和重复等待：

- **XeSS 提交后立即送显**：输入、处理提交、呈现保持在同一个 XeLL 周期中，减少 CPU 等待 GPU 完成后才提交呈现的空档及轮询抖动。
- **限制显示侧积压**：优化低延迟队列和显式同步的容量管理，把需要的等待尽量前移到读取实时输入之前，避免拿到画面后再长时间等待；不同后端按其自身呈现合同处理。
- **倍率与屏幕刷新率提示**：输出目标超出屏幕能力时，状态栏和倍率下方给出建议，帮助选择合适倍率。这个提示保留用户所选倍率，不自行调整倍率。
- **完善延迟诊断**：记录显示积压、呈现完成、输入回调和 GPU 分段耗时，并修正统计滞后时对较早呈现的时间归属，减少误读。
- **全屏操作更顺畅**：可以隐藏控制条、用 Home 临时呼出调节面板，减少游戏时的界面干扰。

补帧仍需要真正到达的相邻原帧，NR/光流/模型计算也仍有成本。收益取决于源帧率、倍率、GPU、屏幕刷新率及同步方式，不能承诺所有组合相同。

### 已有实测数据

下表为本机 RTX 5070 的相同场景对照或对应阶段验证。实时日志中的 P95 以稳定窗口统计；它们分别描述软件阶段，**不是按键到屏幕的光学端到端延迟**。

| 项目与条件 | 对照结果 | 如何理解 |
|---|---|---|
| T911，2560×1440 / 60，NV12，NR + XeSS 2X，320Hz | 回调→呈现调用返回 P95 约 **10.0 → 3.0ms** | 提交阶段减少约 **70%**；现在呈现提交可以早于 GPU 完成，画面真正可显示仍受约 8ms GPU 工作等因素约束，不能说总延迟降低70%。 |
| 屏幕捕获，30fps，XeSS 2X，无NR，320Hz | 回调→呈现调用返回 P95 **3.8 → 3.0ms**；测试窗口周期错位 **922/960 → 0/960** | 轻负载下提交阶段约减少 **21%**，XeLL 周期对齐。 |
| T911，2K60 NV12 + NR，DLSS 6X，标准→新性能模式 | 补帧 GPU P95 **8.1 → 3.3ms**，过期生成帧 **7.7 → 1.0次/秒** | 补帧 GPU 工作约减少 **59%**；这是 RTX50 可选性能模式，有模型取舍，不能外推到其它型号或同画质全链路加速。 |
| 同设备与输入，DLSS 4X，标准→性能模式 | 补帧 GPU P95 **5.6 → 2.8ms** | 该场景约减少 **50%**。 |
| 六效果器、无媒体，实际节点/专业界面 NR 参数修改 | 原脚本约 **65–80ms → 约5ms/次** | 参数响应耗时约减少 **92%–94%**；这是界面编辑，不是视频处理提速。移动节点的结构性更新仍约61–64ms。 |

未用理论推算代替 60Hz/PBP 光学复测。原始条件与最终包回归见[发布验收记录](RELEASE_2.0.8_ACCEPTANCE_2026-10-15.md)。

## 2. 窗口/显示器捕获：游戏模式

在**屏幕捕获 → 游戏 → 游戏模式**启用，选择游戏窗口或显示器后开始捕获。Veyra 在对应屏幕全屏显示增强画面，键鼠操作透传给下面的程序，手柄由实际游戏或远程软件接收。

- **Home**：呼出画面调节面板；列表模式显示调节器，节点模式显示节点画布。
- 再按 **Home / Esc**，或点击面板旁的画面：关闭面板，继续操作游戏。
- **Ctrl+Alt+Home**：退出全屏和透传，返回 Veyra；捕获继续。
- 游戏窗口关闭或捕获失败时，自动退出透传并提示原因；无法注册安全退出热键时拒绝进入透传。
- 捕获指针不再叠一份延迟光标；游戏模式恢复全画面裁剪，减少点击位置偏移。
- 系统允许时去掉窗口/显示器捕获黄框；修复后出现的播放条被采集进画面及部分窗口操作互锁的问题。
- 增加界面失去响应时的保护退出处理。

建议使用无边框全屏游戏。未铺满屏幕的游戏窗口会提示点击位置可能不一致；本版没有加入任意缩放后的鼠标坐标映射，独占全屏及反作弊游戏仍按实际环境验证。

## 3. 可变刷新（VRR）

**显示**页新增“可变刷新”开关，并显示生效中、未生效和原因。配合驱动已开启的 G-SYNC / G-SYNC Compatible 环境，按显示器能力选择呈现方式，减少刷新等待带来的影响。

- 不支持或无法确认能力时，入口置灰或显示具体状态。
- Veyra 保存自己的开关，不修改驱动全局设置；需在显卡驱动与显示器中先启用相应功能。
- 当前能力查询由 NVIDIA 驱动提供；连接 NVIDIA 显卡的兼容 FreeSync 屏可按 G-SYNC Compatible 使用。不能据此宣称 AMD 显卡的原生 VRR 路径已完成实卡验证。
- 本机屏幕驱动报告不支持 VRR，真实能力禁用、状态提示及模拟呈现规则已检查，真实可变刷新收益仍需支持设备验证。

## 4. 采集卡画面 + 远程手柄，以及远程电脑游戏

### PS5：画面和控制分开

采集卡页面新增 **PS5 手柄控制**：游戏画面通过 HDMI 采集卡进入 Veyra，手柄通过远程游玩控制 PS5。

1. 在 PS5 串流页完成主机配对。
2. 在采集卡设置中打开“用远程游玩控制 PS5”，选择主机并连接。
3. 若还要把这台电脑的画面串流给掌机/另一台电脑，搭配第三方远程软件，并按需要开启“后台也转发手柄”和采集卡“音频监听”。

支持“谁按键就用谁”、当前手柄名称、登录 PIN、失败重连和状态提示；采集格式重连不必同时断开手柄控制。修复切换来源时旧定时器关闭新手柄、断开后重新连上等问题。用户已反馈真机使用正常；不同主机/手柄/网络组合仍各自验证。

远程游玩连接仍有协议保活媒体流；Veyra 不把它用作播放画面，也不启动第二套媒体解码。陀螺仪与触摸板能否转发，取决于第三方软件呈现的虚拟手柄能力。

### PC：增强画面也能远程玩

可将 Veyra 的游戏模式增强画面通过 **Sunshine / Apollo 等第三方远程软件与兼容客户端**传到掌机或另一台电脑，由第三方软件回传键鼠/手柄。PC 游戏接受回传输入，Veyra 负责捕获、增强和本机呈现；PS5 采集卡场景则进一步把虚拟手柄转发给主机。远程服务端与客户端需要自行配置，不随便携包安装。

## 5. 其它新增与全部修复

### 性能与增强

- **DLSS 帧生成性能模式**：列表/节点页均可开关，默认关闭；NVIDIA 包附带已核验的 310.9.16.0 运行库，仅 RTX50 使用；RTX30/40 保留原兼容路径，导出仍为标准模式。
- **双卡运算选择**：设置中可让“GPU 占用监控”所选显卡同时负责增强/补帧/导出；能力检测随实际运算卡变化，切换时迁移不可用后端，参数保留。列表模式切回原卡可恢复之前记住的补帧选择。
- **原尺寸 RTX Video SR 优化**：视频文件、串流和压缩采集保留压缩伪影修复；无压缩采集/屏幕捕获在输入已经达到目标分辨率时跳过该额外处理。1080p→4K 等真实放大保持。
- **设置与统计对齐**：切换设置后等待对应指标窗口，清除旧补帧计数，减少瞬间误报和回滚/半帧状态错配。

### 采集与兼容

- 修复驱动报告约1.5万个格式时打开采集页卡死：格式列表缓存、过滤/去重及一次读取，保留实际可用格式。
- 帧率菜单默认展示常用帧率并保留59.94等对应值，提供“显示全部可用帧率”查看驱动完整列表，避免长列表影响操作。
- **GC550 等采集卡内嵌音频兼容**：调整图时钟选择；只有音频没有视频时，重连尝试另一时钟方式，针对音频监听后循环断线/重连的报告。
- **BMD 输入选择**：按设备枚举可用 HDMI/SDI 等实际连接，设置并读回，再按输入过滤可用模式；保存明确端口选择，避免仅依赖驱动默认口。相应失败/恢复路径补齐测试，现场黑屏仍需受影响设备确认。
- **抖音直播伴侣兼容**：识别伴侣进程，启动时采用软件界面；伴侣运行时将用户请求的实时 GPU 优先级临时限制为高，退出后恢复请求值。补充并行软件/模块诊断；实际伴侣并行负载仍待现场复测。
- GPU 设备重置后恢复界面图标；采集失败时增加一次自动重连和更完整的 DRED 诊断。恢复能力不等于已查清所有 GPU 卡死原因。

### 界面与播放

- NR、RTX Video HDR、调色、曲线和保护参数使用局部更新，减少拖滑块时整页重建；设置仍保存。
- 组件列表按需缓存/刷新，减少播放过程中重复扫描运行库清单和重建大量界面行。
- 修复开关点击后失去外部设置绑定：预设、跨页修改或设置被拒绝时，显示跟随真实值。
- 窗口捕获连续改尺寸时保留旧帧池，尺寸稳定约300ms再重建，减少连续冻结；拖动时可能裁切/留边，最后切换仍有一次初始化耗时。
- 修复视频方向元数据、竖屏比例、非方形像素与呈现运动坐标的配合，补充0/90/180/270度验证。
- 修复节点页视频区域越界遮住下方操作栏，普通/软件界面及不同缩放比例统一视频区边界。
- 全屏 Home 可从极简呼出专业调节，从节点模式呼出画布；退出后恢复原页面，避免多余页面动画。
- **检查更新正式接入**：每次启动后台查询 GitHub，新版本顶部短暂提示后自动消失；无更新静默，设置页保留手动检查。超时/网络失败不阻塞播放。
- 日志轮换：启动已有日志超过16MB时保留一份旧日志，单次运行超过64MB时轮换，避免无限累积。
- 补充新增入口的繁中、英文、日文提示及相关布局/导出页/回滚/字幕/生命周期回归。

VFG 混合档、内容节奏重做、XeSS 边缘神盾、播放列表与 PGS/SUP 等 2.0.7 已发布功能继续保留；本次没有更换 VFG 模型或重新宣传此前的混合档收益。[此前更新](RELEASE_NOTES_2.0.7.md)。

## 验证、组件和对应源码

- 最终包已通过用户本机试用；本机35目标可构建，两厂商 ZIP 解压后的 D3D12/软件界面共四场 GTA4K 播放通过。AMD 包在 NVIDIA 机器上启动不代表 AMD NR/FG 实卡验证。
- VFG 合成异步校验曾有一次22项异常；独立完整复测2425项和实际4X播放通过，原因未定。HDR桌面5项严格RGB阈值失败在原版本基线同样存在。保留原记录，不将复测通过解释成已定位根因。
- GC550/BMD、真实直播伴侣并行负载、AMD/Intel增强、真实VRR及跨设备远程控制仍按对应硬件验证。软件分段计时不替代屏幕端到端测试。
- 新 DLSS 性能模式原件：310.9.16.0，7506544字节，SHA256 `CADEE1AA6DB3907BABDC3285DA2AEBC4553D92B89C38BA3EAE8ABD8BBA1BEA72`，NVIDIA有效签名；仍为实验集成，不代表厂商认证。运行清单供发行审计，不锁定用户兼容替换。
- [对应源码归档](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.8-sources)、[构建与源码说明](BUILD_2.0.8.md)、[完整改动覆盖索引](RELEASE_2.0.8_CHANGE_INDEX.md)、[验收记录](RELEASE_2.0.8_ACCEPTANCE_2026-10-15.md)。

| 发布文件 | SHA256 |
|---|---|
| NVIDIA ZIP | `e89972d234fc79167c08b2183b42347c9ace606663b9ac363e2d09a547afe0ba` |
| AMD ZIP | `1740d7dbc6c8307d0dd8deefb9aaafac4398ed0e40022b1e40e66a86092ecda9` |

## English

**Veyra 2.0.8 focuses on lower application latency, interactive enhanced gaming, variable refresh, and capture-card video with remote controller forwarding.** It combines Claude's and Codex's completed work since 2.0.7.

- **Latency:** align XeSS submission and presentation within one XeLL cycle, remove unnecessary CPU waits, constrain display backlog and improve diagnostic attribution. Refresh-budget hints keep the selected multiplier and suggest alternatives. In the T911/1440p60/NR/XeSS2X test, callback-to-Present-return P95 changed about10→3ms; this is early submission, not a70% reduction in optical latency.
- **Game mode:** enable it in Screen Capture for a window or monitor. Veyra shows the enhanced image fullscreen while input reaches the underlying application. Home opens adjustments, Home/Esc closes the panel, Ctrl+Alt+Home exits passthrough. Borderless games are recommended; arbitrary mouse-coordinate remapping is not implemented. Capture borders and accidentally captured controls are handled where Windows permits.
- **Variable refresh:** a saved Display switch with active/inactive explanations. Current capability detection uses NVIDIA's driver, including G-SYNC Compatible displays; genuine VRR benefit and native AMD behaviour need suitable hardware validation. Driver settings are user-managed.
- **Capture + controller:** PS5 control can run alongside HDMI capture without decoding a second playback stream. Pair the console, enable capture-card PS5 control and optionally allow background forwarding. Protocol keepalive media still exists. For remote PC gaming or relaying this enhanced picture to another device, configure third-party streaming software with controller/input forwarding yourself.
- **DLSS performance mode:** optional, default off, RTX50 only, audited310.9.16 runtime. T911/2K60/NR6X FG GPU P95 changed8.1→3.3ms;4X5.6→2.8ms. Quality can differ; these are stage measurements, not universal equal-quality gains. Export remains standard.
- **Other fixes:** compute-GPU selection, native-size VSR skipping for uncompressed sources, huge capture-format-list hangs, common/all-rate menus, GC550 embedded-audio clock compatibility, explicit remembered BMD inputs, Douyin process compatibility, one-shot capture recovery and GPU-reset icons, granular slider updates, cached components, controlled switches, debounced capture resizing, portrait/display geometry, fullscreen panels, asynchronous startup update banners, bounded logs and setting/metric alignment.

The two approved ZIPs are promoted unchanged under release filenames; their internal2.0.8-test labels and original manifests remain. Four final-ZIP playback checks passed. An intermittent VFG synthetic check and baseline HDR RGB threshold failures remain documented; no universal hardware or optical-latency claim is made. See the acceptance record and corresponding-source links above.
