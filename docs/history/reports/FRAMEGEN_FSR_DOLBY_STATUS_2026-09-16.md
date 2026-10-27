# 帧生成 / FSR / 杜比：施工状态（2026-09-16）

给人工验收/继续施工用。**已合并到 main（merge `24e7de7`）；未推送、未发布。**

- 存档点（main）：`693db07`，tag `checkpoint/pre-framegen-fsr-dolby-2026-09-16`
- 施工分支：`codex/framegen-fsr-dolby-20260916`
- 分支提交（关键节点）：`35dd632`（DLSS 6X）→ `4124a9d`（XeSS MFG）→ `ef0fb50`（FSR 探针）→ `27656a6`（FSR 帧生成接入）→ `950f46d`（FSR 超分）→ `9179449`/`aa168ab`（40 系解锁）→ `4154733`/`3dd5add`（30 系解锁）→ `ef016ba`（A-2 节奏 hook）→ `56bb0f6`/`275c3f2`（杜比解码/直通）→ `015f8a4`（PS5+RGB24 修复）→ `d5f7383`。完整列表见 `git log`。
- delivery 短测：本分支多次 23/23 通过（早期 `89286afb…`/`c6b3294d…`/`9951994f…`，后续 id 见 `docs/WORKLOG.md`）；分支 tip 最近一次 `45aefe5dcbdc44f79ecba8167b83f1f0`，均在 `logs/delivery/` 下
- 计划：`docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md`
- 合并存档：前 `checkpoint/pre-merge-framegen-20260916`（main `87ad4cd`）、分支 tip `checkpoint/framegen-fsr-dolby-branch-tip-20260916`（`ad7442d`）、后 `checkpoint/merged-framegen-20260916`

## 一、已完成并实测（可直接验收）

### 1. DLSS 6X（50 系原生，B 工作流）

- 改动：生成帧池 3→5/parity（`kGeneratedPoolSlots=10`）、`FrameBatch` 4→6、`interpolate` 上限 6、present SRV 堆 8→12、DLSSG 后端 `multiFrameCount` 上限 3→5、`validate()` 1..6；新增**能力驱动倍率**：UI 按运行时的 `MultiFrameCountMax` 只给 关闭/2X/3X/4X/6X，超上限的请求被 `requestSettings` 明确拒绝并保留原值，后端失败时**按上限降级并提示**（不再把补帧整档关掉）。
- 实测（RTX 5070 / 616.56 / DLSSG 310.7.0.0）：
  - `--fg-multiplier 6`：`FG capability available=true multiFrameMax=5`、`fg=1`、**445 真帧 / 2215 生成帧**（每帧 5 张）、exit 0。
  - 模拟 40 系上限（`VEYRA_TEST_FG_MULTIFRAME_MAX=1`）：`requested multiplier 4 exceeds GPU capability maxGeneratedFrames=1; applying 2X with frame generation kept`，312 真帧 / 310 生成帧，exit 0。
- 测试：`veyra_ui_contract_tests` PASS；`veyra_repair_contract_tests` 146 项 0 失败（新增 5X/6X 合法、7X 拒绝）；`veyra_repair_preset_tests` 48 组 PASS；`scripts/gates/delivery.ps1` 23/23 PASS，48.9 秒（`logs/delivery/89286afbb28d4785925ae3772e0409c4`），EXE SHA256 `9B8EA46AECF7A95E99472B7A31D60F9B5B9D8008D7B6BE0966C261095FDD0263`。

### 2. XeSS MFG 解锁（A 工作流，2X/3X/4X）

- 改动：新增 `include/veyra/gfx/XessMfgUnlock.h` + `src/gfx/XessMfgUnlock.cpp`，移植 OptiScaler（GPL-3.0，`70676c5f`）的**五处字节补丁**（U1–U5）：只改进程内映射、不改磁盘；补丁前校验模块身份（大小 + SHA-256 + PE TimeDateStamp/SizeOfImage）与每一处原始字节；全部位置先验证再写入，写入后回读校验，失败即回滚；XeFG/XeLL 上下文销毁后**恢复原字节**。`XessPresenter` 现在加载 `xefgSwapChainGetProperties` / `xefgSwapChainSetNumInterpolatedFrames`，查询真实上限、设置 `init.maxInterpolatedFrames` 并调用 setter；UI 只在"审计版运行库存在或会话已上报 >2X"时给出 XeSS 3X/4X。
- DLL 审计（本机随包 `runtime_local/intel/experimental/libxess_fg.dll`）：1.3.1.78 / 22,957,432 字节 / `EC5E0C65…AB27`；TimeDateStamp `0x69CB0F4D`、SizeOfImage `0x015ED000`；**U1–U5 五处原始字节全部匹配**。
- 实测（RTX 5070）：`--fg-xess --fg-multiplier 4` → `maxInterpolatedFrames=3 unlockApplied=true => maxMultiplier=4`、`framesPresented=4`（每真实帧 4 帧）、**563 真帧 / 1677 生成帧**、exit 0，退出时 `unlock rolled back 5/5 patch(es)`。
- 归属：`THIRD_PARTY_NOTICES.md` 已按 GPLv3 义务登记 OptiScaler（含固定提交）与 Magpie fork 的结构参考。

### 3. 杜比位流能力探测（G-1）

- `CaptureCardSource` 现在会分类压缩音频 subtype（AC-3 / E-AC-3(DD+) / DD+ Atmos / TrueHD-MLP / DTS 系列）并输出每设备汇总行。
- 实测（参考采集卡 USB3 Digital Audio）：`device bitstream types=0 [none] pcmTypes=15` —— **这张卡在硬件层就不提供任何杜比/DTS 位流**，因此"支持 Atmos"在当前设备上不可达；位流直通工作必须先换支持位流的采集设备（或改走 HDMI 直通链路）。

### 4. 40 系 MFG 前置实验开关（C-1）

- `VEYRA_TEST_FG_FORCE_MULTIPLIER=1`（仅测试用，UI 不可达）：绕过"请求倍率超过上报上限"的拒绝，让 40 系机器可以直接请求 3X/4X，观察运行库是**真的生成帧**、**重复帧**还是**黑屏/失败**。
- 用法（在 4060 反馈机上）：

```powershell
$env:VEYRA_TEST_FG_FORCE_MULTIPLIER='1'
.\veyra.exe --fg-multiplier 4 --smoke-seconds 15 <一段本地视频>
# 看日志：requested MFG multiplier unsupported / test-only FG multiplier forced above capability
# 以及 [app] smoke frames=... generated=... 与画面是否真的更顺（重复帧=数字翻倍但运动不增加）
```

### 5. AMD FSR 帧生成接入（E 工作流，**已完成并实测**）

完整设计、API 顺序、失败模式与命令清单见 [AMD FSR 帧生成接入记录](FSR_FRAMEGEN_INTEGRATION_2026-09-16.md)。

- 新增 `FsrFgPresenter`：FidelityFX loader + 代理交换链上下文 + 帧生成上下文 +
  每帧 prepare/configure/插帧 dispatch + 提供方 present 回调（合成与真实/生成帧计数）。
- `PresentSink`/`VideoPresenter`/`EnhanceGraph`/`EngineController`/设置 UI/预设 schema v13 全部接入，
  命令行开关 `--fg-fsr`；导出与 XeSS 一样明确拒绝（交换链插帧无编码器输出合同）。
- **实测（RTX 5070 / 提供方 3.1.6+3.1.7）**：
  - 1080p：226 真实帧 / **222 生成帧**，exit 0（`logs/fsr/smoke-fsr-final.log`）；
  - 4K 渲染（render 3840×2160 → display 1280×712）：559 真实 / **555 生成**，exit 0（`logs/fsr/smoke-fsr-4k.log`）；
  - 设置事务重建后继续补帧：`using the retained AMD proxy swapchain`，exit 0（`logs/fsr/smoke-fsr-rebuild2.log`）；
  - 回归：DLSS 6X 875 生成/177 真实、XeSS 4X 639 生成/217 真实，均 exit 0；
  - `scripts/gates/delivery.ps1` PASS（`logs/delivery/4f387d93def9440f8fa9f7efe92d6bd3/result.json`）。
- **上限实测为 2X**：请求 2 或 3 张生成帧时，present 回调仍只收到每真实帧 1 张
  （`logs/fsr/probe-3x-20260916.log`、`probe-4x-20260916.log`）。引擎与预设据此把 FSR 倍率门限设为 2X，
  超过即拒绝而不是"标 4X 实际 2X"。
- **生成帧带真实运动（E-1 门槛）**：探针 `motion` 模式让白方块每帧移动 40 像素，回读插帧目标得到
  方块中心 1041.5，理论中点 1040.0（真实帧 1060、前一帧 1020）→ 不是重复帧也不是外推帧
   (`logs/fsr/probe-motion.log`)。同一探针的提交间隔均值 9.52ms、最大 15.57ms、<5ms 仅 3/47，
   没有成串突发（提供方提交节奏，非屏幕扫描实测）。
- **已知约束（已实测）**：提供方在 `ffxDestroyContext` 后不释放真实 DXGI 交换链，同一 HWND 无法再建交换链，
  因此代理交换链在窗口生命周期内保留（详情见接入记录第 4 节）。
- **未验证**：AMD 显卡实机、HDR10 输出、采集卡实时输入下的 FSR 帧生成；FSR 4.0.1 ML 提供方在本机未被枚举。

### 6. AMD FSR 探针（E/F 前置证据，提交 `ef0fb50`）

`tools/fsr_probe`（CMake 目标 `veyra_fsr_probe`）加载 AMD FSR SDK 2.3.0 的签名 loader DLL，在真实 D3D12 设备上枚举 provider 并创建帧生成代理交换链。本机 NVIDIA RTX 5070 实测结果：

- `framegen-swapchain` 3.1.7、`framegen` 3.1.6 被枚举到（带真实 device 的 version 变体同样）；
- **`CreateContext(framegen swapchain for hwnd) = OK`**（返回非空 context 与代理交换链），`DestroyContext = OK`；
- 4.0.1(ML) 在这些 desc 类型下**未被枚举**——与"4.x 面向 AMD 支持列表"的分档假设一致，需要在 AMD 卡上复测；
- 实现约束：loader 只在 **DLL 所在目录可被搜索**时才发现 provider（工作目录切到 `signedbin` 才成功；仅 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 会得到 `NO_PROVIDER`/`ERROR_UNKNOWN_DESCTYPE`）。播放器集成必须显式把 AMD DLL 目录加入加载搜索路径。

含义：**FSR 帧生成 3.1.x 后端在 NVIDIA 上可做，API 路径已用本机证据验证**；4.0.x ML 等 AMD 卡验证。详细身份与调用序列见 [AMD FSR SDK 2.3.0 本地落地记录](AMD_FSR_SDK_2.3.0_LOCAL.md)。

## 二、未完成（明早需要决策或继续施工）

| 项 | 状态 | 说明 / 下一步 |
| --- | --- | --- |
| XeSS 节奏 hook（A-2） | **已移植并实测（`9dd…`：见提交记录）** | present thunk `0x25C0` 接管 + 提供方调度器 `0x21EE30`，只在 >2X 安装、退出回滚。4K/30fps 素材 4X 实测：`refused=0`、同 burst 内间距 mean≈**8.303ms**（目标 33.33/4=8.33ms）、min≈8.14ms、max≈8.42ms，339 真实 / 1005 生成、exit 0；关掉 hook 的对照里 4X 完全没有调度发生。**未移植**：上游时间戳/截止时间层与墙钟回退。顺带修掉 XeSS 2X 静默退回原生呈现的 bug（现在 280 真实 / 276 生成） |
| 40 系 DLSS MFG 解锁（C-2 完整版） | **已实现（`9179449` 初版 + 2026-09-16 补齐）** | 两个 MIT 上游合并移植：`ImDreamt/MFGAdaUnlock-RenoDx` 的两处 `0x1b0` 架构比较改写、PTX 中点修正（104 处 0.5 替换 + fatbin 截断逼 JIT）、8 个 `dlfg_kernel` 槽位重定向；`dashdogy/RTX40MFG-Unlock` v1.3.3 的 count/index validator 补丁（v1.3.3 修复"卡 2x"的机制）+ 三段 SHA-256 身份链（source fatbin `5A8E0284…` → PTX `46C05996…` → **重建 fatbin `19FB3CD5…` 与上游逐位一致**）+ sm120/sm89 字段校验。重建 fatbin 哈希作为发布闸：不一致即拒绝并回滚。本机三个场景全过：原版 apply/readBack/restored=1/1/1；篡改 fatbin 1 字节 → 干净拒绝+完整回滚（同时修复了原版回滚路径写只读页的崩溃 bug）；篡改 gate 模式 → 预检拒绝。50 系 6X 路径不受影响（Ada 专属门控）。**行为未验证**：3X/4X/6X 是否真出运动帧、Ada 缺硬件 flip metering 是否冻结，需要 40 系实机 |
| 30 系原生 DLSS-G（A 路线） | **已实现（2026-09-16，`4154733` + `3dd5add`）** | 用户授权开工。实测解析 310.7 的 69 个 fatbin（25 注册表程序 + 38 网络 + 6 辅助）与 200 指针槽 + 44 lea 引用；全部重建为 sm_86 单条目 PTX（拒绝 Ada/Blackwell 专有构造）、temporal 中点修正、±1.25 GiB 可达分配、私有 CUDA context 预验证（本机 `preflight=69/69`）、200+44+2 全量发布与回滚；架构门 0x1b0→0x170。产品接入：FG 查询前自动应用（仅 RTX 30 窗口），provider 报不可用时按审计上限宽限。probe 全周期 `applied=1 readBack=1 restored=1`；delivery `548a606d…` PASS。**未验证**：30 系实机行为（额外 gate、质量、性能、稳定性），dashdogy 自述该路线 "very early and experimental" |
| 30 系 FSR 备选（B） | 保留 | Veyra 自带 AMD FidelityFX 帧生成在 30 系上可作为即时可用路径（AMD 官方 FSR3 FG 支持 RTX 30+）。**未验证**：30 系实机 |
| FSR 帧生成（E） | **已完成并实测（2X）** | 见上文 §一.5 与 [接入记录](FSR_FRAMEGEN_INTEGRATION_2026-09-16.md)；4.0.1 ML 需 AMD 卡复测 |
| FSR 超分（F） | **已接入并实测（3.1.x）** | 图内 SR 阶段新分支 + `videoSrQuality=5` 档位；播放器 205–226 次 dispatch 0 失败，AMD 光流形状 exit 0，同帧对照平均绝对差 0.31/255；**相对画质略软（梯度能量比 0.888）如实记录**；4.x ML 需 AMD 实机。见 [FSR 超分接入记录](../plans/FSR_UPSCALING_PLAN_2026-09-16.md) |
| 杜比直通 / 解码（G-2） | **解码兜底已实现并本地验证（`56bb0f6`）** | FFmpeg 解码 AC-3/E-AC-3(含 DD+ Atmos 载体)/DTS/DTS-HD，IEC 61937 解框；采集端在无 PCM 可用时按优先级走位流直通并解码进既有 5.1 管线。本地用真实 AC-3/E-AC-3 位流验证了 6 声道与幅度映射。**未验证**：真实位流采集卡协商（本机设备不提供位流）、TrueHD/DTS-HD 无真实素材 |

### A-2 节奏 hook：已完成的准备与勘察结论

本轮已落地 detour 基础设施并用单测验证：

- 新增 `include/veyra/ThunkHook.h` + `src/base/ThunkHook.cpp`：面向"五字节 `E9 rel32` + 0xCC 填充"的跳转 thunk 做函数级 hook。它会校验 thunk 形状（非 thunk 直接拒绝，不猜）、在目标 ±2GB 内分配一页放两个绝对跳转桩（trampoline 指回原目标、entry 指向替换函数）、打补丁后回读校验、移除时按原字节恢复。替换函数可在任意地址（不受 rel32 限制）。
- 单测（`veyra_repair_contract_tests`，现 154 项 0 失败）覆盖：thunk 到达原函数、安装 hook、替换函数执行并经 trampoline 转发、移除后恢复、非 thunk 目标被拒绝。

上游 `XeFGPacing.h`（OptiScaler 固定提交 `70676c5f`）的施工坐标已勘察完毕，供下一轮直接移植：

| 项 | 值 |
| --- | --- |
| Present thunk | RVA `0x25C0`（`jmp 0x21F730`，后接 11 字节 0xCC 填充，正好可被 `ThunkHook` 接管） |
| 原生 present 目标 | RVA `0x21F730` |
| 调度 thunk | RVA `0x3100`（→ `0x21EE30`） |
| 每帧间隔计算点 | RVA `0x220254`（`count=[r8+8]`、`div rcx`、结果存 `r12`） |
| burst 循环 / 最后一帧调用 | RVA `0x2202E8`（循环内）；`0x220462`（最后一帧，arg7=0，入口 `0x220467`） |
| 限流块触发条件 | RVA `0x220317`：`[rsi+0x340]!=0`、`[r8+8]>2`、`[r8+0x28]==0` |
| 上下文偏移 | `LimiterEnabled=0x340`、`SchedEnable=0x341`、`BurstGate=0xC0`、`Ring=0x168`、`BurstLimiterField=0x28` |
| ring 快照函数 | RVA `0x224CF0`（经 `0x4DA0` 调用；调用点 `0x22023D..0x22024B`） |
| 上游实现要点 | 需要重建 present 调用的参数（上游用 1500 字节 helper `0x3F570`）；全局时序变量需按上下文管理；hook 回调不得抛异常或做大分配/同步日志 |

结论（2026-09-16 晚更新）：hook 体已按上述坐标移植并实测（`ef016ba`），4X 同 burst 间距 mean 8.303ms、refused=0；结果见上文 §二 A-2 行与 `docs/WORKLOG.md`。

## 三、验收清单（建议顺序，2026-09-16 晚更新）

0. `git log --oneline -12` 确认工作已合并在 `main`（merge `24e7de7`，含本分支全部提交）；未推送、未发布。
   构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit 0；分支 tip 的 delivery 短测 PASS（`logs/delivery/45aefe5dcbdc44f79ecba8167b83f1f0/result.json`）。
   EXE 哈希会随 PE 时间戳漂移（无 `/Brepro`），不在此冻结；最终构建/门禁数字以 `docs/WORKLOG.md` 顶部条目为准。
   40 系解锁验收：在 40 系机器上跑 `veyra.exe --fg-multiplier 4 --smoke-seconds 15 <视频>`，
   期望 `[ada-mfg] ... unlock applied=1 gates=2 descriptors=8 kernel=1` 与 `multiFrameMax=5`；
   杜比验收：在支持位流的采集卡上跑一次采集，期望 `capture-audio-bitstream` 行
   （直通 `passthrough to receiver kind=...`，或解码回 PCM 并显示"位流解码为 N 声道"）。
1. **AMD FSR 补帧**：`out\build\audio-continuity-repair-20260915\veyra.exe --fg-fsr --smoke-seconds 10 <视频>`，
   期望日志出现 `[fsr-fg] provider=3.1.6` 与 `using the retained AMD proxy swapchain`，结尾
   `generated ≈ frames`（2X）且 exit 0；顺手看设置里「补帧方式」是否出现 `AMD FSR 帧生成 · 2X`。
   最近一次实跑：284 真实 / 280 生成（`logs/fsr/smoke-fsr-final2.log`）。
2. DLSS 6X：`out\build\audio-continuity-repair-20260915\veyra.exe --fg-multiplier 6 --smoke-seconds 10 loop\local\fixed_clips\test_av_1080p.mp4`，看日志 `maxMultiplier=6` 与 `generated ≈ 5×frames`；UI 里补帧下拉应出现 `6X · 五张中间帧`。
3. XeSS 4X：`--fg-xess --fg-multiplier 4 --smoke-seconds 10 <视频>`，看 `unlock applied`、`framesPresented=4`、`pacing installed`、退出时 `rolled back 5/5`；节奏 hook 已移植（`ef016ba`，§二）：4K/30fps 4X 实测同 burst 间距 mean 8.303ms、refused=0；观感是否均匀仍请人工判断。
4. 40 系：用 `final-40x6x` 包的 EXE 在 4060 上直接跑 `--fg-multiplier 4`（或 6），确认 `[ada-mfg]` unlock 行与真实出帧；不再需要 `VEYRA_TEST_FG_FORCE_MULTIPLIER`。30 系用 `final-30x86` 系列包，看 `[ampere-mfg]` 行与真实出帧。
5. 杜比：用 `--smoke-seconds 10 <capture2:...>` 跑一次，确认 `capture-audio-bitstream` 行；直通路径应出现 `passthrough to receiver kind=...`，回退路径应显示解码后的声道数。

## 四、边界声明

- 已合并 main（merge `24e7de7`，tag `checkpoint/merged-framegen-20260916`）；未推送远端；未发布。
- 磁盘上的 `libxess_fg.dll` 未修改（哈希与审计值一致）；所有运行库修改仅在进程内存，退出即恢复。
- 隔离分支上的 delivery 短测 23/23 通过，但**实卡 40 系、XeSS 节奏、FSR、杜比直通均未验证**，不作为已完成能力对外描述。
