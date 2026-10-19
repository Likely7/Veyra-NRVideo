# Veyra 2.1.0 · AMD NR 双模型、抗锯齿与人脸美颜

2026-10-19。整合2.0.9发布后Claude与Codex已合入主线的功能及修复。重点是 **AMD NR 0.44、小模型切换、独立抗锯齿、美颜与人脸原画保护，以及采集卡分辨率/格式列表修复**；同时包含网络缓冲、圆刚多声道、窗口捕获与全屏操作改进。

## 下载与升级

- [NVIDIA 便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0/Veyra-2.1.0-NVIDIA-win64-portable.zip)
- [AMD 便携包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.1.0/Veyra-2.1.0-AMD-win64-portable.zip)

按软件实际使用的显卡选一个，完整解压到新目录，运行 `veyra_qml_ui.exe`。本次直接发布用户验收过的两个ZIP，只改外部文件名，**不重新构建、不重新压缩**；包内目录、清单及界面中的 `2.1.0-test` 保留，Windows文件版本为2.1.0.0。两个正式下载与验收包逐字节一致。新二维码更新在仓库README和本页，原ZIP内文档保持验收时字节。

**[Veyra 软件知识库](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)**：汇总四个群聊的使用文档，涵盖软件设置、采集卡和常见问题。遇到问题可以先查阅。

## 1. AMD NR：接入 lmxxf 0.44，支持 Small / 原模型

- 将AMD NR从0.39升级为0.44热修源码适配版，继续面向 **RX 9000 / RDNA 4（gfx1200、gfx1201）**，需要相应HIP 7驱动环境。本次不是将NR开放到全部AMD显卡，也没有新增RX 6000/7000支持。
- AMD每层NR现在也使用 **“启用性能优化版”**，默认开启，选Small 310.9.16；关闭使用原模型。升级后默认画面可能与旧版不同，需要旧效果可关闭该开关。模型变更有画质差异，不能保证等画质提速。
- 列表、节点、各层参数、复制、预设、继续上次、会话与导出配置均保存模型选择。不同层可以分别选Small或原模型，两个独立模块避免上游全局模型开关让各层串改。关闭NR或旁路层时修改模型仅保存设置，不打断播放。
- 同时检查运行模块、实际模型选择和Small资源；资源缺失/旧模块/冲突配置明确报错，不用静默换模型来冒充成功。保留现有内部1080p预算、同队列同步、重置与错误处理，HDR NR边界不变。
- NVIDIA继续沿用2.0.9的SF310.9.16与NVIDIA原版；本版没有替换NVIDIA模型或加入Lecram插件。

来源为 [lmxxf/dlss5-on-amd-9070xt-porting](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting)，固定提交 `4764e4f1a24f5234b8e29952dc41f493b2d77c90`。Veyra自行构建的Small/Original模块、原样权重/HIP资源及许可证均有清单；没有依赖Magpie或ReShade宿主。**本机为RTX 5070，完成集成与合同验证，没有测得AMD实卡性能百分比。** [适配与验证详情](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/AMD_NR_044_REPORT_2026-10-18.md)。

## 2. 独立抗锯齿：FXAA、SMAA、MLAA

新增可单独开关的抗锯齿效果器，列表“画质”页和节点模式都能使用，可配合VSR等超分减轻斜线、细边的锯齿。

- **FXAA**：低成本快速平滑，提供质量档位。
- **SMAA**：按边缘形态混合，提供低/中/高/超高质量；额外提供默认关闭的实验时域模式。
- **MLAA**：可调边缘阈值与强度，按素材控制平滑程度。
- 列表在放大前处理；节点最多两个实例，可在允许位置放到超分前后。参数、顺序、会话、预设及列表离线导出共用处理图。
- HDR下用感知亮度检测边缘、在线性域混合；切源、拖动、尺寸变化等重置时域历史。时域SMAA基于现成视频帧，没有游戏引擎抖动采样，**不能当成游戏原生TAA/SMAA T2x或4x的等价实现**；运动素材可能拖影，默认关闭。

移植自SAOG0721/Magpie固定提交 `d687997`，保留GPL来源和SMAA许可说明。[实施与数据](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/EFFECTS_INFRA_AA_53_PLAN_2026-10-17.md)。本机RTX 5070的4K标准测试中，FXAA约0.15ms、SMAA约0.30ms、MLAA约0.30ms；密集边缘及实验时域模式更贵。这是指定测试图的GPU阶段耗时，不是所有素材的固定开销或整体FPS提升承诺。

## 3. 美颜：468点人脸轮廓、原画保护与磨皮

美颜是独立效果器；列表在NR组及其保护区域之后、RTX Video HDR之前，节点模式可自行编排允许的顺序，最多6层，新层默认关闭。提供自然磨皮、轻度磨皮、人脸原画保护、保护+自然磨皮预设。

### 两个独立子开关

- **人脸原画保护**：自动识别人脸，把当前帧任何效果之前的原画混回人脸区域。保护的是调色、超分、NR及之前美颜处理之前的画面，不是仅取消某一层NR。可调保护强度、整脸/皮肤范围、区域扩展和羽化。源视频低分辨率时，恢复的是放大前的原画信息，不能保留增强细节又称完全恢复原画。
- **磨皮**：只处理本节点接收到的画面，可选人脸或全部肤色，调节整体强度、磨皮程度、半径、细节保护、纹理保留和提亮；高级肤色参数可进一步限定范围。识别到的眼睛、眉毛和嘴唇从磨皮区域排除。

### 识别与跟踪

YuNet人脸检测搭配MediaPipe **468点人脸网格**，按脸型生成遮罩，减少旧椭圆遮罩把头发、帽檐或背景一起恢复的问题；支持1–8张脸、检测质量档位、置信度及遮罩预览。所有美颜层共用本帧识别，不为每层重复运行检测。

跟踪改为每个新帧检测及One Euro平滑，减少旧15Hz检测、固定平滑和速度预测造成的拖后/回弹。漏检保留短暂宽限后淡出；侧脸、低头、小脸网格不可信时回退椭圆，新出现的人脸头几帧也可能使用椭圆。不是所有画面都能正确识别人脸。

本机RTX 5070、最终网格版一张脸测试：**1080p约0.93ms，4K约1.42ms**（美颜节点中位数加识别/跟踪/网格均值）；会增加GPU工作量。参考对照62张脸，轮廓网络坐标最大差0.00014裁剪像素；特定测试图轮廓内20,071像素全强度保护逐位等于原画，五官内部608像素强磨皮时保持不变。以上是工程样本结果，不代表所有真人/游戏角色的主观效果或所有GPU性能。

支持实时预览及列表视频导出，保存每层参数；导出自动关闭遮罩调试显示。大图分块路径暂时跳过美颜并提示；没有实现单节点前后对照，暂停调参仍可能重跑前面的NR。[完整实施、性能及限制](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/BEAUTY_IMPLEMENTATION_PLAN_2026-10-17.md)。

## 4. 采集卡分辨率 / 格式选不到：重做发现与精确连接

整合PR57并补齐Windows实测适配，重点修复“OBS里有，Veyra里选不到”以及菜单遗漏像素格式。

- 驱动能力接口未实现、为空或无可解析项时，使用视频pin的原生媒体类型枚举；明确的权限/访问错误保留错误原因。优先真正的capture pin，不盲选preview pin。
- 读取驱动尺寸范围、步长和帧率范围，保留离散项，并提供 **自定义分辨率检查**。区间推导项显示“范围候选（连接时验证）”；这些不是已经确认能连接的全部组合。
- 修复166666/166667等100ns时间取整导致同组格式丢失；同组列出各像素格式，连接仍使用其真实帧间隔。**59.94与60、逐行与隔行继续区分**，不伪造未知帧率。
- 三联菜单以分辨率、帧率、像素格式约束选择；新增 **“旧版格式选择”完整可搜索列表**，可查看被常用帧率过滤的项。两种入口共用选择和保存；MJPEG继续标红并注明“不建议使用”。
- 连接时核对SetFormat/GetFormat及实际连接后的尺寸、帧率、像素格式和扫描方式。驱动偷换模式或请求不支持时明确报错，**不静默路由到另一格式**。
- 修复刷新排队、设备切换旧回执覆盖、选择保存和恢复。翻转、色彩、SDR、帧率、音频、入口等沿用上次；缺失设备/音频/格式明确失败并保留原选择，避免错误结果覆盖设置。

本机MCS 4K-T911发现1,136条驱动模板/范围候选，打开“显示全部可用帧率”后菜单可达；NV12/YUY2 1080p60真实连接、重连、保存恢复均通过。**未取得反馈者BMD实卡，不宣称本次已经根治所有BMD黑屏或所有驱动组合。** 枚举只发生在打开/刷新/配置时，没有提高UI绘制频率或新增逐帧枚举。[Windows验收](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/PR57_INTEGRATION_REPORT_2026-10-17.md)。

## 5. 网络视频与服务器播放（#48）

- 网络与服务器文件使用共享解复用会话，音视频共用有界包缓存，避免各自读流互相抢连接；本地普通文件保留原直读路径。
- 设置→播放新增可保存的网络缓冲、起播预缓冲、断线自动重连和损坏画面处理选项；支持自动/关闭/小/中/大缓冲，预缓冲可跟随档位或自行选秒数，下次打开网络片源生效。
- 带宽持续低于视频码率时暂停缓冲并说明原因；信号面板显示缓冲秒数、下载速度、重连及丢包。正常缓冲显示“缓冲中”，不误报“输出未达标”。缓冲会按档位增加等待或内存占用，并非免费消除网络延迟。
- 区分整个打开40秒期限与15秒读期限；断线重连适配直播时间线重启，清理解码历史，不把重连恢复后的帧当旧时间线。
- HLS主列表选择最高码率变体；修复损坏网络音频、隔行IPTV损坏帧直接停止、预览跳帧循环误处理Waiting，以及音轨切换与读线程同步。
- 服务器文件字幕扫描先等播放，再限速扫描，减少大文件起播被字幕读盘拖慢。不会提升服务器本身带宽。

19/19本机网络矩阵通过，另有隔行损坏补测；公开Twitch回放HLS实际播放69秒、一个会话、0卡顿/0重连，剪辑MP4播放30秒。真实用户NAS/SMB及原问题地址仍需各自验证。[网络改动与完整测试条件](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/NETWORK_BUFFER_48_PLAN_2026-10-17.md)。

## 6. 圆刚采集音频（PR63 / PR64）

- 增加圆刚厂商音频图，针对符合身份的AVerMedia / Live Gamer设备、非“仅PCM”模式使用；其他采集卡保留原DirectShow音频路径。需要用户已安装的对应官方组件，包里不伪造或替代驱动。
- 识别IEC61937、解码AC-3/E-AC-3并按真实声道布局配置，可处理PCM/杜比、采样率和声道数切换，不将压缩载波当成PCM噪声。
- PR64补齐non-PCM开关查询、必要时开启和回读验证；厂商监测与启用顺序串行化，缺接口或查询失败不盲写。保留回调看门狗、有限重建及回退，单块拒收不再直接拆掉采集会话，连续错误仍触发恢复。
- 已分包的完整编码帧即时送解码，AC-3/E-AC-3第一包当次输出1,536采样；48kHz下移除了解码阶段等待下一包的32ms，**不等于整体采集或屏幕延迟实测降低32ms**。聚合包保留所有尾帧，缓存和无进展恢复有上限。

厂商ABI夹具、真实编解码及普通采集卡回归通过；本机没有圆刚实卡，作者报告的GC553G2结果与本机验证分开记录。MAT/TrueHD/DTS-HD及Atmos不在此入口承诺范围。[PR63](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/PR63_AVERMEDIA_AUDIO_REPORT_2026-10-18.md) / [PR64](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/PR64_AVERMEDIA_REPORT_2026-10-19.md)。

## 7. 其他操作、兼容与底层修复

- **窗口游戏模式**：会话、全屏和快捷键监听就绪即可接管输入，不再额外等待首页动画或增强首帧；退出先恢复普通输入，窗口句柄变化后快捷键重新绑定。保持3秒异常恢复保护。
- **窗口画面对齐**：贴合原窗口时精确鼠标穿透；填充/拉伸/原生像素/固定比例真正应用。缩放后坐标不再一一对应时阻止错误鼠标穿透，键盘/手柄仍交给源窗口；没有伪装实现任意缩放鼠标映射。
- **捕获尺寸恢复**：源窗口resize正确更新宽高比及源信息；静止窗口重建后无新帧时，一次性重建WGC会话与帧池，修复黑边/旧尺寸/等待新帧。保留原300ms防抖与250ms几何心跳。
- **全屏操作**：专业模式点击全屏按钮不再误触暂停/继续；双击退出保留原点击归属；极简全屏指针按实际画面区域隐藏，浮层可正常操作；游戏面板快速双击不再误退全屏。
- **效果器基础**：处理链容量16→32，满额明确拒绝；抗锯齿/美颜支持统一插入位置与计时、参数保存、预设与会话兼容。修复默认关闭节点恢复时整链顺序失效、NR旁路时只重绑调色、分块图片继承整图执行计划导致被拒，以及导出worker协议不匹配导致任务拒绝。
- **首页与赞助页**：首页标题改为Veyra NR Video，咖啡入口连接“赞助作者”页，保留微信/支付宝、Ko-fi及作者主页；B站主页已纠正为用户指定账号。补齐新增提示的繁中/英文/日文与网络40秒超时翻译。
- **发行资料**：替换新的6群二维码（图片注明10月26日前有效），保留微信赞助、Discord、Ko-fi和知识库。UI原33ms/250ms节奏、默认GPU优先级、现行补帧模型不因本次发行文档更新而变化。

## 验证、源码与组件

整合构建43目标通过；58组整合回归覆盖效果链/保存/四语言、抗锯齿与美颜GPU及导出、AMD接口/图合同、采集/音频和真实UI。一个既有NR分块测试因中文源码路径找不到ngx-local.json未通过，普通尺寸/分块用例通过；不能把它抹成全部通过。最终修正B站链接的两ZIP又分别完成GPU/软件UI四次解压冷启动和GTA 4K基础播放，每场399–404帧。用户随后确认本次两包实测可发布。

AMD HIP实际推理、AMD/Intel与RTX20/30/40完整覆盖、圆刚/BMD反馈实卡、长时间显存稳定及光学端到端延迟，不由本机RTX5070结果替代。此次不宣称NVIDIA长期显存增长已根治。

- [完整对应源码与依赖](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0-sources)
- [全部提交及文件变更索引](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/RELEASE_2.1.0_CHANGE_INDEX.md)
- [构建、原包与源码对应说明](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/docs/BUILD_2.1.0.md)
- [第三方来源及许可证](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.0/THIRD_PARTY_NOTICES.md)

| 文件 | SHA256 |
| --- | --- |
| NVIDIA ZIP | `259a800768e24ee6b8b03e6ee537b5e2a1a7b9f60e7b39c6573055786da33391` |
| AMD ZIP | `e9ad547c0711f03d8277a47cae5e100b8817ee0da496916d56770d1c4927689e` |

## English

**Veyra 2.1.0 adds AMD NR 0.44 Small/Original models, independent anti-aliasing and face beauty/restoration, and fixes missing capture resolutions/formats.** The exact user-approved ZIPs are promoted without rebuilding/repacking; internal2.1.0-test labels remain, PE2.1.0.0. Pick one GPU package above; sources are on a separate non-latest release.

- **AMD NR:** RX9000/RDNA4 continues to use lmxxf, now0.44. Use performance model defaults on(Small310.9.16); off uses Original. Independent modules permit different models per layer, saved in presets/sessions/export. Inactive edits do not rebuild playback; missing assets or incompatible modules fail explicitly. This is not RX6000/7000 support. NVIDIA runtimes are unchanged; no Lecram/ReShade backend is included. No AMD hardware speedup percentage was measured locally.
- **Anti-aliasing:** FXAA/SMAA/MLAA in List and Nodes, quality/threshold/strength controls, up to two nodes, HDR handling, persistence and List export. Optional temporal SMAA defaults off and is an approximation without engine jitter. RTX5070 standard4K fixture GPU costs: roughly0.15/0.30/0.30ms; content changes costs.
- **Beauty:** independent original-face protection and skin smoothing, up to six layers, four presets, editable intensity/feather/range/detail/texture/skin selection. YuNet and468-point MediaPipe contours share detection across nodes; eyes/brows/lips are excluded from smoothing. Protection restores the pre-effect source, including removing earlier colour/SR/NR/beauty changes inside the mask. Per-frame detection and One Euro filtering reduce lag/bounce; uncertain meshes fall back to an ellipse. Final single-face RTX5070 examples:1080p~0.93ms,4K~1.42ms. List export is supported; tiled images skip beauty with a message.
- **Capture(PR57):** native media-type fallback, driver ranges/steps/custom-size checks, precise pixel-format/rate identities, distinct59.94/60 and progressive/interlaced entries, searchable legacy list, MJPEG warning, exact connection validation and saved/resumed capture settings. Driver substitutions fail explicitly. Range candidates still require actual connection; all BMD devices are not declared fixed.
- **Networking(#48):** shared bounded demux, adjustable buffering/prebuffer/reconnect/corrupt-frame policy, real buffering status, bitrate/download diagnostics, HLS best variant, restarted-live timelines, interlaced damage tolerance and throttled server subtitle scans.19-case matrix and short real Twitch tests passed; users' own NAS/streams remain separate validation.
- **AVerMedia(PR63/64):** vendor audio graph and non-PCM query/arming/readback, AC3/EAC3 multichannel decoding and transitions, bounded recovery, immediate complete-payload decoding. Removing one1536-sample decode-stage wait at48kHz is32ms at that stage, not a measured end-to-end latency claim. Local vendor tests used fixtures; real AVerMedia field validation remains required.
- **Other fixes:** earlier safe game-mode input handoff, scaling rules and hotkey/window recovery, static WGC resize recovery, fullscreen click/cursor behaviour,32-node capacity, disabled-node order restoration, NR-bypass slot rebinding, tiled image plans and export protocol consistency. New home/sponsor page, corrected Bilibili profile, complete new translations and refreshed community QR. Existing UI cadence/GPU priority/FG models are retained.

See the detailed sections and linked reports above for test evidence, hardware limitations and corresponding sources. Long-run NVIDIA VRAM growth and untested hardware are not claimed resolved.

## 支持与反馈 / Support & feedback

<p align="center">
  <a href="https://discord.gg/c9aREyMj8"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join Veyra on Discord" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://ko-fi.com/likely7"><img src="https://storage.ko-fi.com/cdn/kofi5.png?v=6" alt="Support Veyra on Ko-fi" height="36"></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v2.1.0/docs/images/2.1.0/community-group.png" alt="Veyra 交流群 6" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群。群码按图片标注于 **2026-10-26 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group. QR valid before 2026-10-26.

[Veyra 软件知识库 / Knowledge base](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)：四个群聊汇总的使用文档，涵盖软件与采集卡常见问题。
