# Veyra 2.1.2

2026-11-02 · Windows x64 · 从 2.1.1 升级

本版汇总 2.1.1 发布后 Claude 与 Codex 完成并合入 main 的功能和修复。下载 NVIDIA 或 AMD 中与实际处理显卡对应的一个完整免安装包，解压运行 `veyra_qml_ui.exe`。Windows 文件版本为 2.1.2.0。

## 超分辨率：两种新玩法

- **双重超分**：基础 RTX 视频超分 → 缩小第一次 AI 处理结果 → 第二次 RTX 视频超分。开关在基础超分下方，展开后独立调整第二次的缩小比例、目标尺寸与质量；列表和节点设置均可保存，默认关闭。
- **超分后缩回到**：单次 RTX 视频超分到更高分辨率，再按线性光精确面积采样缩回原尺寸或 2K／4K／5K／6K／7K。例如 1080p → 8K → 4K，后续 NR、补帧和输出按缩回后的尺寸处理。开启双重超分时暂停使用此项，并保留选择。
- 普通单次 8K 超分继续保留。新的双重与超采样处理图在创建资源前检查 WDDM 显存预算，不足时明确拒绝该组合，避免擅自降低用户选择的尺寸。画质取决于素材和参数，双重超分属于可选实验效果。

## 英伟达低延迟与 AMD NR

- **英伟达低延迟：关／开／开＋增强**，直接接入 NVAPI Reflex；原帧与生成帧分开标记，避免补帧路径重复 Sleep。保存用户选择，并兼容旧低排队配置；XeSS 使用提供方自己的调度。降低排队的实际收益随设备和设置变化。
- **新增 RX 7000 · RedDuke 实验 NR**：在 NR 版本处选择“RX 7000 / 9000 · AMD NR（实验）”，程序按显卡路由；性能优化开关对应 Small／Original，各层兼容 25%–100% 手动处理比例、预设与会话。通过独立 worker 隔离，与 RX 9000 的 lmxxf 和 NVIDIA 路径分开。
- AMD NR 依赖系统已安装的 HIP 7 驱动组件；包内不附带真实显卡驱动。RX 7000 接入完成了本机协议、设置与界面验证，实卡适配按实验功能提供。实时视频采用受预算限制的内部推理尺寸，再合成至输出尺寸。

## 补帧、去重与帧节奏

- 修正自动剔除重复帧对采集微小噪点过于敏感的问题：整画面比较允许极少量分量超出 ±3 级，并持续参考最近一张真正的新画面，保留缓慢变化的累计识别能力。
- 删除图内额外的 64×36 亮度粗判断，防止已计算好的生成帧被再次误丢弃。针对 30 帧内容补帧后阶段性只到约 45 FPS 的问题修正重复过滤路径。
- T911、4K60 NV12、PS5 30 帧内容的同一批 **5401 个样本／90 秒**：旧算法 5 次噪点误判，新算法为 0。实机短测中 86 张带噪重复帧均正确识别。这是该样本的结果，不是所有采集设备的性能保证。
- 平滑源时间戳与回调呈现锚点，维持低排队；60→30 采集保留重复画面的第一份，减少额外等待。界面名称统一为“自动剔除重复帧”。
- **VFG 自动调控**：列表／节点补帧质量区可开关。实时 2X 负载紧张时可为当前帧对选用下一档更快模型；已有关闭选择保留。模型预加载需足够显存，4K 下额外占用约 1.9 GB。
- **垂直同步抖动适应**：有效垂直同步路径、输出不高于 80 FPS 且具备平滑锚点时，生成帧容量等待随回调抖动调整，上限 3 ms。自动模式选中垂直同步路径时也适用。真实 60 Hz 回放注入 1.9 ms 抖动的短测：DLSS 生成帧提交缺口约 37.7 → 5.6 次／分，VFG 中档约 5.6 → 2.8 次／分；这是软件提交统计，不等同于物理显示掉帧或屏幕端到端延迟。

## 捕获、声音与日常操作

- **屏幕捕获帧率**：修正 Windows Graphics Capture 默认 16 ms 更新间隔与高刷桌面节奏冲突，改进捕获限速和 HDR 状态缓存。本机 60 FPS 源测试，旧约 53.55／53.72 FPS，修正后约 60.00／59.99 FPS（捕获上限 60／120）。
- **无补帧捕获延迟**：采集／屏幕捕获在 GPU 就绪后直接呈现，移除固定首帧锚点造成的额外扣留。
- **采集卡 Dolby／DTS 载波音频**：自动识别通过通用 16 位双声道 PCM 引脚传来的 IEC 61937 AC3／EAC3／DTS，解码并混成立体声，保留中置人声。普通 PCM 路径保持；需要采集卡原样透传载波。
- **F7 截图、F8 开始／停止录制**，游戏模式也可用，可在设置重绑。
- **极简音量**：修复快速拖动、0%／100% 端点难拖、窗口拖动抢占；输入数值后 Return／小键盘 Enter／点击空白提交，Esc 取消；编辑时保持控制条和焦点稳定。
- 桥接层按职责拆分，公共 QML 面板与历史状态处理收拢；开发目录、分支及文档归档统一入口，便于后续维护。

## 验证与组件

本次直接使用 Claude 最新去重修复构建的程序：产品提交 `358be8cfe82a1563cc05fdeca34f684a0e95b6fc`，主 EXE SHA256 `33d8e615666365ba84f1cfebbc6ffef4bb6c2dbaf08119bdad6648e7d9cefa44`。发布整理只更新说明、二维码和打包元数据，程序、QML、shader、运行库与模型保留已验字节。

该构建通过 8 组合同／设置检查、QtQuick **132 通过／0 失败／1 跳过**、DLSS／VFG／XeSS 功能短测以及 NVIDIA／AMD 两包解压冷启动。主要测试设备 RTX 5070；AMD 包在该设备上的基础冷启动不能代替 RX 7000／9000 实卡推理验收。提交 FPS 与生成帧统计不代表显示器扫描输出。运行库身份和实验标记见随包 manifest、许可证和 [对应源码说明](BUILD_2.1.2.md)。

## 升级并保留设置

1. 退出旧版和新版，在旧版“设置 → 关于 → 用户数据 → 打开目录”确认并备份数据位置。
2. 推荐把同显卡类型的 2.1.2 包完整解压到新目录，再把旧版的 **`runtime_local/user-data-2.0.0`** 复制到新版同位置；自定义数据目录以软件显示为准。
3. 自定义 LUT 另复制 **`runtime_local/luts`**。只迁移自己的数据，不要把整个旧 `runtime`／`runtime_local` 或旧 DLL 覆盖进新包。
4. 同显卡类型也可退出后覆盖解压，但覆盖不会删除旧版多余组件。为保持组件清单一致，本次优先使用“新目录＋迁移数据”；保留旧包方便回退。

下载：[2.1.2 完整运行包](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2) · [应用与 RX 7000 网络对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2-sources)。开箱运行只需要一个显卡包，不需要下载源码包。

---

## English — Changes since 2.1.1

- **True dual RTX Video SR:** first SR result → downsample → second SR. A switch beneath base SR expands independent second-pass controls. Off by default. Ordinary single-pass 8K remains available.
- **SR then resolve down:** render higher with RTX Video SR, then exact linear-light area-downsample to source/2K/4K/5K/6K/7K; subsequent NR/FG uses the resolved size. Paused while dual SR is active. New graphs check WDDM memory budget before allocation without silently changing requested sizes.
- **NVIDIA Low Latency:** Off / On / On + Boost, direct NVAPI Reflex with separate source/generated-frame markers and no duplicate Sleep. XeSS retains provider-owned scheduling. Settings persist and migrate.
- **Experimental RX 7000 NR:** isolated RedDuke worker, Small/Original model choice through the performance switch, per-layer 25–100% scale and saved sessions/presets. RX 9000 lmxxf and NVIDIA paths remain separate. Requires installed HIP 7 driver components; real display drivers are not bundled. RX 7000 hardware inference is offered as experimental.
- **Precise content dedup:** tolerate extremely sparse capture noise in full-frame comparison, retain the last accepted new frame as reference, and remove the coarse 64×36 discard that could throw away already-computed generated frames. The same 5401-sample / 90-second T911 4K60 NV12 PS5 sample changed from five false-new classifications to zero. 60→30 capture retains the first duplicate copy.
- **Frame pacing:** smoothed source/callback anchors while retaining low queue depth; optional VFG 2X automatic regulation can select the next faster model for a pair under load. Preloading needs free VRAM (about 1.9 GB extra at 4K). Effective VSync paths with smoothed anchors and output ≤80 FPS adapt generated-frame capacity waits to callback jitter, capped at 3 ms. A short 60 Hz replay with injected 1.9 ms jitter reduced software generated-frame submission gaps from 37.7 to 5.6/min for DLSS and 5.6 to 2.8/min for medium VFG; these are not physical scanout or end-to-end latency measurements.
- **Capture:** fix WGC's default 16 ms update cadence on high-refresh desktops (local test: ~53.55/53.72 → 60.00/59.99 FPS for a 60 FPS source at 60/120 caps), cache HDR state, and present ready no-FG capture frames without extra first-anchor holding.
- **Audio:** detect IEC 61937 AC3/EAC3/DTS on generic 16-bit stereo PCM capture pins, decode/downmix with centre dialogue retained when the card passes the carrier unchanged.
- **Controls:** rebindable F7 screenshot/F8 recording, including game mode; Cinema volume fast dragging/endpoints/focus repaired, Return/keypad Enter/click-away commit and Esc cancel. Shared QML/bridge and repository maintenance improvements.

**Build and validation:** executable PE 2.1.2.0, product commit `358be8c`, EXE SHA256 above. The tested binary/runtime/model/QML/shader bytes are preserved. Eight contract/settings groups, QtQuick 132 passed / 0 failed / 1 skipped, GPU functional checks and both clean-package cold starts passed. Most tests used RTX 5070; basic AMD-package playback there is not AMD inference certification. Experimental/community runtime identities and licences are included; no claim of official NVIDIA partnership or universal hardware validation.

**Upgrade:** close Veyra and back up the directory shown in Settings → About → User data. Prefer extracting the matching GPU package into a new folder; copy only `runtime_local/user-data-2.0.0` and your custom `runtime_local/luts` (or use the displayed custom data path). In-place overlay is possible after exit, but does not remove stale components; do not overwrite new DLLs with an entire old runtime folder. Keep the previous package for rollback. [Application and RX 7000 network source](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2-sources); unchanged dependency sources are linked in [BUILD_2.1.2.md](https://github.com/Likely7/Veyra-NRVideo/blob/v2.1.2/docs/BUILD_2.1.2.md).

## 支持与反馈 / Support & feedback

<p align="center">
  <a href="https://discord.gg/c9aREyMj8"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join Veyra on Discord" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://www.patreon.com/cw/Likely7" title="Patreon 主页 / Creator page"><img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/main/docs/images/support/patreon-home.svg" alt="Patreon 主页 / Creator page" height="36"></a>
  &nbsp;&nbsp;
  <a href="https://www.patreon.com/Likely7/posts/buy-me-coffee-175018110" title="请我喝杯咖啡 · US$5 / Coffee support"><img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/main/docs/images/support/patreon-coffee.svg" alt="☕ 请我喝杯咖啡 · US$5 / Coffee support" height="36"></a>
</p>

<p align="center">
  Patreon 主页查看作者动态；☕ 咖啡按钮直达 US$5 赞助页面。<br>
  Visit the creator page for updates, or open the US$5 coffee support page.
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/v1.4.0/docs/images/1.4.0/donate-wechat.jpg" alt="微信赞助" width="220">
  &nbsp;&nbsp;&nbsp;&nbsp;
  <img src="https://raw.githubusercontent.com/Likely7/Veyra-NRVideo/main/docs/images/community/group-8-20261102.png" alt="Veyra 交流群 8" width="220">
</p>

左：微信赞助（自愿，不影响功能）；右：交流群 8。群码按图片标注于 **2026-11-09 前**有效，过期请查看仓库更新。

Left: optional donation; right: community group 8. QR valid before 2026-11-09.

[Veyra 软件知识库 / Knowledge base](https://qcnv4i7tnd7l.feishu.cn/wiki/space/7688258025282358221?ccm_open_type=lark_wiki_spaceLink&open_tab_from=wiki_home)：四个群聊汇总的使用文档，涵盖软件与采集卡常见问题。
