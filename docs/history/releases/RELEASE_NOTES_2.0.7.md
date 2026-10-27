# Veyra 2.0.7 · 混合补帧、内容节奏重做与兼容修复

2026-10-13。相对正式版 **2.0.6** 的完整更新。此次直接采用已交付并经用户测试的 **2.0.7-test-r2** 完整包；包内显示标识仍为 2.0.7-test-r2，Windows 数字版本为 2.0.7.0，程序、运行组件和模型没有为发布再次替换。包内测试说明/清单保留原交付状态；当前发布状态与在线源码入口以本页为准。

[NVIDIA 完整包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7/Veyra-2.0.7-NVIDIA-win64-portable.zip) · [AMD 完整包](https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.7/Veyra-2.0.7-AMD-win64-portable.zip)

按 Veyra 使用的显卡选一个，完整解压到新目录后运行 `veyra_qml_ui.exe`。两个包不需要同时下载。模型/后端各有硬件要求，“AMD 包”不代表所有 AMD 型号都支持所有功能。

**[Veyra 软件知识库](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)**：由社区整理四个群聊的消息，汇总成 Veyra 使用文档；大部分软件设置、常见故障和采集卡问题可以先在这里查找。反馈仍可附日志到 [Issues](https://github.com/Likely7/Veyra-NRVideo/issues)。

## 1. VFG 混合档位：Veyra 最新研究成果

这次的“科研”是 **Veyra 对现有模型的调度与质量分配研究**：将有限 GPU 时间优先分配给较难生成的中间位置。底层使用 NVIDIA VFG 官方模型，没有声称训练出新模型，也没有把 VFG 变成一次推理生成全部子帧的算法。

VFG 在一对原帧 A、B 之间，每生成一张中间帧都需要单独推理。**4X 是原来的 1 个帧间隔分成 4 份，需要 3 张生成帧；8X 需要 7 张，不是分别插入 4、8 张。** 全部用高档时，高倍成本会迅速增加。中间位置通常离两张真实帧都最远，更依赖运动/遮挡估计，因此本方案给它优先使用较高档模型。

| 档位 | 最靠近中点的一张生成帧 | 其余生成帧 | 适用取舍 |
| --- | --- | --- | --- |
| 高+中 | 高 | 中 | 保留高档中点，其余兼顾画质；开销仍高 |
| 高+低 | 高 | 低 | 高档中点与更低总开销之间折中 |
| 中+低 | 中 | 低 | 更重视高倍率的可运行性 |

以 4X 为例：A → 低档 t=¼ → 高档 t=½ → 低档 t=¾ → B，即“高+低”。偶数倍率选正中点；奇数倍率选中点之后紧邻的那一张。**2X 只有一张生成帧，混合档等同前一档，不能期待混合省时。** 其它倍率也始终只有一张使用前一档。

两个模型实例共享同一 CUDA 流，每对输入各绑定一次，推理仍按序执行。它不是把两张图混色、不是锐化，也不是自动在高/中/低之间切换。**默认仍为中档**，用户在列表/节点的 VFG 质量中自行选择。预设/会话可保存新档位；2.0.6 及更早版本不认识混合档，回退前请保留新版预设副本，不用旧版强行覆盖。

同时把 SDK 内“每张子帧都重复做”的自动镜头切换检测，改为 **每对 A/B 一次 GPU 检测**；硬切时在 GPU 上选择当前原帧，避免跨镜头生成假画面，不将检测结果读回 CPU。减少检测引起的主机等待和重复工作。它仍有一次专用检测派发，并非零成本；淡入淡出等软切判定也可能与 SDK 不同。

### 本机数据与选档参考

RTX 5070、驱动 616.56、GTA VI 原生 4K30、单层 NR；每组 20 秒、预热 8 秒。来自本次开发期间的同 EXE 对照，**不是所有机器的承诺，也不是发布时新跑出的全硬件数据**。FPS 为软件提交读数，不是物理屏幕刷新率。

| 4X / 目标 120fps | 软件提交 FPS | GPU 占用 | 补帧整组 P95 |
| --- | ---: | ---: | ---: |
| 原中档＋SDK 切镜检测 | 120 | 60% | 12.62ms |
| 新中档 | 120 | 58% | 11.45ms |
| 新中+低 | 120 | 45% | 6.92ms |
| 新高+低 | 120 | 82% | 19.80ms |
| 新高+中 | 120，存在跳过/降档 | 95% | 24.29ms |

相同中档下，整组 P95 12.62→11.45ms，约 **降低 9.3%**。新中档换成中+低后，11.45→6.92ms，约 **降低 39.6%**，GPU 占用 58%→45%，下降 **13 个百分点**；后者包含较低档模型带来的质量取舍，不能宣传为同画质提速。

| 8X / 目标 240fps | 软件提交 FPS | GPU 占用 | 补帧整组 P95 |
| --- | ---: | ---: | ---: |
| 原中档＋SDK 切镜检测 | 210 | 98% | 29.9ms |
| 新中档 | 228 | 98% | 27.2ms |
| 新中+低 | 240 | 64% | 13.2ms |

该场景中，中+低把 8X 从跑不满变为 240fps；不是说所有显卡都能 4K 8X。两实例可能增加显存，例如该 4X 测试整卡峰值中档约 6.27GB、中+低 6.65GB、高+中 7.87GB。相邻生成帧来自不同档，细纹理、发丝和字幕边缘可能出现质感变化/闪动，实际观感请对照。高+中在本机 4X 已接近极限，保留高档并不代表每张都有高档质量。

此外保留跳过超预算组时的输入历史，减少无用种子推理；细化单张/整组预算、严重超载估计和有界恢复探测；状态会说明每张成本、可稳定倍率及屏幕限制。此前实验中的自动选最大倍率、连续 CUDA 运算和交换链预等出现负收益，**已经撤回，未混入正式包**。当前调度仍可能按预算少生成或跳过超时工作，混合档并不保证请求倍率永远达标。

详见 [混合档原理与原始数据](../reports/VFG_MIXED_TIERS_2026-10-11.md)、[此前预算修复](../reports/VFG_NEXT_REPORT_2026-10-07.md)、[后续尝试与撤回](../reports/VFG_TASK_2026-10-10.md)。2.0.6 已公布的 80→120fps 等数据不重复算成本版新增收益。

## 2. 自动识别内容节奏重做，固定 60→30 还原

60Hz 传输不一定有 60 张不同画面，30fps 游戏常是 AABBCC。重复画面进入 NR/补帧会浪费处理机会，内容去重在增强之前进行。

旧方案只看 64×36 个稀疏亮度点，较小/较暗的移动容易漏掉，P010 又只取高 8 位；原地站立的小动作可能被错当静止，出现 6–16fps 以及 GPU/VFG 耗时波动。最终方案改为：

- **自动识别/识别 30、50、60fps 内容**：整幅画面、所有平面逐像素与“上一张保留的新画面”比较。每个分量都在采集噪声容差内才算重复，任一超出即保留；慢变化会累积，不是只与上一张输入比较。
- 容差是 8 位刻度 **±3 级**，10 位为 ±12 级；部分打包格式用完全相同判断。采集回调只在去重开启时比较驱动样本并编号，引擎消费画面号，不为此从 GPU 回读图像。
- 逐字节完全相同曾实测无效：T911 同一画面每次输出也有 1–3 级噪声。因此本版不是“百分之百无损识别”：整张画面变化都落在容差内仍可能被视为重复；压缩/噪声更大的重复画面也可能识别不出。
- 持续静止时按已学习内容节奏保留画面，避免只能靠低频兜底放行造成 6fps。硬解串流和 MJPEG 缺少合适 CPU 原图，当前不做这种内容去重，不宣称覆盖所有来源。
- **“采集 60→30”回到独立固定隔帧采样**：AABBCCDD→ABCD，保留真实输入时间戳，不经过画面判断。输入确为稳定 60 时输出稳定 30；它不能把本来欠速/不规则的输入修造成完美 30。

T911、2560×1440@60、30fps 游戏、NR＋VFG 4X：NV12 自动模式每秒保留约 30–31 张、提交 **119.7±1.1fps**；固定 60→30 保留恒 30、提交 **119.9±0.6fps**；P010 自动也约 30–31 张。用户已实机确认该修复。小低对比运动离线样本旧法仅留 134/600，新法留 **598/600**，另两张变化本身不超过容差，不能写成零误判。

整画面比较有成本：该卡 1440p 回调复制/比较 NV12 约 0.27→0.5–0.8ms，P010 约 0.6→1.35–1.6ms；仅内容去重模式承担，固定 60→30 与源时间戳模式不增加这项比较。其它卡噪声、CPU 和格式表现仍需实测。

详见 [节奏重做、实卡与误判边界](../reports/CONTENT_CADENCE_EXACT_2026-10-12.md)。

## 3. XeSS「边缘神盾」：略微改善，保留开关

人物轮廓附近错误运动会把前景向量带入刚露出的背景，产生弯曲/果冻。新增 **边缘神盾（Edge Aegis）**，位于列表“补帧 → 光流·运动估算”及节点输入光流卡的算法选择下面；默认开，可关闭对照，选择自动保存。

- 仅 **XeSS＋运动来源“光流”** 生效。先按图像匹配精修边界，再判定不可靠的露出背景，从附近可信区域填补向量。
- NVIDIA NVOF 用前后向光流一致性检查；FidelityFX 光流用“上一帧位置认领”检查。后者没有真实后向光流，效果不等价。
- 保持 NR/SR 的光流输入以及其它补帧算法；零运动模式不额外精修。不是升级 XeSS 模型，也不把光流提高到全分辨率来堆成本。

RTX5070、2560×1440、XeSS2X、合成人物/背景，每场景 11 张真实插值帧：NVOF 路径相对本轮最初未精修版，轮廓带 MAE 侧跑 6.8755→6.4016、高速侧跑 8.2148→7.3042、横移背景 3.7651→2.5901、密集栏杆 10.1397→9.6196。**这是合成误差指标，不能换算成真实游戏观感改善百分比。** 进一步前后向检查相对最初精修版，有些场景改善小，慢侧跑还小幅变差（6.3781→6.4016）；四肢高速摆动与栏杆残差未解决。

开启有 GPU 成本：该机 4K 源、1080p 光流，无窗口 NVOF 光流阶段约 1.03→2.29ms，FidelityFX 约 0.41→1.01ms。FidelityFX 的合成轮廓误差约降低 4%–29%，**同样是在 RTX5070 上运行该后端，不是 AMD 显卡实测**。对观感不合适或预算紧张的场景可关闭；本版只称有限改善。

详见 [边缘神盾、数据与剩余问题](../reports/XESS_FLOW_REVIEW_2026-10-12.md)。

## 4. 其它补帧、超分与状态

- **FSR 单独补帧的光流初始化**：以前 NR/SR/独立光流全关时，FSR 虽能生成帧却拿到零运动。补齐 FSR/FSR4 消费者条件，显式光流和引擎原有自动规则实际初始化光流；主动选择零运动仍保留。FSR 不使用 XeSS 专用精修。实际用光流会增加必要成本，不称提速。见 [修复与负对照](../reports/FSR_FLOW_FOLLOWUP_2026-10-12.md)。
- **DLSS SR 增加 K/M/L 选择**：保留 K 默认，可请求 DLSS4.5 M/L；列表、节点、草稿、预设和会话分别保存，新格式采用旁文件保护旧配置。接口提交的是 preset hint，成功出图不等于能读取厂商内部选用的模型，也不保证普通视频必然更清楚。见 [接入验收](../reports/DLSS45_SR_REPORT_2026-10-11.md)。
- **RTX Video SR/FSR 执行缺口**：原尺寸 VSR 恢复处理不再被当作“不放大所以跳过”；FSR 首帧/无有效运动时使用原有空间缩放路径。导出说明按共享图实际 SR 状态判断。这是执行正确性修复，不宣称全面画质提升；原来没做的工作现在会消耗 GPU。
- **超分负优化全部撤回**：静态运动归零、局部/整帧静态检测等画质实验没有证明全面收益，额外 GPU pass 和纹理已移除，不为更锐而强加滤镜。见 [测试与撤回](../plans/SR_VISUAL_QUALITY_PLAN_2026-10-10.md)。
- **正常限速不再误报**：按去重后的有效内容节奏、播放速度、实际补帧、对比暂停、已生效输出上限及视频所在屏幕的垂直同步上限计算目标。状态上方显示启用选项/原因，把合理限速标为正常；真正欠速、时间线异常和预算降档仍告警，不是单凭开关亮着就放绿。
- **设置应用确认**：修复同时关补帧、改速度、seek 后首帧被跳过导致一直“调整中”。新设置首帧实际成功后再确认。见 [状态规则与真实 GUI](../reports/CADENCE_STATUS_REPORT_2026-10-11.md)。

## 5. 采集格式、自动保存与补帧稳定性

接入并适配 [PR34](https://github.com/Likely7/Veyra-NRVideo/pull/34)（GeT-LeFt），补齐三个选择框背后的能力枚举及持久化：

- **先分辨率，再帧率，再像素格式**。后两项受实际驱动组合约束；更改前项后需重新确认具体格式，有效历史完整配置仍恢复。
- 枚举驱动公开的所有离散媒体类型/帧率，保留扫描方式、字段顺序与精确时间间隔，不固定几个常用格式、不根据范围凭空补模式。当前输入能力以驱动公开数据为边界；超出播放器接入能力的条目仍列出并标不可用。
- **MJPEG 标红“（不建议使用）”**，仍可明确选择。无效、过期或设备不支持的组合提示 **“此采集卡无此格式”** 并拒绝连接，不偷偷改成 RGB 或其它模式。真正解码失败单独报解码错误。
- 设备、格式、帧率、音频、色彩/范围在修改时保存；翻转、转 SDR、设备缓冲、音频入口模式、Blackmagic/美乐威选项一起恢复。**不必点连接才保存**。继续上次/启动自动继续共用最新表单，缺失设备/格式/端点保留原配置并提示，不静默换卡或关音频。
- 连续刷新/切卡只接受最新异步查询；设置原子写入，保存失败提示；旧格式键仅在唯一匹配时迁移。
- 修复部分 Elgato 时间线的 **1ms 短间隔污染下一帧基线**：用最近 5 个有效间隔的稳健中位数，避免正常下一帧被误认断点、反复重置 NR/光流/补帧。只是保存 5 个数字，**不是再缓存 5 帧或增加 5 帧延迟**；真实 PTS、VRR 和真正断点仍保留。

本机 T911：驱动公开能力展开为 **378 个组合，界面 378/378 可达**；1080p30 的连接/重连/继续/启动恢复通过。该数字不是 378 种信号都实测出画，也不是所有采集卡拥有相同列表。异常时间线 GPU 回放修复前误重置、修复后四组均 0 次误重置；原现场 5070Ti/Elgato 仍需复测。

详见 [格式与保存](../reports/PR34_CAPTURE_SETTINGS_REPORT_2026-10-12.md)、[最终选择安全](../reports/TEST_2.0.7_REPORT_2026-10-12.md)、[时间线修复](../reports/FG_CAPTURE_RECOVERY_REPORT_2026-10-12.md)。

## 6. Blackmagic、Elgato 与 HDR 兼容

- **Blackmagic 原生采集开关**：在采集页与美乐威模式并列，默认关；关闭走 DirectShow，开启走已安装 Desktop Video 的 DeckLink 原生接口，重新选择对应设备/格式后连接。支持的卡可自动检测实际信号，其余手动；记录无输入、实际制式与位深变化。HDMI/SDI 沿用驱动配置，不盲切端口、不改 EDID。内置音频当前为 48kHz/16bit/立体声。
- DirectShow 路径参考 OBS 的驱动 allocator 协商和 renderer 标记，尊重驱动 DIB 方向、严格检查实际连接合同。**此前兼容修复在反馈者现场仍黑屏**，新增原生接口是进一步解决路径，不能写成全型号黑屏已根治。本机无 BMD 卡；状态机/10位 GPU 颜色/普通 USB 回归通过，实际出画和输入到屏幕延迟待实卡。见 [原生采集说明](../reports/DECKLINK_NATIVE_REPORT_2026-10-09.md)。
- **Elgato HDR/SDR**：扩展设备识别，支持属性的设备在 8 位格式连接时请求卡内 HDR→SDR；关闭时恢复可读原值。非 MK.2 P010 自动范围按“亮度有限、色度全范围”处理，手动范围优先，改善已复现的 GPU 过饱和计算缺口。保留 MK.2 严格合同。USB HID 控制未移植，不能声称所有 Elgato 型号都恢复 RGB/HDR。见 [条件与未验型号](../reports/ELGATO_HDR_PARITY_2026-10-11.md)。
- **HDR 补帧闪黑兼容**：设置页新增开关，**默认关闭**、立即保存；仅符合 NVIDIA HDR10 应用补帧条件的直接呈现路径采用单张合成纹理复制，XeSS 保持原路径。可能增加显存带宽/纹理开销，切换会短暂停顿。默认路径不分配这张纹理。现场闪黑根因未最终确认，不宣称默认已全部修好，也没有改变 HDR 导出能力。见 [开关与代价](../plans/HDR_FG_COMPAT_TOGGLE_PLAN_2026-10-09.md)。

## 7. 设置切换、快捷键、界面与叠加层

- **改选项后“软件错误”、要重启**：相同窗口/设备/输出合同的效果图重建复用交换链，避免旧引用尚存时再次 `CreateSwapChainForHwnd` 被拒；resize 失败仍保留可用旧缓冲，释放外部引用后可恢复。保留失败回退诊断，真正 HDR/SDR/窗口等合同改变仍按需重建。不能据此声称所有驱动/叠加层概率故障都已现场根治。
- **关闭的效果器编辑卡顿**：保存全部新值，但只比较实际启用效果的有效参数；编辑未开启 NR/SR/FG/LUT 等不再排空/重置 GPU 图，开启时用最新设置。逐层 NR 和共享光流按真实消费者判断，正常开启项的重建与错误回退保留。全局抗闪烁强度/容差保存也补齐。
- **数字手输后快捷键失效**：提交、Esc、失焦正确释放隐藏编辑器焦点；切到另一输入框不会抢走其焦点。滑杆、限幅和非法输入保持。
- **OBS/RTSS 软件 UI 滚动拖影、图标越界**：软件模式把同一路径图标/虚线边框栅格化成缓存 Image，正确遵守 clip；GPU 模式保持 Shape。没有整窗强制刷新、新 Timer 或抬高 GPU 优先级。统计仍为原 33ms 插值/约250ms状态节奏，避免重回2.0.4高频UI绘制问题。生产页面滚动对照相关残留 **1237→0 像素**、固定头部越界 **209→0**；这是对应测试区域结果，不是性能百分比。
- **专业模式画面挡播放条**：统一宿主与专业页的响应式布局尺寸，原生视频 HWND 跟随正确可见区域。GPU/软件 UI、100%/150%、窗口/全屏等 **52 个状态**，播放条遮挡 0，原生窗口误差在 1 物理像素内。
- **长文字与多语言**：菜单按实际文字测量并限于窗口，正文/说明换行，有界滚动；节点右键、窄窗口统计、操作提示、采集格式选项与按钮处理截断并可看全文。简中/繁中/英文/日文适配，未宣称所有历史词条已翻译完整。
- **NVIDIA 信息浮窗/游戏加加兼容**：监控兼容“自动”检测后使用软件 UI，仅视频保留 D3D12；运行中才出现时提示重启。引擎 8 秒无进展记录阶段、栈与每进程最多一份小转储；退出有界等待，避免永远挂进程。修复看门狗退出回调生命周期。驱动首次冷编译也可能触发提示后自行恢复，诊断并非根因证明。

详见 [设置/快捷键/布局](../reports/SETTINGS_SR_UI_REPORT_2026-10-10.md)、[关闭项编辑](../reports/INACTIVE_EFFECTS_REPORT_2026-10-10.md)、[软件 UI 与性能保护](../reports/UI_SOFTWARE_DAMAGE_REPORT_2026-10-11.md)、[画面位置](../reports/PRO_VIDEO_GEOMETRY_REPORT_2026-10-12.md)、[叠加层与看门狗](../reports/FREEZE_FIX_2026-10-10.md)。

## 8. 音频、字幕与播放列表

- **文件音频预读**：读包移到独立线程，有界队列上限 2.5秒/4096包/16MiB，稳态输出线程不再同步读盘，降低慢存储阻塞音频时钟进而拖住视频的风险。seek 清空/预填、暂停、倍速、端点重开沿用既有规则；不是额外固定播放延迟，也不能保证所有存储不会卡顿。见 [证据与回归](../reports/AUDIO_READ_AHEAD_2026-10-11.md)。
- **PR31 采集音频输出切换**：保留 [GeT-LeFt 的修复](https://github.com/Likely7/Veyra-NRVideo/pull/31)，端点 generation 改变后正确重开，避免切耳机/音箱继续占旧端点；实际静音 PCM/端点恢复回归通过，不修改 Windows 默认设备。
- **内嵌 PGS 不可用**：FFmpeg 开启 zlib，修复 Matroska 压缩 PGS 能列轨却解不出内容；保留 PS5 H.264 slices256 补丁。补齐 BlockDuration 结束时机、清屏/seek 和“正在读取/无数据/失败/缺 decoder/缓存上限”状态，不能假定所有用户原片都是同一原因。
- **外挂字幕**：支持 SUP/PGS、VobSub IDX+SUB 多语言；接通并实测 MicroDVD、SubViewer/SBV、SAMI、MPL2、Spruce 文本 STL，保留 SRT/ASS/SSA/WebVTT。后台可取消加载、主副轨/各自延时、同名发现和错误保留现有字幕。TTML、EBU 二进制 STL 不在此次支持声明，缓存仍有上限。见 [字幕格式实测](../reports/SUBTITLE_FORMATS_REPORT_2026-10-09.md)。
- **issue21 播放列表**：极简底栏/全屏入口，独立浮窗可移动、放大缩小；导入文件夹/子文件夹，继续拖入其它目录/文件，拖动重排，按名称自然顺序/时间/大小排序，单项移除、去重、持久化、正在播放/暂停标记；自然播完自动下一项，追加/排序不打断当前播放。移除不删除磁盘文件，手动停止/失败/直播不冒充自然播完。后台扫描和虚拟化支持200+文件，不遍历解码每部影片。见 [列表与音频回归](../reports/PR31_PLAYLIST_REPORT_2026-10-09.md)。

## 9. 诊断、交付与已知边界

- 补充输入/实际呈现像素的闪黑诊断、NVIDIA 显存增长现场收集、CUDA 分阶段时间与 DXGI Present→显示事件统计。专用采集脚本需按对应文档运行，不是默认抓取全部画面；Present 统计不是屏幕光学延迟。
- 2.0.7 首轮整合28组定向回归，刷新版完整 CMake/Ninja548步骤、27目标；16组定向回归、100项时间线断言、3组内容真值回放，以及最终 NVIDIA/AMD ZIP 各 GPU/真实软件 UI 共4次解压启动通过。来源旧测试数据与最终包验收分开记录。
- **扩展 NR＋DLSS 热切换回放未全绿**：刷新版31/34，旧原2.0.7同场33/34，均出现驱动/NGX冷初始化长停后追赶超时；当时用户另一个播放器仍在运行，不能断言并发是唯一原因。失败保留，没有放宽门槛或包装成全部验收通过。详见 [最终包验收](../reports/TEST_2.0.7_REFRESH_2026-10-12.md)。
- **NVIDIA 长期显存增长尚未根治**。不同 AMD/Intel、BMD/Elgato 实卡、HDR现场闪黑、长时间稳定性及输入到屏幕延迟没有全面验收；社区 NR/运行库继续为实验性质，不代表厂商认证。
- 公开 AV1 4K 高码率样本的软件回退极慢在旧版也复现，本版没有修复该情况。节点离线导出、原生 BMD 多声道等既有限制不因本次发布自动解除。
- 下载区只提供两个运行包；[独立对应源码归档](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7-sources)保留本次应用源码、patched FFmpeg/dav1d/zlib，以及未改变依赖源码的固定链接。源码不含专有 SDK/运行库/模型，包内 manifest 是发行审计清单，不是用户 DLL 哈希锁。

全部提交及最终产品文件清单见 [2.0.6→2.0.7 变更索引](RELEASE_2.0.7_CHANGE_INDEX.md)。

## English

**2.0.7 promotes the already tested 2.0.7-test-r2 packages unchanged.** Internal display version remains test-r2; PE version is 2.0.7.0. Download one vendor package and extract it completely into a new folder. The main Release has only NVIDIA and AMD runtime packages; [corresponding sources](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7-sources) are archived separately.

**[Veyra knowledge base](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)** collects community guidance from four group chats, including software setup and capture-card questions.

### VFG mixed tiers — Veyra's latest research

This is Veyra's model-allocation and scheduling research using existing NVIDIA VFG models, not a new trained model. VFG still runs inference separately for every generated frame: 4X inserts three, 8X inserts seven. High+Medium, High+Low and Medium+Low spend the first tier on the midpoint frame (the first frame after the midpoint for odd multipliers), and the second tier on all remaining frames. The midpoint is generally further from real input frames and harder to interpolate. At 2X there is only one generated frame, so the mixed tier equals its first model and offers no mixed-tier saving. Models share one CUDA stream and run serially. Default remains Medium; this is an explicit user selection, not automatic model switching.

Shot-change detection now runs once per A/B pair on the GPU rather than inside every SDK inference. Hard cuts select the current real frame without CPU pixel readback. On RTX5070/616.56, GTA4K30+one NR layer, 20s windows after 8s warm-up, Medium4X group P95 fell 12.62→11.45ms (9.3%). New Medium→Medium+Low changes 11.45→6.92ms (39.6%) and GPU utilisation 58→45%; this second comparison trades some model quality for cost. At 8X, old Medium/new Medium/Medium+Low submitted 210/228/240fps, at 98/98/64% GPU. These are local software submission measurements, not display FPS or universal gains. Two models can increase VRAM; alternating quality can cause texture breathing. High+Medium4X was already near this machine's limit. Older versions reject mixed-tier preset/session values rather than overwriting the file.

### Content cadence rebuilt

Sparse 64×36 luma sampling could miss small movements and mistakenly reduce active scenes to 6–16fps. CPU-visible content modes now compare the whole image against the last new picture, across all planes, using a ±3/255 capture-noise band (±12 at 10bit). A single component outside the band keeps the picture; slow changes accumulate. Long still scenes follow the learned cadence. Hardware-decoded streams and MJPEG currently do not use this deduplication. Changes entirely within the noise band may still be lost; noisy duplicates may remain.

Fixed capture60→30 is again a separate timestamp-based every-second-frame sampler, not content detection. T9111440p60 with a30fps game, NR+VFG4X: NV12Auto kept30–31 pictures/s and submitted119.7±1.1fps; fixed half-rate kept30 and submitted119.9±0.6. Full-image comparison adds CPU work only in content modes. Small-motion replay retained598/600 real pictures versus134/600 with sparse sampling; two changes were within the noise tolerance.

### XeSS Edge Aegis — limited improvement

A persistent, default-on switch under optical-flow selection refines XeSS motion boundaries and fills unreliable newly uncovered backgrounds. NVIDIA uses forward/backward consistency; FidelityFX uses previous-frame position claims. It applies only to XeSS with optical-flow motion, preserving NR/SR guidance and other FG algorithms. Synthetic interpolation errors improved in some cases, with slight regressions elsewhere; fast limbs and fences remain problematic. At4K/1080p flow on RTX5070, NVOF flow cost was roughly1.03→2.29ms and FidelityFX0.41→1.01ms. FidelityFX results on this NVIDIA GPU are not AMD hardware validation. Disable the switch when its visual/cost trade-off is unsuitable.

### Other changes

- FSR-only FG now initialises optical flow when requested even with NR/SR/standalone flow off; explicit zero-motion remains available.
- DLSS SR K/M/L preset hints, independent List/Node/session/preset persistence; K remains default. Correct native-size VSR and FSR first-frame execution. Unsuccessful SR quality experiments were withdrawn.
- Intentional deduplication, VSync, active FPS caps, playback speed and comparison mode use the effective output target and explain normal limits; real underperformance still warns. Fix pending-settings acknowledgement after simultaneous FG-off/speed/seek.
- Adapt PR34: enumerate all driver-exposed discrete capture modes, resolution→FPS→pixel-format selection, red MJPEG warning, strict errors for unsupported combinations, no silent fallback. Save complete capture settings before connecting, restore flip/SDR/buffers/audio/vendor options for resume/startup, preserve unavailable selections. A short PTS outlier no longer resets FG history on the following normal frame; the robust baseline stores five intervals, not five frames.
- Opt-in, default-off Blackmagic native DeckLink capture with actual signal/mode diagnostics; OBS-style DirectShow allocator compatibility remains. Elgato tone mapping and mixed P010 chroma-range handling, respecting manual overrides. Physical BMD/Elgato recovery remains unverified.
- Default-off HDR FG flash compatibility copy with explicit memory/bandwidth cost; it is a field candidate, not a proven universal fix or expanded HDR export support.
- Reuse compatible swap chains across graph rebuilds and preserve buffers after failed resize. Editing disabled effects no longer resets live GPU work; manual numeric edits correctly release keyboard focus. Software-UI icons use cached raster images for clipping without full-window repaint or higher UI rates. Fix professional viewport overlap, multilingual menus and narrow layouts.
- NVIDIA overlay/GamePP automatic software-UI compatibility, engine-hang stack/minidump diagnostics and bounded shutdown. Driver cold compilation may also trigger the watchdog and then recover.
- Bounded independent file-audio read-ahead; PR31 capture render-endpoint reopening. zlib-enabled patched FFmpeg fixes compressed MKV PGS; external SUP/PGS/VobSub and additional text formats, asynchronous subtitle loading/status/clear/seek fixes. Persistent folder playlists with floating resize/move, drag reorder/append, natural-name/time/size sort, removal, current-playback indication and natural-EOS next item.
- Additional flash/VRAM diagnostics and Present timing. Negative CUDA batching, automatic largest-group and swap-chain pre-wait experiments stay reverted.

Final package validation:16 targeted groups,100 timing assertions,3 cadence replays and4 actual extracted-ZIP GPU/software-UI starts passed. The broader NR+DLSS hot-switch replay still timed out (also on the previous2.0.7 baseline); it was not declared fully passed. Long-run NVIDIA VRAM growth, all-vendor hardware coverage, field HDR/BMD issues and optical display latency remain unresolved or unverified. Existing high-bitrate AV1 software-fallback problems also remain. Experimental community runtimes are not vendor certification.
