# Veyra 2.0.9 · 新版NR、HDR调节与播放兼容更新

2026-10-17。本版整合2.0.8以来已完成的NR、PR44、字幕/游戏操作与issues41–45修复，保留现有VFG混合档、XeSS边缘神盾、低延迟和采集/串流功能。仅NVIDIA NR更换为新版社区兼容运行库；AMD专有组件与模型保持原件。

## 下载与升级

- [NVIDIA便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9/Veyra-2.0.9-NVIDIA-win64-portable.zip)
- [AMD便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.9/Veyra-2.0.9-AMD-win64-portable.zip)

完整解压到新的可写目录，运行veyra_qml_ui.exe。无需下载源码即可运行；旧版保留便于回退。两个已交付测试ZIP原样晋升，只改外部发布文件名，包内2.0.9-test、原文件夹/清单标志保持，Windows文件版本2.0.9.0。此方式保留实际已验字节，不冒称重新编译过正式EXE。

**[Veyra软件知识库](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)**：由四个群聊整理的使用文档，包含常见软件设置、采集卡和故障排查。首次使用或遇到问题可先查阅。

## 1. SF310.9.16 NR：默认性能模型，保留旧模型开关

- RTX20–50默认选择SF310.9.16兼容构建，界面每层新增“启用性能优化版”，默认开启，使用内置模型B；关闭使用同一DLL中的旧模型A。不需要自己来回替换DLL。
- NVIDIA原版继续保留，选择它时该开关置灰；AMD不显示此开关，运行库/权重/内核不变。新NVIDIA包删除Lecram和旧SF-v2文件；旧0/1选择迁移至新版SF，明确保存的NVIDIA原版/AMD选择保留。
- 模型开关与每层参数、复制/复位、预设、列表/节点各自会话、继续上次及导出共用。活动NR切A/B需要重建，可能短暂停顿；效果关闭/层旁路时调整无效模型设置不会重建播放。现有风格、最高5的总变化强度和自动/手动画面调控保持。
- 预热、近期图缓存和自动NR尺寸池已适配新版SF；缓存区分实际DLL与模型，保留预算/退出释放。没有扩大旧特定Lecram双层+DLSS自动COMPUTE实验范围，也没有改VFG或其它补帧模型。

### 本机相同条件实测

RTX5070 12GB、驱动616.56，GTA6原4K视频，单层NR、强度1/风格0，调控/SR/FG/HDR/调色关闭，GPU优先级一致；同一2.0.8 EXE仅更换SF槽文件，旧/新/新/旧顺序，比较相同8–21秒内容。

| 内部NR尺寸 | 旧SF-v2 NR GPU均值 | 新SF310.9.16默认模型 | NR GPU耗时下降 |
| --- | --- | --- | --- |
| 1920×1080 | 5.87296ms | 3.20118ms | 45.49% |
| 原生3840×2160（补充） | 约21.29ms | 约11.79ms | 约44.6% |

1080对应增强链GPU阶段约7.1681→4.4958ms。首轮尺寸夹具误用了原生4K，复核后补测真正1080并分开列示。新旧模型画面可能不同；上述是特定内容的GPU耗时，不是全型号同画质FPS、屏幕端到端延迟或万能画质提升。作者兼容构建面向20–50，本轮本机实际验证5070，20/30/40仍需对应实卡反馈。

## 2. HDR显示调节：PR44整合与性能适配

感谢贡献者georgeg911的PR44，保留原提交和来源。显示页现在分为读数、快捷选档、基础调节、精确参数、记忆与自动化五张卡片；整档预设或内容×环境自由组合，简化的暗部/明暗/高光保护与精确参数联动。切换选档界面本身不改画面，换预设不覆盖强度和显示器峰值。

- 可记忆影片/显示设备，按片源容器元数据和系统显示信息给出推荐；首次自动选档默认关，已保存影片选择优先。推荐不是逐帧识别剧情，也不替代屏幕校准。
- 支持原生HDR10、scRGB及SDR经RTX Video HDR转换后的HDR输出；曲线图和元数据使用相同实际解析参数，图输出/导出表面不被预览显示曲线误改。
- 读数卡展开且曲线生效时最多每250ms统计一次，只异步读回20字节计数；隐藏停止，无正常播放像素回读、额外提交或逐帧CPU等待。保留33ms/250ms界面节奏，避免修界面又拖慢播放。未宣称零开销或无测量依据的性能百分比。
- 补齐黑帧零读数、过期统计清理、自定义预设/未知亮度状态、非法参数拒绝和“已应用”真实结果；修复打开片源时提前记忆、停止/切源漏保存、清除后立即重建。最多60影片/32显示器，400ms防抖保存，兼容旧环境ID。

## 3. 长片字幕、游戏画面对齐与自定义穿透快捷键

- 采纳用户提供补丁发现的问题：共享PGS位图缓存旧64MiB预算会截断长片/多轨后段，改为默认4GiB按需预算，修复64位乘法与严格范围检查，保留坏数据/尺寸/数量/取消防护。**是RAM预算上限，不预分配4GiB，也不预占显存。** 拒绝日志限频，避免坏字幕刷屏。
- 五轨200条/75MiB实际压缩PGS样本复现旧预算后段截断，新预算五轨后段均可定位并显示；PGS/SUP/VobSub/DVB/文本和异步加载继续回归。不是所有用户影片已逐一验证。
- 屏幕捕获游戏模式按原窗口真实可见边界或显示器区域对齐，修正鼠标位置；处理Qt缩放和物理取整，普通电影/专业播放条布局保持。窗口化游戏按原位置/尺寸显示，余下留黑；需要铺满显示器请用无边框全屏。窗口跟随沿用250ms几何缓存，没有新增视频帧缓存。
- 设置→快捷键可自定义“游戏模式：调整画面”和“游戏模式：退出鼠标穿透”，默认Home/Ctrl+Alt+Home；支持冲突检查、保存/重启和还原，提示同步显示实际键位。独立键盘消息线程、左右修饰键/长按配对、退出卸载/旧事件失效，避免QML重复响应。
- 全屏切换完成后再接管输入，修复重入时焦点错误。独立进程Raw Input目标和真实发键/鼠标落点已测；独占/反作弊/提权游戏、跨屏混合DPI仍需实机，不保证封锁所有游戏Raw Input按键。

## 4. 播放与兼容修复

- **#42 网络播放**：首页/来源菜单“播放地址”，Ctrl+U，支持HTTP/HTTPS/RTSP直接媒体地址和单地址拖入，共享原FFmpeg/音频/增强链。网络操作5秒截止、可取消；直播暂停后重连继续，避免旧连接积压。不支持任意网页/网页登录内容；是否能拖动由媒体与服务器Range能力决定。标题只显示主机，URL账号/token不进入最近文件/播放列表/配置；不伪造直播离线导出。
- **#41 自动去隔行**：设置→播放的保存开关，默认开、下次打开文件生效；仅处理解码器标为隔行的文件/网络视频，逐行旁路。软解bwdif、硬解GPU处理，50i→50p、59.94i→59.94p，保留真实场PTS，修正可见尺寸、输出fence/参考生命周期、EOF尾场和seek重置。模块不可用时明确置灰。采集/离线导出未扩展，不猜错误标记的片源。
- **集成去隔行构建**：修复复用CMake缓存时旧FFmpeg头文件优先、选项开启却被静默编成stub的问题；明确核验前缀，缺头文件构建失败。保留原patched FFmpeg五DLL/dav1d，新增独立LGPL avfilter模块并附对应源码/补丁/许可证。
- **#45 VRR**：分开G-SYNC Compatible“当前模式可请求”和“驱动实际活动”。不因possible/requested/activity为false就认定用户没开；未收到真活动状态时说明等待确认，不伪报已启用，不改驱动控制面板。真实VRR收益仍须对应显示器验证。
- **#43 英文专业界面**：右侧参数栏底部说明完整换行，底栏按内容增高、上方列表可滚动，重置按钮保持可见；补充窄/宽窗口及OBS软件界面验证。
- **RTX2060等待样本相关修复**：NVOF初始化资源注册补齐输入/输出fence及部分失败回滚同步；不占逐帧fence，不新增逐帧CPU等待。NR初始化/执行失败明确显示失败原因，不无限“等待样本”或沿用旧耗时。2060用户现场恢复仍需反馈，不能只凭5070通过断言唯一根因已解决。
- 统一新增入口英/日/繁中提示，保持既有默认GPU优先级、真实时间戳、队列上限和其它补帧链路。

## 验证、组件身份与源码

- 52构建目标通过，模型设置/迁移/预设/会话/导出快照等17个回归进程通过；Qt Quick77项通过、2424翻译缺译/占位符错误0。
- 真实GTA4K/1080NR覆盖A/B/原版/两层、列表/节点、风格2强度5自动调控、XeSS2X/DLSS2X；重启保存恢复通过。预热、50次近期缓存开关和90帧自动尺寸池与重建参考逐帧SHA一致。
- A/B两个独立导出worker各完成原生4K/30fps/6帧，独立解码复核；1080i软/硬解尾场与seek通过。两个最终ZIP解压后D3D12/OBS软件界面共四场4K基础播放通过，AMD包在NVIDIA上启动不等于AMD增强实卡验收。
- 新SF原件310.9.16.0，276162560bytes，**NotSigned/community experimental**，SHA256 `CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED`。用户文件与作者compat-fp16-310.9.16-cuda13.4.3原RAR解出文件完整匹配；NVIDIA原版身份保留。运行库审计清单不锁定用户自行替换DLL。
- NV长期显存增长未因本次换模型宣称根治；20/30/40、AMD/Intel实卡、真实VRR、跨屏/实体投影和光学延迟按各自证据验证。
- [完整对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.9-sources)、[构建和运行组件说明](BUILD_2.0.9.md)、[全部提交/产品路径索引](RELEASE_2.0.9_CHANGE_INDEX.md)、[NR实测记录](../reports/NR_SF_310916_REPORT_2026-10-17.md)、[PR44验收](../reports/PR44_HDR_DISPLAY_REPORT_2026-10-16.md)。

| 发布文件 | SHA256 |
| --- | --- |
| NVIDIA ZIP | `6618461d722428aff65e8eee1847cd673aaa02d6c00beab87b7e661b97e0f843` |
| AMD ZIP | `e9224fb7f546253e351d2262168a8cedd3c80319f68379392699ecb439c6cc83` |

## English

**Veyra2.0.9 focuses on SF310.9.16 NR and combines completed HDR, subtitle, game-input and playback compatibility fixes since2.0.8.** The two delivered ZIPs are promoted unchanged under release filenames; internal2.0.9-test labels/manifests remain, PE2.0.9.0.

- **NR:** RTX20–50 defaults to the community-compatible SF runtime. Use performance model defaults on(B); off selects embedded legacyA. NVIDIA original remains available with this option disabled; AMD components/models are unchanged. Lecram/oldSF-v2 are removed, legacy choices migrate, and per-layer presets/List/Node sessions/export preserve models. Active changes rebuild NR; inactive edits do not interrupt playback. Existing strength5/picture controls remain.
- **Measured example:** RTX5070/616.56, GTA4K input, internal1080NR, sameEXE/settings/content and old/new/new/old runs: oldSF-v2 5.87296→newB3.20118ms, −45.49% NR GPU time; enhancement GPU chain7.1681→4.4958ms. Native4K supplemental NR21.29→11.79ms. Models can differ visually; these are not universal equal-quality FPS or optical-latency claims. RTX20/30/40 need actual hardware feedback.
- **HDR(PR44, georgeg911):** five cards, quick and content/environment presets, linked simple/precise controls, per-title/display memory and metadata recommendations, nativeHDR10/scRGB/RTXVideoHDR compatibility. Sparse20-byte counters at most4Hz only while visible; no playback pixel readbacks or extra submissions. Fix stale/black/unknown/custom status, premature or missing saves and false success; retain existing UI cadence.
- **Subtitles:** replace the shared64MiB compressed-bitmap budget that truncated late multi-trackPGS with a4GiB on-demand RAM ceiling and64-bit validation. This does not allocate4GiB upfront or consume that amount of VRAM. A five-track75MiB real decoder fixture verified the regression; PGS/SUP/VobSub/DVB/text/cancellation remain tested.
- **Game input:** align enhanced capture to actual window/monitor bounds with DPI rounding; windowed games retain original placement, borderless fullscreen fills the display. Configure adjustment and exit-passthrough hotkeys in Settings(defaultHome/Ctrl+Alt+Home), with conflict/persistence/reset support, an independent hook pump, paired/repeat handling and safe re-entry focus timing. Exclusive/anti-cheat/elevated games and mixed-DPI setups still require testing.
- **Playback fixes:** Ctrl+U/directHTTP/HTTPS/RTSP input with bounded cancellable network operations, genuine timestamps and privacy-aware history; saved automatic deinterlacing(softwarebwdif/GPU, final fields/seek preserved); G-SYNC Compatible eligibility separated from real driver activity; wrapped English Professional footer. Fix cached-FFmpeg-header precedence silently disabling deinterlacing, deploy its independentLGPLmodule and corresponding sources while retaining original patchedFFmpeg binaries.
- **NR initialization:** synchronize NVOF initialization registration/partial failure cleanup relevant to RTX2060 waiting-for-samples reports, without adding per-frame waits; show failures instead of indefinite waiting. The affected2060 still needs field feedback.
- **Validation:**52 targets,17 regression processes,77 QtQuick tests and complete2424-entry translations; realA/B/original/multi-layer/List/Node/XeSS/DLSS playback, separate4K model exports, persistence/cache/pool/interlace checks and four extracted-ZIP startups. SF DLL is NotSigned/community experimental; it matches the author's archive. Other hardware, long-run NVIDIA VRAM growth, physicalVRR and optical latency are not declared fixed or universally validated.

Use the download/source links above and the knowledge base. Existing community and sponsorship links are retained below.

## 支持与反馈 / Support & feedback

<p align="center">
  <a href="https://discord.gg/c9aREyMj8"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join Veyra on Discord" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://ko-fi.com/likely7"><img src="https://storage.ko-fi.com/cdn/kofi5.png?v=6" alt="Support Veyra on Ko-fi" height="36"></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.0.7/docs/images/2.0.7/community-group.png" alt="Veyra 交流群 6" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-19 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-19.

[Veyra 软件知识库 / Knowledge base](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)：四个群聊汇总的使用文档，涵盖软件与采集卡常见问题。
