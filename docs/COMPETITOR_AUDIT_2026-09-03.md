# Veyra 竞品与方法审计（2026-09-03）

> **2026-10-04 历史事实与当前用法**：以下外部状态按2026-09-03/06审计日期理解，不作为今天最新SDK或竞品能力证明。用户2026-09-16已允许优先移植有明确许可证的成熟开源实现；下文旧“Magpie不可复制”策略已被该授权覆盖，来源/固定提交/许可证/修改仍必须标注。本轮Magpie NR性能研究原件在E盘deps，逐项先验证适用于Veyra的时域模型、PTS和资源所有权，再决定移植；外部收益估计不是本机数据。Veyra当前能力/未验项目见CURRENT_STATUS，性能进度见PERF_EXECUTION_NR_2026-10-04。

本文件回答两个问题：目前类似项目做到哪一步；评论区“逆编译整个渲染管线，效果与原生无区别”对 Veyra 真正有何启发。它是路线依据，不是第三方代码复制清单。

## 1. 结论先行

1. **Veyra 仍有意义，但“又一个能跑 Feature 18 的视频工具”已经没有意义。** 过去数天已有多个图片/视频增强器出现。Veyra 的差异只能是：物理采集卡实时入口、低延迟原生窗口、同一引擎兼顾播放和导出、以及比简单 Zero Motion 更可靠的 Guidance。
2. **Magpie 已经拥有 NVOF motion、confidence、Depth Anything V2 Small 和 DLSSG。** 因此只补一张深度图不会自动超过 Magpie。
3. **评论区所谓“原生”等价依赖游戏内部数据。** 游戏注入可以在正确渲染点取得 raw hardware depth、engine motion vectors、exposure/camera data、HUD-less color。HDMI 采集与压缩视频只有最终像素，这些信息不可逆地丢失；估算值不可能被诚实地叫作“与原生无区别”。
4. **可迁移的不是逆编译行为，而是完整输入契约：** 正确颜色、current→previous motion、深度、曝光、reset、subrect、UI 分层和资源时序。Veyra 要独立实现这个契约。
5. **首发不是最小 MVP。** 三个入口、4K SDR、真实硬件编码、恢复与诊断都属于发布门槛。HDR、3X/4X、VFR 原样输出和厂商私有采集 SDK 仍是独立能力，不能因为“4K”三个字被偷偷混进来，也不能拿它们拖延三个主入口。

### 1.1 2026-09-06 官方状态补充

- NVIDIA 已正式面向消费者发布 DLSS 5，并在 NBA 2K27 首发：<https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/>。
- NVIDIA Research 将它描述为使用当前渲染帧、引擎 motion vectors、时序状态和艺术控制的 3D-guided neural rendering：<https://research.nvidia.com/labs/adlr/DLSS5/>。
- 截至本次审查，公开 DLSS Developer 页面仍未提供可供任意 D3D12 视频应用下载的 DLSS 5 feature/header/API：<https://developer.nvidia.com/rtx/dlss>；公开 Streamline 仓库也没有对应公共 feature：<https://github.com/NVIDIA-RTX/Streamline>。
- 所以“正式上线”证明 NVIDIA 已把产品交付给合作游戏，不证明 Veyra 已获得正式 SDK、Application ID、任意视频输入契约或 runtime 分发权。
- 用户决定不等待公开 SDK，继续用当前固定 hash 的实验 Feature 18 做本机研发。竞品结论不变：可调用不是壁垒，Veyra 的价值仍必须来自采集/播放/导出、时序、Guidance、质量诊断和离线未来帧。

## 2. 评论区方法的可迁移部分

截图中的作者声称其项目通过逆向游戏渲染管线实现“原生 DLSS + 档位切换 + DLSS 5”。在没有仓库地址、调用日志和可复现实验的情况下，这句话本身不能当证据。但它指向了正确的工程原则。

### 2.1 游戏内原生路径为什么通常更好

DLSS 的时间模型并不只看一张 RGB 图。高质量调用至少涉及：

- 插入点正确的 scene color，最好不含 HUD/字幕；
- 与 color 同帧、同视角的 depth；
- 单位、方向和缩放正确的 motion vectors；
- jitter、render/output subrect、exposure、camera 常量；
- scene cut、resize、seek、drop、device lost 时统一 reset；
- UI 在重建/补帧后再合成。

游戏内注入可以从资源绑定或渲染 pass 中拿到这些数据。评论者的“逆编译渲染管线”若确实有效，价值就在这里，不在某个 DLSS DLL 开关。

### 2.2 Veyra 能照搬什么，不能照搬什么

可做：

- 把三种输入统一为严格的 `FramePacket` 与 `GuidanceFrame`；
- 用 NVOF 估算 motion，并利用 cost、前后向一致性、亮度残差和深度残差生成 confidence；
- 用 Depth Anything V2 Small 估算相对深度，并以 motion 重投影旧深度；
- 对不可信区域将 motion 平滑衰减到 0，而不是整帧盲信；
- 在 seek、切镜、采集丢帧或 PTS 跳变时让 SR/NR/FG/flow/depth 同时 reset；
- 将字幕、播放控件和诊断 OSD 放在 DLSSG 之后；
- 离线导出利用未来帧做 forward/backward consistency，这是实时 Magpie 做不到的非因果优势。

不能诚实做到：

- 从 HDMI 像素恢复游戏的 raw hardware depth；
- 从烧录进画面的 HUD 还原完美 HUD-less color；
- 从压缩视频恢复准确的 engine motion、camera matrices 或 exposure；
- 未做同源 A/B 就宣称“原生等价”或“必然优于 Magpie”。

未来可以另做游戏端 sidecar，把原生 depth/motion 随采集画面传给 Veyra；它不是本次三入口首发 gate，且不能拿来掩盖纯 HDMI 模式的边界。

## 3. Magpie 当前状态

审计仓库：<https://github.com/SAOG0721/Magpie>

审计 commit：`289dc0f6d52075f5a06b47a3f70b35d438095bf5`（2026-09-03 14:14 +08:00）。

已在源码中确认：

- NVOF motion，S10.5 `/32` 处理、grid/densify、cost→confidence；
- 可用时做 forward/back consistency；
- Depth Anything V2 Small FP16，异步执行、P02/P98 + EMA 归一化；
- 深度不是每帧重算，默认 interval 4，并用 motion/confidence 重投影和过滤；
- Guidance 可切 Available / Force Zero / Motion Only / Depth Only；
- SR、Feature 18 和 DLSSG 共用 Guidance；
- DLSSG 2X/3X/4X，且明确注册缺失的 HUD-less/UI 类资源；
- 诊断视图含 motion、depth、confidence、depth residual。

可攻击点：

- Magpie 面向桌面/窗口捕获，不是物理采集设备工作站；
- 它接到的是最终合成帧，无法恢复真正的 HUD-less color 和 engine buffers；
- DAV2 路径仍存在 staging/CPU 参与，深度为因果、间隔更新；
- 播放器/导出场景可以利用文件 PTS、无屏幕二次捕获、离线未来帧和独立字幕层，Veyra 在这些场景有结构优势。

因此目标不是“加深度就超过 Magpie”，而是针对三个场景做更合适的调度和质量策略。

## 4. 直接同类仓库

### 4.1 Merserk/dlss5-visual-enhancer

仓库：<https://github.com/Merserk/dlss5-visual-enhancer>

审计 commit：`f90f7319eb1321601c61db83498a53032f1f2934`。MIT 只覆盖其原创代码；运行包仍涉及 NVIDIA、ReShade/RenoDX、FFmpeg 等独立许可。

它已经把图片、视频、批量导出、HDR 选项和 DLSSG 包装成 Gradio 产品，所以 Veyra 不能再以“有导出按钮”为差异。源码层观察：

- 可见 Python 层用 OpenCV DIS optical flow，工作宽度约 640；切镜仅用缩小灰度图平均绝对差阈值；
- 可见仓库不含关键 native worker 二进制源码，Git checkout 本身不能证明全部 README 功能；
- 图片/视频工作流、输出不覆盖、作业 manifest、音频/字幕处理值得作为产品检查表，但不得把 README 声明当 Veyra 的实测结论。

### 4.2 DaniilSokolyuk/video2dlssnr

仓库：<https://github.com/DaniilSokolyuk/video2dlssnr>

审计 commit：`e1946117699c4e6dcd531f5e042401d04268320e`。审计时仓库没有 LICENSE/COPYING，**不得复制代码**。

源码显示其顺序为 DLSS SR → optical flow → Feature 18 → composite；支持 NVOF，失败时退到 compute Lucas–Kanade，并有 scene-cut reset。值得吸收的行为：

- SR 在 NR 前；
- motion 必须有可视化和已知平移测试；
- NVOF cost 应参与置信度衰减；
- GPU 主路径应避免中间 readback。

但其 README 所称“FFmpeg decode 到 encode 都不回 CPU”与 raw stdin/stdout 架构表述存在歧义，不能照抄宣传。

### 4.3 Zonnery/dlss5-nr-player

仓库：<https://github.com/Zonnery/dlss5-nr-player>

审计时没有许可证。代码包含直接 Feature 18 Create/Evaluate，但主要是 FFmpeg raw pipe、GPU upload/readback 和 Zero/placeholder guidance；有播放器、离线转换和桌面复制，没有物理采集卡。不可复制代码。

### 4.4 SamG-Coder/dlss5-infinity-studio

仓库：<https://github.com/SamG-Coder/dlss5-infinity-studio>

审计 commit：`4e936b4c3df4a448a86e197396913491354ba312`，MIT。它展示 DirectML depth + Streamline feature 1004 + NVENC 的离线工作站路线，但 README 自己承认 motion 仍为 zero、无 scene-cut reset、VFR/HDR 未完成。Veyra 继续直接 NGX，不为了追赶而同时接入 Streamline。

### 4.5 jlrouzies-fr/DLSS5-Feeder

仓库：<https://github.com/jlrouzies-fr/DLSS5-Feeder>

审计 commit：`03da7d9ca36c92f462ed936f5a629116b6b8ee5b`。它给无 DLSS 游戏制造 DLAA contract：ReShade backbuffer + raw hardware depth + estimated motion，再触发 DLSS/Feature 18。

最值得并入 Veyra 自有实现的思想：

- motion validation 不只看一个 flow cost，还看 static hypothesis、luma、depth 和 forward/back consistency；
- 不可信像素用 trust mask/gating；
- E/F 类旧 CNN preset 对透明粒子、烟尘、火焰会更强地限制 history，J/K transformer 倾向不同；但 Veyra 只能在官方 header 确认且 A/B 后暴露，不能把字母当魔法画质档；
- raw depth 必须做 inverted/orientation 诊断；
- D3D12 同设备零拷贝与明确资源状态比“有深度图”更重要；
- 合成 jitter 无法凭空创造源图不存在的信息，其 README 也承认此实验可能闪烁且不省性能。

其 motion provider、ReShade 链和第三方 bundle 各有许可证；Veyra 不直接集成该 add-on。

## 5. Veyra 的首发竞争策略

### 5.1 统一质量核心

每帧构造：

```text
FramePacket
  color       RGBA16F linear scene/display proxy
  sourcePTS   精确时间
  duration    帧时长
  sequence    单调帧号
  colorInfo   range/matrix/transfer/primaries
  flags       discontinuity/drop/seek/cut/resize

GuidanceFrame
  motion      RG16F current->previous，源像素单位
  depth       R32F 相对深度
  confidence  R8_UNORM
  reset       整条历史链的单一真源
  provenance  zero/nvof/dav2/offline-bidir
```

统一处理顺序：

```text
source -> color normalize -> scene/cadence analysis
       -> optional DLSS SR (V1 Zero Guidance)
       -> post-SR NVOF + confidence -> optional depth
       -> Feature 18
       -> optional DLSSG 2X
       -> post mix/clamp -> OSD/subtitles -> display or encode
```

这里的顺序是硬约束：`workingExtent` 由 SR 输出决定；SR bypass 时等于 source extent。NVOF、depth/confidence、Feature 18 与 FG 都使用同一个 working extent，禁止让 SR 反过来依赖尚未生成的 NVOF，形成循环图。

### 5.2 三套调度，而不是三套算法

- **NR Low Latency**：latest-frame mailbox=1；B 到达就处理 B；FG off；允许丢过期视频帧；检测 drop 后全链 reset；禁止 readback。
- **FG Low Latency**：保留 A/B，等 B 后生成 A½；需要一帧 lookahead window，但显示附加延迟必须实测，不能直接写死为 `1/f`。采集卡已有延迟不等于免费获得 B。
- **Buffered Quality**：保留 A/B/C；C 只用于验证 A/B guidance、depth 和 cut/trust，不冒充 DLSSG 的第三帧输入；相对 pair mode 通常再增加约一个源帧周期。
- **Player Balanced**：按 PTS 调度，不丢源帧；音频为主时钟；NVOF + 可选深度；seek 后 reset；字幕在 FG 后合成。
- **Export Quality**：不丢帧；允许文件级 lookahead 做 forward/back validation；depth 可逐帧或使用 Video Depth Anything Small；4K 首发用 D3D12 NVENC，只回收压缩 bitstream，不回读 raw pixels。

依据：NVIDIA 的 [NVOFA FRUC guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html)明确以 previous/next 两帧生成中间帧并做 forward/back validation；这证明“后一帧有用”，但不证明采集卡延迟能免费提供后一帧。D3D12 NVENC 的资源与 fence contract 以 [Video Codec SDK 13.1 guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html)为准。

### 5.3 首发画质档

- `NR Low Latency`：NVOF motion；depth 自动关闭或低频；NR 开；零主动 lookahead。
- `FG Low Latency`：A/B 配对、FG 2X、`lookaheadFrames=1`，显示延迟实测。
- `Buffered Quality`：A/B/C、NVOF + DAV2 interval 4 + confidence gate、`lookaheadFrames=2`。
- `Export Quality`：文件级 bidirectional validation + 稳定深度 + 无丢帧 + D3D12 NVENC 4K 输出。
- `Motion Only`、`Depth Only`、`Zero` 只保留为诊断和 A/B，不作为默认宣传档。

估计深度不应默认强开：动漫、二维 UI、快速切镜和 AI 生成的非刚体运动可能让 depth 比 zero 更坏。`Auto` 必须根据 depth residual、age 和 motion confidence 按像素/按帧降级。

## 6. “优于 Magpie”的合法证明方式

不能凭架构宣称胜出。首发 gate 至少固定五类同源片段：

1. 慢速镜头 + 细线；
2. 快速平移 + 遮挡/显露；
3. 烟、火、粒子和半透明；
4. UI/字幕/文字；
5. 切镜、重复帧和 PTS discontinuity。

每类比较 `Zero / Motion / Motion+Depth / Auto`，保存输入 hash、配置、输出、GPU timing、reset、flow/depth/confidence 可视化。指标至少包括 temporal warp residual、flicker、duplicate/generated-frame hash、A/V drift 和主观盲评。只有某一场景真实胜出时，才写“该场景优于 Magpie 当前默认”；禁止泛化成全面领先。

## 7. 许可证动作

- Magpie GPLv3：只作黑盒行为与独立实现参考；不复制源码。
- 无许可证仓库：不复制任何代码、shader、header 或资源。
- MIT 项目：只有明确复用时才复制，并保留 copyright/license notice；第三方二进制不因外层 MIT 自动变成 MIT。
- Depth Anything / Video Depth Anything：只允许 Small 且必须锁定具体 model hash 与许可证；Base/Large 的非商用限制不得混入拟商业分发。
- NVIDIA NVOF SDK、DLSS SDK、DLSSNR/DLSSG runtime：均放 `third_party_local`/`runtime_local`，默认不随 Veyra 分发。
