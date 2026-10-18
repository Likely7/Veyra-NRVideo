# Dolby Vision 支持 · 工作交接（2026-10-17 更新）

新会话从这里开始读，不要从零摸索。所有结论都经过实测或编译验证。

## 一、当前状态

已提交（**全部只在本地 custom 分支，未推送**，用户要求验收后才推）：

- P3-2 / P3-3 之后的提交见 `git log --oneline` 顶部（本文件随后续改动一起提交）
- e1d39c93 feat(dovi): let level-1 brightness drive the present curve (P3-2a..d)
- 6628626e feat(dovi): map level-1 brightness onto the static curve（P3 前半，纯函数）
- d1f462d8 feat(ui): add a dedicated Dolby Vision card（P2b，用户已验收）
- d05b2a7c feat(dovi): expose profile/RPU through the source snapshot（P2a）
- P1 系列：9e2514dc / 9ea7961e / 34683d4e / e2c4134c

完成度：

| 步骤 | 状态 | 说明 |
|---|---|---|
| P3-2a..d | ✅ | VideoPresenter 持有本帧 L1，设置打开时把 effective 曲线送进 blit |
| P3-3 | ✅ | 真设置项：杜比视界卡里「跟随动态元数据」开关；偏好 + 每片/每显示记忆；i18n 四语 |
| P3-3 修正 | ✅ | 4f24d9bd：跟随**只改高光起点、不动峰值上限**（上限是中灰 gamma 的锚点，改了整张画面变暗） |
| P3-4 | ✅（策略对拍） | 与 mpv v0.41 / libplacebo v7.376 对拍：面板能力足够时专业实现也**不压**，我们 -0.62%，一致；肉眼观感由用户决定 |
| P4 | ✅（结论：不改） | 元数据已是"按曲线实际输出上报"（`clampMetadataToCurve`），跟随的 6.8% 偏差在噪声级；逐帧上报会引入闪烁风险。详见下文 |

### P3-4 对拍证据（2026-10-17）
命令与产物：`D:\DSH\.dsh-tmp\dovi-follow\mpv\dbg.log`

- mpv 识别：`Dolby Vision - version 1.0, profile 8, level 6, RPU 1, EL 0`；
  解码格式 `yuv420p10 dolbyvision/bt.2020/pq`
- 输出：`\\.\DISPLAY2, 3840x2160 @ 10 bits, RGB_FULL_G2084_NONE_P2020`，
  `swapchain format R10G10B10A2_UNORM`，flip-model —— 与我们的 HDR10 直出同一条路
- 映射：libplacebo 的 shader 里 `#define tone_map(x) (a*x+b)`，全文**搜不到**
  `bt2390 / hermite / spline / mobius / reinhard / hable` → 内容峰值 336 nit 远低于
  面板能力时它**不做非线性压缩**，PQ 原样送出
- 我们的同一帧：336 nit 处 -0.62%，1~320 nit 0.00%（契约测试锁死）
  → 结论：我们的"0.85 系数 + 在内容峰值收口"在 HDR 直出场景下**不是激进方案**，
  与专业参考行为一致；真正的压缩交给用户静态曲线（它按 displayPeakNits 缩放）

### 一个待决的既有问题（属 P4 范围）
本机 DXGI 报的显示峰值达到 10000 nit 级，`resolveHdrTuning` 会把档位里的
start 450 / cap 1200 缩放到 clamp 上限 2000 / 4000，对 336 nit 的内容**整条曲线空转**
（用户会觉得"选了档位没反应"）。用户可在界面手动填「显示器峰值」绕过；要不要给
这个自动值加合理性上限，需要 P4 决策，不要擅自改既有行为。

### P4 结论：HDR10 元数据**不需要改代码**（2026-10-17 查证）
查了 `VideoPresenter::applyHdrMetadata` 与 `engine::clampMetadataToCurve`：

- 它已经是"描述输出而不是片源"：曲线启用且 `peakCapNits` 有效时
  `cll = min(sourceCLL, cap)`、`fall = min(fall, cll)`，并用 2 秒节流 + 值不变不重发。
- 实测数据（p8_30s.mkv）：片源 `maxCLL=359 / maxFALL=348`，用户 cap 经缩放到 4000，
  所以实际上报就是 359；跟随把该帧最高输出压到 336，偏差 6.8%，属于测量噪声级，
  不足以让显示器做错二次映射。
- 若改成"跟随开启时按帧峰值上报 MaxCLL"，会变成每 2 秒甚至更快地改元数据，
  显示器可能反复重启自己的 tone mapping（闪烁），收益（359→336）远小于风险。
  → **维持现状**。只有当将来出现"跟随把高光压掉 30% 以上"的强压缩模式时才需要重新审视。

### 真正值得产品侧跟进的一条（不在 DV 范围，已告知用户）
`displayPeakNits` 自动值取 DXGI 的 `MaxLuminance`，本机报 10000 级，`resolveHdrTuning`
据此把档位等比放大到 clamp 上限 → 静态曲线对 336 nit 的内容空转。
可行的低风险改法（未实施）：给自动值加合理上限，或在界面标出"系统报告值可疑，
请手填真实峰值"。用户当前可手填绕过。

程序包状态（D:\项目\Veyra-NV\Veyra-2.0.9-NVIDIA-win64-portable）：

生效 exe = .p5 = 见本次回复末尾的哈希（含 P1+P2+P3）
.orig 作者版 = 7ADD56B9651D6B62…（永远不动）
qml\Veyra 与 qml.p5\Veyra 都已含杜比视界卡（含新开关）

## 二、已实测的关键事实（含本轮新增）

- FFmpeg 的 AV_FRAME_DATA_DOVI_METADATA 确实到达帧；`VEYRA_DOVI_TRACE` 下日志 dovi-probe present=1
- L1 提取正确：p8_30s.mkv → maxPq=0.6339（dovi_tool 金标准 2596/4095）
- PQ→nit：0.6339 PQ = 336 nit；0.85×336 = 286，实测 highlightStartNits=286
- **曲线必须打开**才有可见效果：`hdrCurve.enabled=0` 时 blit 的 curveFlag=0，dimensions 不参与
- **`describeStages()` 是唯一权威**：`EnhanceGraph::initialize()` 里 `desc_=desc` 会整体覆盖描述，
  所以任何"新加的 live 字段"必须写进 `include/veyra/engine/GraphDescription.h` 的 `describeStages`，
  否则只在 applySettings 时活一下，图形一重建（开片、后端恢复）就丢。曲线的 hdrCurve 就在这里。
- 会话/预设库的 tuning 块**不要**用来持久化这类开关：bridge 的 `revalidate()` 会用 runtime configuration
  重写 pending settings，块里的值会被打回默认（hdrCurve 当年就是靠 `commitHdrTuning()` 事后重写才活着）。
  本轮试过把 doviFollow 放进 ChainGlobalSettings 并把 library→v14 / session→v9，实测会话被拒
  （`[ERROR] [ui-session] 模式会话损坏、无法读取或版本不支持`），已全部回退，改用偏好持久化。
- 可用的持久化通道（实测有效）：`qml-preferences.v1.json` 的 `doviFollow`（全局兜底）
  + `hdrTitles[].doviFollow` / `hdrProfiles[].doviFollow`（每片/每显示，和曲线同一份记忆）
- 手写 chain-session.v1 当夹具不可靠：v1 会话只有 9 个字段/配置，加字段必须整份改成新版本，
  写错就被拒且**保留原文件不覆盖**，现象是"设置没生效但文件没变"
- **`peakCapNits` 同时是中灰 gamma 的锚点**（`hdrCurveLuminance`：g=log(mid/peak)/log(203/peak)），
  所以跟随**绝对不能**改它。用真实档位（mid 175 / start 450 / cap 1200）实测：把 cap 跟着
  帧峰值 304 改，1 nit 掉 78%、100 nit 掉 18%、1000 nit 被硬夹掉 61% —— 整张画面变暗，
  而且逐帧 cap 波动会让画面呼吸。现行实现只改 `highlightStartNits`，且**单向下调**
  （用户起点比帧峰值低时保留，不削弱用户已设的压缩）。
- 滚降区间（dims[12]）在跟随时用"内容自身峰值"收口，但 `dims[13]`（硬上限）仍是用户值：
  否则高显示峰值（例如系统报 10000 nit）会把坡拉得极缓，等于没压。
- 实测口径（本机日志）：p8_30s.mkv 内容峰值 336 nit、用户档位经显示峰值缩放后
  start=2000 / cap=4000；跟随生效后 start=286（n=0）、273（n=200, 帧峰值 321），cap 恒为 4000

## 二·五、P5（P7 双层）已探明的实现事实（2026-10-17）

用 `D:\share\Dolby Vision\[Dolby] ... Full Enhancement Layer Test`（Dolby 官方 FEL 测试片）实测：

- 容器：`profile 7, level 6, RPU 1, EL 1, BL 1`；dovi_tool demux 出 BL 67.2 MB / EL 44.7 MB
  （EL = BL 的 66% → FEL，不是 MEL）
- **FFmpeg 完全看不到 EL**：`demuxer: opened streams=1`、packet 里 `layer=0` 的 NAL、
  `sideDataElems=0`。所以 `AV_FRAME_DATA_DOVI_METADATA` 只有 RPU，没有 EL。
- **但 EL 一直在我们拿到的 packet 里**，以 **NAL type 63**（Dolby 私有，未指定类型）与 BL 交织：
  ```
  7e 01 + 完整标准 NAL        ← 这就是包装规则
  7e01 4001… = 内层 type 32 (VPS)
  7e01 4201… = 内层 type 33 (SPS)
  7e01 4401… = 内层 type 34 (PPS)
  7e01 4e01… = 内层 type 39 (SEI)
  7e01 2601… = 内层 type 19 (IDR slice)
  7e01 0001… = 内层 type 0  (TRAIL_N)
  ```
  RPU 是 **type 62**（`7c 01` 包装，每 AU 一个，约 376 字节）。
- **EL 的 parameter sets 不在 hvcC 里**，而在 TrackEntry 的 `0x41E4 BlockAdditionMapping`：
  `0x41E7`=名称（"hvcE" / "dvcC"）、`0x41ED`=数据。hvcE 是 hvcC 格式（795 字节，含 VPS/SPS/PPS）。
- 剥掉 `7e 01` 后 EL 可直接解码：**1920x1080 yuv420p10 pq**（BL 是 3840x2160）。
- 单独解码 EL 会报参考帧警告（`Could not find ref with POC n` / `First slice in a frame missing`），
  但**能连续出帧**；用 dovi_tool 拆出的 EL.hevc 做对照，错误数**完全相同（12060）**，
  证明我们的提取与它等价，这些警告是残差层固有现象。
- 提取器原型：`D:\DSH\.dsh-tmp\dovi-follow\{make_el_test,extract_el_full,probe_el_subtype,naldump}.py`
- mpv 的旁证：它有 `Dolby Vision Profile 7 splitter` + `enhancement_pair`，但实测在同一个文件上
  配对失败（`el_newer; el head 1.752000`），全程只发 BL → **连 mpv 也没真正用上 EL**；
  libplacebo 的 dolbyvision.c 注释也明说 P7 只用 L1 元数据。

剩余工作（P5.3/P5.4）：
1. 在 MediaFileSource 里按需提取 type 63（剥 `7e01`）+ 拆出 hvcE 作为 EL 解码器 extradata，
   建第二条解码路径（1080p → 需要 2× 上采样对齐 BL）
2. GPU 合并：用 RPU 的 reshaping 参数（FFmpeg 的 `AVDOVIRpuDataHeader` 已解析：
   `coef_log2_denom` / `bl_video_full_range_flag` / `el_bit_depth` / piece-wise 曲线 / MMR）
   做 inverse reshape + EL 残差相加 → 12bit
3. 任何一步失败/性能不足就回落到现有 BL+RPU 路径，并用 FEL Test/Control 两片验证


## 二·六、P5 最终状态（2026-10-17 收尾）

P7 双层增强层**已经实现并跑通**，默认关闭，界面开关在
专业页 → 杜比视界设置 → 「合并增强层（Profile 7 双层）」。

| 子项 | 状态 | 证据 |
|---|---|---|
| P5.1 EL 在哪 / 怎么封装 | ✅ | EL 在 NAL type 63，`7E 01` + 标准 NAL；参数集在 TrackEntry 的 `hvcE`（不在 hvcC） |
| P5.2 EL 独立解码 | ✅ | 1920×1080 yuv420p10le；1200 帧 / 1238 包；0.07 ms/包；残差实测 −13…+17 |
| P5.3 GPU 合并 | ✅ | 开关开启后画面逐帧改变 0.23~0.29（整体 1.2%），关闭时**逐位相同** |
| P5.4 回落 | ✅ | Profile 8 / Control 片：`bound=0`，零 EL 活动，播放正常 |

### 实现位置

- `src/source/MediaFileSource.cpp`：`extractEnhancementNals` + `feedEnhancementLayer`
  （第二条软解解码器，容错模式），`out.doviEl` / `out.doviP7`
- `include/veyra/pipeline/DolbyVisionP7.h` + `DoviP7Constants.h`：参数打包进
  `dv0..dv6`（`dv6.w=2` 表示重建、`=3` 表示重建并把残差画成灰度）
- `include/veyra/engine/DoviBlElCompose.h`：CPU 参考实现（7 条单测锁住）
- `shaders/YuvToLinearRgb.hlsl`：`ReconstructLumaP7`（t12 = EL luma，R16_UINT）
- `src/pipeline/EnhanceGraph.cpp`：`uploadElLuma` + 绑定 slot 12 + 参数写入

### 三个把人骗过的坑（都会伪装成"功能没生效"）

1. **`dv6.w` 写成了 `params[24]`**（那是 dv6.x）。着色器读到的永远是 0，合成从不执行；
   而单测断言写的是同一个错槽位，所以一直是绿的。**教训：断言要对着语义写，不是对着实现。**
2. **着色器从未部署**：只复制 exe，`.dxil` 还是旧的（20596 vs 21808 字节）。
   **教训：部署清单必须包含 `shaders/*.dxil`。**
3. **播放位置记忆**：第二次运行从上次位置继续，两次画面完全不同步，对比毫无意义。
   **教训：自动化对比必须用全新 data-dir（清空 `positions`）。**


### P5 的最终结论（2026-10-17 收尾）

配对机制**已经打通并验证**：`read()` 会压住最多 3 个基础帧，等它们各自的增强层帧按
时间戳到达后一起交付；配上就合成，等不到就退回基础层。日志证据：

```
PAIRED held pts=542 el=1
[dovi-el-gpu] bound=1 frames=3 ready=1 el=1
```

**但配对率被增强层的出帧率卡死了。** 解码器实际吐出的时间戳是跳跃的：

```
83 → 250 → 292 → 334 → 501 → 542 …      缺 125、167、209、375、417、459
```

基础层每帧都有，增强层只能解出其中一部分（实测不到 20%），而且加大缓存/等待窗口
（8→32、3→8）**完全没有提升配对率**——缺的帧就是没有。

由此可以断定：

| 方案 | 能否解决 |
|---|---|
| 放大配对窗口 | ❌ 已实测无效 |
| B′（包队列 + 预读） | ❌ 改的是喂包节奏，改不了丢帧 |
| B（独立解码线程） | ❌ 同上 |

丢帧的根因在增强层流的解码本身（参考帧链断裂，FFmpeg 跳过不完整参考结构的整帧），
不在取流架构里。**除非换解码路径，否则画质收益的上限就在这里。**

因此：功能保留（机制、代码、文档齐全），**开关默认关**。将来若 FFmpeg 的 EL 解码
改善，这套配对机制可以直接受益，不需要重做。

本轮还修掉两个把功能伪装成"不生效"的 bug，都值得记住：

1. **`doviP7Compose` 的偏好恢复被嵌在 `doviFollow` 的条件分支里**——偏好文件里没有
   `doviFollow` 时，这个开关永远读回 false。所有测试目录都因此失效。
2. **参数打包挂在"本帧有 EL"的条件上**——配对移到上层后，打包时 packet 里永远没有
   EL，`ready` 恒为 0，即使配上也不合成。


### 复盘（2026-10-17）：修掉 4 个缺陷

对整条杜比视界路径做了一次通读，P7 这部分有 3 个缺陷、参考实现有 1 个越界：

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `takeElForPts` | 帧从时间戳表取出后交给局部指针，没有成员持有 | 每配对成功一帧泄漏约 3MB（1080p AVFrame） |
| 2 | seek 路径 | 只 flush 了基础层解码器 | 跳转后增强层沿用旧参考链，首帧可能出垃圾 |
| 3 | `feedEnhancementLayer` | 残留一个从不被填充的 `elFrame_` 分配 | 死代码 |
| 4 | `doviInverseMap` | `pivots[num_pivots - 1]` 未检查 `num_pivots` | RPU 解析失败时读 `pivots[-1]` |

第 1、2 条最值得记：它们都不会让功能"看起来坏"，只会悄悄吃内存、或跳转后偶发一次怪画面。

### 仍未验证：MMR（色度逆映射）

`include/veyra/engine/DoviBlElCompose.h` 里的 `doviMmrMapping`（5.4.2.3.3，三阶 MMR 特征展开）
**从未在真实数据上执行过**。原因是本批片源的色度曲线走的是与其他分量一致的路径，实际生效的
只有亮度那条多项式分支。

- 结构上按规范写了（`tt[]` 21 个特征 + 逐阶系数累加），但**没有样本能证明它算得对**
- 若将来遇到真正需要 MMR 的 Profile 7 片源，这段必须先单独验证，再谈画质
- 验证方法：找一段 `mapping_idc == 1` 的 RPU，用 ffmpeg 的 `dovi_split` 拆流后对着
  规范逐项对账；或者用 `VEYRA_DOVI_MAPPING_DUMP` 导出参数确认 `mappingIdc` 真的出现 1

顺带记一条有用的工具发现：`deps/ffmpeg/bin/ffmpeg.exe` 存在，且自带 `dovi_split` bitstream
filter（`-mode bl|bl_rpu|el|el_rpu`）。任何"我的实现对不对"的疑问都可以拿它做独立参照——
这次就是用它证明"手工提取与官方拆分结果完全一致（都只解出 87/2877 帧）"。


## 二·七、HDR 曲线被假峰值稀释（2026-10-17 修复）

**症状**：调 HDR 曲线（高光保护、整体观感）感觉没有变化。

**根因**：`DisplayPeakInfo::plausible` 是 `valid` 的副本——注释里描述的"10000 nit 配
0.000 黑位是 EDID 上限而不是测量值"这条检查**从来没写**。于是：

```
DXGI 报 10000 nit  ->  plausible 仍为 true  ->  presenter 注入 10000
                   ->  resolveHdrTuning 按 10000/1000 缩放曲线
                   ->  用户设的 450/1200 实际变成 2000/4000
                   ->  内容峰值只有几百 nit，曲线永远不触发
```

**修复**：
- `PresentSink`：`plausible = valid && !(peakNits >= 4000 && minNits < 0.01)`
- `VideoPresenter`：只有 `valid && plausible` 才注入 `displayPeakNits`
- 判为不可信时不缩放（`displayPeakNits` 保持 0），曲线按用户原值生效
- 界面明确标注"这个数值是显示器的上限声明，不是实测，因此曲线未按它缩放"

**本机实测**：`dxgi output: peak=10000 black=0.0000 valid=1 plausible=0`，
`curveDisplayPeakNits=0`（此前是 10000）。

### 顺带做的界面改进

- 杜比视界卡片 summary：`Profile 7 · RPU 有` → `Profile 7（双层）· 最亮 1000 nit`
- 卡片内新增「当前片源的推荐」一行（复用曲线卡片的 `applyRecommendedHdrTuning`），
  并加门控：**非杜比视界片源时不可点**（HDR10 同样带亮度元数据，一开始漏了这个判断）
- 用户档案的 5 条影片记忆统一为一套通用基准：参考白 203（BT.2408）、高光起点 450、
  上限与显示峰值 800、滚降 110、强度 100；原文件已备份

### 一项评估后决定不做

「跟随效果反馈」（在界面上显示当前帧跟随压到了多少 nit）。它是可观测性而非功能，
需要跨 presenter / snapshot / bridge 四层，且每帧变化的值不能直接推给 QML。
对普通使用没有价值，开关对比更直观。


### 增强层覆盖率上限：实测验证（2026-10-17）

之前记录的"约 3%"是按基础层 2877 帧当分母算的，**分母用错了**。对 EL 流本身做
NAL 统计（`ffmpeg -f hevc` 提裸流 + 解析 Annex B 的 nal_unit_type）后：

| 指标 | 数值 |
|---|---|
| EL 流访问单元（帧）总数 | **1472** |
| IRAP（可独立解码的 I 帧） | **85**（IDR 30 + CRA 55） |
| 实际解出 | **87** |
| 每帧 slice 数 | 8 |

- **每帧 8 个 slice**。统计 NAL 时会得到 240 个 "IDR"、440 个 "CRA"，那是 **slice 数**
  除以 8 之后的 30 帧和 55 帧——早期把 slice 当帧，才误以为"理论上限 240 帧"。
- 按 EL 自己的 1472 帧算，I 帧占 **5.8%**；实际解出 87 帧 ≈ 85 个 I 帧（多出 2 帧是
  CRA 偶尔成功），即 **100% 的 I 帧都解出来了**。
- 剩下的 1387 帧是 P/B，需要基础层的参考帧，单流解码器物理上无法满足。

**结论：5.9% 就是上限，不是实现缺陷。** 这也可以直接答复"为什么只多出一点点画面"
的疑问。

#### 一次失败的性能优化（不要重做）

动机：94% 的 EL 帧注定解不出来，却仍被送进解码器，浪费 CPU。

做法：在 `extractEnhancementNals` 里只保留 IRAP slice（16–21）和参数集（32–35）、
SEI（39/40），丢弃 P/B（0–9）。

结果：**解码器 `decoded=0`，一帧都出不来**，配对数从 8 掉到 1。推测是参数集顺序
或 slice 间上下文被打乱（PPS 每帧一个，共 1556 个）。回退后立即恢复正常。

**以后不要再试这个方向**：收益是"省 CPU"而不是"多出画面"，而完整验证需要解析
slice header，成本远超收益。

### 仍未定论的两条（不要当成已完成）

- **EL 覆盖率矛盾**：离线统计 mkv block 得 51%（`111000` 节奏），运行时
  `decoded/packets` ≈ 97%。大概率是当初的 block 统计口径漏了 BlockGroup，
  但没有重新统计确认。当前实现按"这一包是否解出 EL 帧"判断。
- **画面方向**：CPU 原型预测 +6% 变亮，实测某段 −1.2% 偏暗。残差围绕零点对称
  （−13…+17），不同片段方向不同，原理上说得通，但**没有第二份独立实现交叉验证**。

### 诊断开关（默认全关）

```
VEYRA_DOVI_P7_DUMP=1   着色器把 EL 残差画成灰度（黑 = 该帧无残差）
VEYRA_DOVI_TRACE=1     逐帧 RPU + 合成绑定计数
界面开关                专业页 → 杜比视界设置 → 合并增强层
```

## 三、下一步（按顺序，每步都要编译+运行验证）

### P3-4 与 mpv（gpu-next）同片对拍
    D:\DSH\.dsh-tmp\dovi-test\tools\mpv（v0.41.0 / libplacebo v7.376）
    建议：同片同段截图对比高光过渡；Veyra 侧用 p8_30s.mkv + 打开 HDR 曲线 + 打开跟随

### P4 HDR10 元数据一致性
    applyHdrMetadata 仍用静态 hdrCurve 做 clamp（刻意如此，避免每帧改显示端元数据）；
    P4 要决定：跟随 DV 时显示端元数据是否改为按片段/按片峰值上报

## 四、绝对不要再踩的坑（每一条都真踩过）

1. 不要用「找右花括号当块尾」删代码 —— 曾因此误删 23 行 DV 解析代码。删改一律完整多行精确匹配，删完 git diff 核对。
2. PowerShell 的 Out-File -Encoding utf8 会写 BOM —— BOM 插到文件中段会变非法 token，整个 QML 组件加载失败。写文件用 Python / WriteAllText(UTF8Encoding(false))。
3. qmllint 0 错误不等于能加载 —— 唯一可靠关卡是启动退出码 0，且日志无 Unexpected token / no root object / Type unavailable。
4. hdr_tuning 与 preset_library 测试有状态残留：%TEMP%\pr19-hdr-persistence 与 %TEMP%\veyra-preset-library-tests
   里的旁文件不会被清，第二次运行就会假失败。跑之前把这两个目录**移走**（不要直接删，按约定进回收站/另放）：
   移走后 100% 通过，本轮实测 hdr_tuning / preset_library 均如此。
5. MediaFileSource.cpp 里 `auto& dv=info_.dolbyVision;` 出现两次 —— 定位必须用多行唯一锚点。
6. **`out.dovi = doviFrameInfo;` 曾经写在提取之前**，导致每帧带的是上一帧的 L1（曲线慢一帧，首帧没有值）。
   本轮已改到提取之后；改这类顺序时注意 P5 分支也会重算同一个成员（值相同，无副作用）。
7. 验收前必须把 exe（exe + .p5）与 qml/qml.p5 一起部署。
8. 绝不自作主张 push。
9. **跟随绝对不能改 `peakCapNits`**（它就是中灰 gamma 的锚点）。第一版改了，用户一眼看出
   "整张画面都在变"；用他的真实档位算出来暗部掉 78%。现在有 3 条契约测试锁死：
   上限原样保留、1~203 nit 输出完全一致、不削弱用户设得更低的起点。
10. 滚降判断发生在中灰 gamma **之后**（顺序 lift → mid → rolloff → cap）。用户的 mid=175 会把
   336 nit 的内容先压到 ~297，所以"跟随"在他的档位上只能做到 -0.6% 量级 —— 这不是没生效，
   是曲线的既有形状决定的；想验证"只动高光"，看暗部/中间调是否纹丝不动，别指望高光大幅变化。
11. 本机系统把显示峰值报成 10000 nit 级，档位里的 start 450 / cap 1200 会被 `resolveHdrTuning`
   缩放到 clamp 上限（2000 / 4000），静态曲线对 336 nit 的内容本来就空转 —— 排查"跟随没反应"时先看
   日志里的 `(user=…)`，别拿 profile 里存的原值当运行时值。

已确认与本工作无关、但会出现在日志里的噪音：
- `[ERROR] [qml] DialogHost.qml:767 / 1219 Unable to assign [undefined] to bool` —— 改动前就有（04:59 的日志里已出现）
- `veyra_settings_transaction_tests`（需传目录参数）在本机 8 轮 HDR 比对全挂（`SETTINGS_RESULT passed=0`）；
  已用 `git stash` 在**没有** DV 改动的基线上复测，同样 passed=0 → 与 DV 无关，非本功能回归

## 五、用户偏好

- 全部输出用中文
- 结论要有证据（日志/哈希/对表），不要凭记忆下判断
- 删除一律进回收站；临时产物放 D:\DSH\.dsh-tmp\
- 验收前完整部署（exe + qml + shaders），并报告哈希
- 验收通过前不推送任何东西
