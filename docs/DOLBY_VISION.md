# Dolby Vision 支持 · 完整技术档案

> ⚠️ **本文的「当前状态」已过期，但技术内容仍然有效**
>
> - 第一节是 **2026-10-17 的快照**，那时写着「未推送」——**现在已提为 PR #13 / #56**
> - 想查杜比视界的**最新状态、PR 是否合并**：看 `MY_WORK.md` 第一、二节
> - 本文保留的是**为什么这么做、技术上怎么验证**，这部分不会过期

**合并自**：`DOVI_WORK_HANDOFF.md`(370 行) + `DOVI_P5_VERIFICATION.md`(230 行) + `DOLBY_BL_EL_COMPOSITING.md`(250 行)
**整理日期**：2026-10-25
**说明**：三份文档内容按主题重组，**技术细节一字未删**。

---

> **文档地图**（我自己的 5 份文档，作者原版 600+ 份在 `docs/` 不动）
>
> | 文件 | 一句话定位 | 什么时候看 |
> |---|---|---|
> | **MY_WORK.md** | 记录 + 20 条行动规则 | 想提 PR、想知道做过什么、查当前状态 |
> | **HANDOFF.md** | 环境、构建、部署、验证 | 要动手编译或部署 |
> | **docs/DOLBY_VISION.md** | 杜比视界 P5/P7/L1 技术档案 | 改杜比视界相关代码 |
> | **docs/BILIBILI_MODULE_HISTORY.md** | B 站模块开发史 + 未解决问题 | B 站功能出问题时 |
> | **docs/HISTORY.md** | 13 个专题的技术排查链 | 遇到具体技术障碍（AMD 光流、采集卡…）|

## 目录

| 想查什么 | 看哪节 |
|---|---|
| 技术事实与已探明的实现细节 | 一、工作交接（**状态已过期，技术有效**）|
| P5 为什么显示绿色、怎么修的 | 二、P5 验证 |
| P7 增强层的数学算法 | 三、P7 合成算法 |
| 相关源码在哪 | 四、代码位置索引 |

---


## 一、工作交接与当前状态（原 DOVI_WORK_HANDOFF.md）

> ⚠️ **本节写于 2026-10-17，其中的「当前状态」已经过期。**
>
> - 那时写「全部只在本地 custom 分支，未推送」——**现在已提为 PR #13 / #56**
> - 那时列出的提交哈希（`e1d39c93` 等）是历史节点，最新状态看 `git log`
>
> **想查杜比视界的最新状态、相关 PR、哪些还开着**：去看 `MY_WORK.md` 第一、二节。
> 本节保留的是**为什么这么做、技术上怎么验证的**，这部分不会过期。

新会话从这里开始读，不要从零摸索。所有结论都经过实测或编译验证。

### 1. 当前状态（2026-10-17 快照，已过期）

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

#### P3-4 对拍证据（2026-10-17）
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

#### 一个待决的既有问题（属 P4 范围）
本机 DXGI 报的显示峰值达到 10000 nit 级，`resolveHdrTuning` 会把档位里的
start 450 / cap 1200 缩放到 clamp 上限 2000 / 4000，对 336 nit 的内容**整条曲线空转**
（用户会觉得"选了档位没反应"）。用户可在界面手动填「显示器峰值」绕过；要不要给
这个自动值加合理性上限，需要 P4 决策，不要擅自改既有行为。

#### P4 结论：HDR10 元数据**不需要改代码**（2026-10-17 查证）
查了 `VideoPresenter::applyHdrMetadata` 与 `engine::clampMetadataToCurve`：

- 它已经是"描述输出而不是片源"：曲线启用且 `peakCapNits` 有效时
  `cll = min(sourceCLL, cap)`、`fall = min(fall, cll)`，并用 2 秒节流 + 值不变不重发。
- 实测数据（p8_30s.mkv）：片源 `maxCLL=359 / maxFALL=348`，用户 cap 经缩放到 4000，
  所以实际上报就是 359；跟随把该帧最高输出压到 336，偏差 6.8%，属于测量噪声级，
  不足以让显示器做错二次映射。
- 若改成"跟随开启时按帧峰值上报 MaxCLL"，会变成每 2 秒甚至更快地改元数据，
  显示器可能反复重启自己的 tone mapping（闪烁），收益（359→336）远小于风险。
  → **维持现状**。只有当将来出现"跟随把高光压掉 30% 以上"的强压缩模式时才需要重新审视。

#### 真正值得产品侧跟进的一条（不在 DV 范围，已告知用户）
`displayPeakNits` 自动值取 DXGI 的 `MaxLuminance`，本机报 10000 级，`resolveHdrTuning`
据此把档位等比放大到 clamp 上限 → 静态曲线对 336 nit 的内容空转。
可行的低风险改法（未实施）：给自动值加合理上限，或在界面标出"系统报告值可疑，
请手填真实峰值"。用户当前可手填绕过。

程序包状态（当前部署目录 `D:\Program\Veyra-2.1.0-NVIDIA-win64-portable`；
旧路径 `D:\项目\Veyra-NV\...` 已废弃，见 MY_WORK.md 第一节）：

生效 exe = .p5 = 见本次回复末尾的哈希（含 P1+P2+P3）
.orig 作者版 = 7ADD56B9651D6B62…（永远不动）
qml\Veyra 与 qml.p5\Veyra 都已含杜比视界卡（含新开关）

### 2. 已实测的关键事实（含本轮新增）

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

### 2.5 P5（P7 双层）已探明的实现事实（2026-10-17）

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


### 2.6 P5 最终状态（2026-10-17 收尾）

P7 双层增强层**已经实现并跑通**，默认关闭，界面开关在
专业页 → 杜比视界设置 → 「合并增强层（Profile 7 双层）」。

| 子项 | 状态 | 证据 |
|---|---|---|
| P5.1 EL 在哪 / 怎么封装 | ✅ | EL 在 NAL type 63，`7E 01` + 标准 NAL；参数集在 TrackEntry 的 `hvcE`（不在 hvcC） |
| P5.2 EL 独立解码 | ✅ | 1920×1080 yuv420p10le；1200 帧 / 1238 包；0.07 ms/包；残差实测 −13…+17 |
| P5.3 GPU 合并 | ✅ | 开关开启后画面逐帧改变 0.23~0.29（整体 1.2%），关闭时**逐位相同** |
| P5.4 回落 | ✅ | Profile 8 / Control 片：`bound=0`，零 EL 活动，播放正常 |

#### 实现位置

- `src/source/MediaFileSource.cpp`：`extractEnhancementNals` + `feedEnhancementLayer`
  （第二条软解解码器，容错模式），`out.doviEl` / `out.doviP7`
- `include/veyra/pipeline/DolbyVisionP7.h` + `DoviP7Constants.h`：参数打包进
  `dv0..dv6`（`dv6.w=2` 表示重建、`=3` 表示重建并把残差画成灰度）
- `include/veyra/engine/DoviBlElCompose.h`：CPU 参考实现（7 条单测锁住）
- `shaders/YuvToLinearRgb.hlsl`：`ReconstructLumaP7`（t12 = EL luma，R16_UINT）
- `src/pipeline/EnhanceGraph.cpp`：`uploadElLuma` + 绑定 slot 12 + 参数写入

#### 三个把人骗过的坑（都会伪装成"功能没生效"）

1. **`dv6.w` 写成了 `params[24]`**（那是 dv6.x）。着色器读到的永远是 0，合成从不执行；
   而单测断言写的是同一个错槽位，所以一直是绿的。**教训：断言要对着语义写，不是对着实现。**
2. **着色器从未部署**：只复制 exe，`.dxil` 还是旧的（20596 vs 21808 字节）。
   **教训：部署清单必须包含 `shaders/*.dxil`。**
3. **播放位置记忆**：第二次运行从上次位置继续，两次画面完全不同步，对比毫无意义。
   **教训：自动化对比必须用全新 data-dir（清空 `positions`）。**


#### P5 的最终结论（2026-10-17 收尾）

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


#### 2026-10-28 修复：崩溃根因与配对率（P0 + P1）

2026-10-17 的结论是"配对率被 EL 出帧率卡死"，这个判断**是错的**。真实原因有两个，
都在我们自己的代码里。

**P0：d3d11va 表面池被配对帧泄漏耗尽（崩溃，`code=-12`）**

`read()` 为等 EL 把基础帧 `av_frame_clone` 后放进 `baseWaiting_`，交付时把裸指针交给
调用方却**从没释放**。每个克隆都 ref 住一片 d3d11va surface，而该解码器的表面数组是
**固定大小**的（日志实测 `array=20`），Veyra 又从未设过 `extra_hw_frames`：

```
[media] decoder: d3d11va decoded surface 3840x2176 ... array=20 slice=19
[engine-stall] frame=12 ...
[media] decoder: send_packet failed code=-12 text=Cannot allocate memory
```

20 片里 DPB 常驻约 8 片，可周转 12 片，正好等于崩溃前的 12 帧。上游 mpv 在
`video/decode/vd_lavc.c` 里把这件事写得很直白（"every frame ... pins a surface from
the fixed pool"），并给 BL 解码器预留 6+8 片；我们没有。

修复：交给调用方的克隆在**下次 `read()` 入口**释放（调用方本来只借到下一次读，
`EngineController::retainCandidate()` 会先自己拷一份）；seek、硬件→软件回退、close
时一并清理；P7 启用时给硬解预留 10 片（实测池子从 `array=20` 变成 `array=30`）。

**P1：配对永远差一帧，而且过期判断用错了基准**

EL 解码器按设计比基础解码器**慢一帧**（送包 N 时解出的是 N−1 的帧）：

```
attempt base_pts=42  cached=[0]      ← BL 42 在等，缓存里只有 EL 0
attempt base_pts=83  cached=[42]
attempt base_pts=125 cached=[]       ← 0 和 42 都被当成"过期"删掉了
```

新增的"丢弃过期 EL"用了**当前正在探测的 BL 帧**作基准，于是把**队列里更老的 BL 帧
还需要的那一帧**删掉了，谁也配不上。改成按 `baseWaiting_.front()`（最老等待帧）判断，
并把配对容差从精确相等改成 2 tick。

**P1：改用 FFmpeg 自带的 `dovi_split` 拆流**

手写 walk 只能在"参数集恰好出现在流里"时工作，seek 之后无法恢复。FFmpeg 9.0 的
`dovi_split` BSF 做同样的事，并且额外：从容器的 `hvcE` 记录取 EL 参数集
（Matroska BlockAdditionMapping → `AV_PKT_DATA_HEVC_CONF`，libavformat 已经帮我们挂好）、
按 profile 折半算 EL 尺寸、无 EL 的 AU 返回 `EAGAIN`。容器没有 hvcE 时自动回退到手写 walk。
另外：RPU 的 `disable_residual_flag` 连续 24 帧为 1 时判定为 MEL，直接关掉 EL 解码。

**实测（4K P7 FEL 测试片，120.036 秒）**

| 项目 | 修复前 | 修复后 |
|---|---|---|
| 播放 | 12 帧后 `-12` 崩溃 | 全片播完，0 次 `-12`、0 次回退 |
| EL 拆流 | 手写 NAL walk | `dovi_split filter ready mode=el EL 1920x1080 extradata=795 bytes` |
| 配对 | 多数配不上 | `PAIRED held pts=…` 正常，`bound=1 frames=800` |
| seek | 未验证 | 8 次精确跳转，每次 2 帧内重新配上 |

仍未做：FEL 重建只有亮度通道（上游 libplacebo 是三通道 + pivot 多项式 + 12bit），
`DoviBlElCompose.h` 里的 CPU 参考实现有完整 MMR，尚未移植到 HLSL。


#### 2026-10-28 P2：三通道残差与 MMR 逆映射

P0/P1 之后能播了，但合成只加**亮度**残差，色度是原样透传。抓一帧 RPU 看就明白这不成立：

```
curve 0 (Y) : numPivots=9  mapping=poly(1)  coef=(0, 1<<23, 0)   → identity
curve 1 (Cb): numPivots=2  mapping=MMR     mmrOrder=3
curve 2 (Cr): numPivots=2  mapping=MMR     mmrOrder=3
nlq: offset=512 slope=2048 threshold=0 inMax=1048576 （三个分量相同）
```

两个事实：
1. **三个分量各有自己的 LINEAR_DZ 量化器**，残差必须按分量分别反量化；
2. **色度的逆映射是 MMR 多项式**，不是恒等。以 Cb 为例，权重是
   `0.762·c + 0.381·c² + 0.1905·c³`（`coef_log2_denom = 23` 归一化后），
   在 0.5 附近几乎不变、离中心越远越偏离恒等。

实现：
- 参数布局改成每分量一组 NLQ（`dv0..dv2`），`dv3` 放位深/分母，`dv4/dv5/dv6` 放 MMR 常数、
  模式与阶数；
- MMR 权重放不下常量池（60 个已经用完），改为 **18×1 的 RGBA32F 系数纹理（t14）**，
  每帧上传 288 字节；
- 色度平面上传到 **t13（R16G16，960×540）**，与 luma 采样一致地做双线性上采样后取整；
- pass 的额外 SRV 范围从 `t8..t12` 扩到 **`t8..t14`**（这条没改会导致
  `CreateComputePipelineState failed constants=60 extra=5`，shader 里引用了范围外的寄存器）。

`ReconstructP7` 现在是：分量级 NLQ 反量化 → 分量级逆映射（identity 或 MMR）→
三者相加 → 12bit 量化。与 libplacebo 的 `sh_dovi_compose_nlq` + `sh_dovi_reshape`
同构，差别只在我们要求「单段 MMR」或「恒等」，其它曲线仍回退 BL。

**实测**：`reconstruction parameters packed nlqOffset=512 slope=2048 coefDenom=23 modes=0/1/1 mmrOrders=0/3/3`
与 RPU 完全对应；残差图（`VEYRA_DOVI_P7_DUMP=1`）能看到城市夜景的高频细节，
不是全屏过曝，说明 NLQ 的量级正确。

仍未做：多段曲线（每个分量 >2 个 pivot 且分段不同）与 `ext_mapping_idc != 0` 的片源仍回退 BL。

#### 2026-10-28 复检：打包对了，读错了

P2 的「实测通过」被自己推翻了一半。复检 1013 行改动时发现两个缺陷。

**缺陷一：Cb 的 MMR 逆映射从未执行**

着色器拿 **luma 的 mode 标志**去判断要不要处理 Cb，又把结果写进 Cb 的槽：

```hlsl
// 错的
if (dv5.y > 0.5) base.y = MmrEvaluate(1, sig, dv4.z, int(dv6.y + 0.5));
if (dv5.z > 0.5) base.z = MmrEvaluate(2, sig, dv4.w, int(dv6.z + 0.5));
```

`dv5.y` 是 mode[0]（Y 的）。本片源 Y 是恒等映射 → `dv5.y == 0` → **整个分支不进**，
Cb 一直按恒等处理；Cr 进了，但 constant 取的是 Cb 的。正确写法是每个分量读自己的三元组：

| 分量 | mode | constant | order |
|---|---|---|---|
| Y | `dv5.y` | `dv4.z` | `dv6.x` |
| Cb | `dv5.z` | `dv4.w` | `dv6.y` |
| Cr | `dv5.w` | `dv5.x` | `dv6.z` |

**为什么当时没发现**：日志打的是 `modes=0/1/1 mmrOrders=0/3/3`——**打包侧完全正确**；
而消费侧错位后，画面依然正常，因为
(a) 本片源三个分量的 `mmr_constant` 都是 0，
(b) Cb 的映射 `0.762c + 0.381c² + 0.1905c³` 在 0.5 附近近似恒等，只在两端才偏。

**教训：参数日志只能证明"打包对"，不能证明"消费对"。** 要证明后者，得让参数处在
会让错误显形的位置——比如拿一张 mid-grey 之外、三通道 constant 非零的帧去比。

**缺陷二：引擎缓存了借用的 EL 帧**

`out.doviEl = elFrame_` 是借出指针，而 `elFrame_` 会在下次配对、MEL 关闭、解码失败时
被 `av_frame_free`。引擎的 `retainCandidate()` 却把整个 `pkt`（含该指针）存进
`cachedPacket`，稍后重放时再传给 `graph->process(...)` → 可能读已释放内存。

触发窗口很窄（要 `initialFileFramePending`，而 P7 内容通常是 HDR，走不到
`pausedEligible` 那条重放路径），但确实存在。修法：缓存时把 `doviEl` 置空，重放那一帧
退回基础层，视觉上是一帧的事。

**教训：借用指针一旦被上层缓存，生命周期就不再由出借方控制。** 要么在交接处收窄
（这里是置空），要么把所有权真正转移出去。

#### 多段曲线：量完之后决定不做（2026-10-28）

P2 只支持「恒等」和「单段 MMR」的逆映射，多段曲线一律回退基础层。当时判断是
"手头没有多段片源，做了也无法验证"。后来加了按帧统计（`P7 mapping frames`），
三个片源的结果：

| 片源 | 类型 | 被拒帧 | 比例 |
|---|---|---|---|
| `FEL TEST ST DL P7 CMV4.0 4000nits V4` | 测试片 | 0 / 600 | **0.0%** |
| `FEL RPU_EL测试` | 测试片 | 4 / 300 | **1.3%** |
| `Despicable Me 2010` UHD remux | **真实电影** | 9 / 600 | **1.5%** |

**测试片和真实盘的结论一致：多段帧约 1.5%，即每 60–70 帧一帧、大约每 3 秒一次。**
1.5% 的帧不合成，视觉上是一次极轻微的跳变，不构成"画面不一致"。

所以：**不做多段支持**（成本 300–400 行，收益 1.5% 的帧）。这不是偷懒，是量过之后
的取舍。如果以后遇到整片使用多段曲线的片源（`refused` 比例很高），计数器会立刻暴露，
那时候再做，而且有现成的验证素材。

顺带纠正一个误判：日志里 `identity ↔ multi-piece` 来回刷屏，看起来像"整段交替"，
那是因为**只在状态变化时打印**。实际计数只有 1–2%，"看起来一半"是日志形态造成的错觉。

#### L5 有效区域：查到了，但不动（2026-10-28）

DV 的 L5 扩展块描述"有效画面矩形"，mpv 用 `dovi-level5-mode` 暴露它（`mask` 涂黑 /
`crop` 裁掉）。实测两个片源：

| 片源 | L5 active area | 有效画面 |
|---|---|---|
| `[Dolby] FEL Test` | top=275 bottom=276 | 3840×1609（2.39:1） |
| `Despicable Me 2010` | top=42 bottom=42 | 3840×2076（1.85:1，几乎无黑边） |

结论：
- **`mask` 没有收益**：Veyra 是 HDR/PQ 输出，黑边在 PQ 域是 0，本来显示就是纯黑，
  不像 SDR 映射那样会被抬成灰。
- **`crop` 只是显示偏好**：把 2.39:1 的画面裁边填满窗口会改变构图，多数播放器默认不裁。
  真要做是 ~50 行（把 L5 传给 presenter，`videoRect` 按有效区比例算），默认关闭。

所以现在只记录、不应用。数据留在日志里，需要时再说。

#### Profile 5 实测（2026-10-29）

P5 一直是本项目里唯一只有代码审查、没有真实数据的主路径。找到片源后补上了：

片源 `Wednesday S02E01 2022 2160p NF WEB-DL DDP5.1 Atmos DV H.265`（Netflix，8.2 GB）。

```
[source-dovi] profile=5 level=6 compatibility=0 bl=true el=false rpuDeclared=true route=2
[source-dovi] base layer pinned to PQ for profile 5
[source-dovi] profile 5 base layer converted as IPT-PQ-C2: RPU reshaping (per-frame,
              projected on {1,sqrt(x),x}) -> RPU ycc_to_rgb -> PQ EOTF
              -> RPU rgb_to_lms x fixed HPE LMS->BT.2020 -> PQ OETF
[source-dovi] profile 5 constants ycc_to_rgb=[1.000, 0.0975, 0.2052 | ...]
              lms_to_rgb=[3.2388, -2.3256, 0.0867 | ...]
```

画面色彩正常（若 IPT-PQ-C2 映射出错会呈现明显的绿屏或紫屏），日志 0 错误。

顺带修掉两处日志缺陷：

| 问题 | 后果 |
|---|---|
| 钉基底时无条件打印 "8.1 is PQ" | profile 5 的日志在说 8.1 的事 |
| profile 5 也打印 P7 的曲线诊断 "supported" | 打包器被短路，report 是默认构造值，相当于对一条没有增强层的流断言"曲线可用" |

构造伪 P5 的办法（改容器 DoVi 配置记录，用于验证 P5 的守门逻辑，不验证转换）：
把 MKV 里 `dvcC` 之后 7 字节起的 `DOVIDecoderConfigurationRecord` 改成
`profile=5, el=0, bl=1, compatibility=0`。这样 Veyra 会进入 P5 路径，并因为
RPU 仍是 P7 的、没有颜色块而正确拒绝——这正好验证了"不硬转"的防御逻辑。

#### Profile 8 的基底类型由 compatibility ID 决定，不是 profile 号（2026-10-29）

常见的混淆是把 8.2 和 8.4 对调。按 Dolby 的 bitstream profiles 文档 Table 2：

| CCID | 标签 | 基底 |
|---|---|---|
| 0 | Dolby Vision proprietary | P5 |
| 1 | HDR10 | P8.1 |
| 2 | **SDR**（BT.1886 / BT.709） | **P8.2** |
| 4 | **HLG** | **P8.4** |
| 6 | Blu-ray | P7 |

所以：

- **iPhone 录的 Dolby Vision 是 HLG 基底，即 Profile 8.4**
- **SDR 兼容基底是 Profile 8.2**

代码按 CCID 判断而不是 profile 号（`source/DolbyVision.h` 的 `route()`），并
据此钉住色彩标签：`matches()` 要求 HLG 是 `transfer==HLG && BT2020`、SDR 是
`!isHdrPath() && BT709`。

实测印证：B 站那部 Galaxy 是 `profile=8 compatibility=4`，日志为
`base layer pinned to HLG`。

一个细节：官方 Table 1 里 Profile 8 的 CCID 只列 "1 or 4"，CCID 2 在官方对应
Profile 9（AVC 8-bit）。但实践中确实存在 `profile=8 compatibility=2` 的文件
（Jellyfin 也接受 1/2/4），所以「P8.2」是实践叫法。按 CCID 判断两种情况都能
正确处理。

#### L2 / L8 trim 元数据：数据在，但没人做，也不该猜（2026-10-29）

调研过一次，结论是不做。记在这里，避免以后再查一遍。

先纠正一个常见误解：**L2 不是「局部色调映射」**。按 Dolby 的元数据分级，L2 是
**trim pass** —— 为不同目标显示器（100 / 600 / 1000 nits 等）预制的色调曲线与
色域微调，是逐场景的全局调整，不是空间上的分区映射。L8 是 CM 4.0 版的 trim，
是 L2 的超集，多带 `target_mid_contrast`、`clip_trim`、`style_id` 等字段。

片源实测确实带这些数据（`extension blocks` 的 levels 列表里能看到 `2` 和 `8`）：

| 片源 | L2 | L8 |
|---|---|---|
| 《星期三》S02E01（Netflix P5） | 3 个 | 1 个 |
| `FEL TEST ST DL P7 CMV4.0` | 1 个 | 1 个 |

但开源实现里**没有一个读取它们**：

| 项目 | 情况 |
|---|---|
| libplacebo | 只做 reshaping + 用 L1 驱动的 `st2094` tone mapping |
| mpv | 跟随 libplacebo |
| dovi_tool | RPU 处理工具（info / convert / export），不渲染 |

**根本原因不是难，是目标设备未知。** L2/L8 的每条 trim 都绑定一个
`target_display_index`（「给 600 nits 电视用的」）。TV-Led 模式下电视自己知道
属于哪一档；player-led 播放器只能从 EDID 猜显示器峰值，**猜错档位会让画面比
不做更差**。

真要走的顺序是：先做 L2（参数少、档位语义清楚），L8 等 CM 4.0 片源多起来再说。
前提是先有验证手段 —— 现在没有 Dolby 参考播放器可以逐帧对比，做了也无法证明
是「对」的。

#### 「动态元数据完整应用」不是待办，是能力边界（2026-10-29）

这条经常被列成「未做」，但它不是一个能被「做完」的东西。

杜比把逐帧显示管理叫 Display Management（DM），是授权技术，没有公开规范。
开源世界能做到的只有近似：

| 实现 | 实际做的事 |
|---|---|
| libplacebo / mpv | 读 L1 的 `max_pq` / `avg_pq`，按 ST 2094 的思路算一个 knee，套到自己的 tone mapping 上 |
| Veyra | 读 L1，只移动 roll-off 起点（比用户曲线压得更早、不抬高），默认关 |

两者都是「用 L1 这个信号驱动一个通用映射」，都不是 DM，差别只在 L1 怎么用。

要「完整」，缺三样东西，而且都不是工程量的问题：

| 缺什么 | 为什么拿不到 |
|---|---|
| DM 的映射算法 | Dolby 不公开，需授权 |
| L2/L8 的目标设备绑定 | 播放器不知道显示器属于哪一档（见上一节） |
| 验证手段 | 没有 Dolby 参考播放器可逐帧对比，做了也无法证明「对」 |

连商业播放器（PowerDVD、JRiver）的「杜比视界支持」也是走 TV-Led —— 把 RPU
原样送给支持的显示设备，自己不算 DM。

所以评估这条时，正确的问法不是「什么时候做」，而是「要不要把近似做得更狠」：

- 把 L1 跟随做彻底（不单向下压、整条曲线跟随）：约 200 行，会更频繁地动画面
- 补 L2 trim（选档 + 套用）：约 150-200 行，选档靠猜

两条都是锦上添花，也都无法证明比现在更「对」。

#### 复盘（2026-10-17）：修掉 4 个缺陷

对整条杜比视界路径做了一次通读，P7 这部分有 3 个缺陷、参考实现有 1 个越界：

| # | 位置 | 问题 | 后果 |
|---|---|---|---|
| 1 | `takeElForPts` | 帧从时间戳表取出后交给局部指针，没有成员持有 | 每配对成功一帧泄漏约 3MB（1080p AVFrame） |
| 2 | seek 路径 | 只 flush 了基础层解码器 | 跳转后增强层沿用旧参考链，首帧可能出垃圾 |
| 3 | `feedEnhancementLayer` | 残留一个从不被填充的 `elFrame_` 分配 | 死代码 |
| 4 | `doviInverseMap` | `pivots[num_pivots - 1]` 未检查 `num_pivots` | RPU 解析失败时读 `pivots[-1]` |

第 1、2 条最值得记：它们都不会让功能"看起来坏"，只会悄悄吃内存、或跳转后偶发一次怪画面。

#### 仍未验证：MMR（色度逆映射）

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


### 2.7 HDR 曲线被假峰值稀释（2026-10-17 修复）

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

#### 顺带做的界面改进

- 杜比视界卡片 summary：`Profile 7 · RPU 有` → `Profile 7（双层）· 最亮 1000 nit`
- 卡片内新增「当前片源的推荐」一行（复用曲线卡片的 `applyRecommendedHdrTuning`），
  并加门控：**非杜比视界片源时不可点**（HDR10 同样带亮度元数据，一开始漏了这个判断）
- 用户档案的 5 条影片记忆统一为一套通用基准：参考白 203（BT.2408）、高光起点 450、
  上限与显示峰值 800、滚降 110、强度 100；原文件已备份

#### 一项评估后决定不做

「跟随效果反馈」（在界面上显示当前帧跟随压到了多少 nit）。它是可观测性而非功能，
需要跨 presenter / snapshot / bridge 四层，且每帧变化的值不能直接推给 QML。
对普通使用没有价值，开关对比更直观。


#### 增强层覆盖率上限：实测验证（2026-10-17）

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

#### 仍未定论的两条（不要当成已完成）

- **EL 覆盖率矛盾**：离线统计 mkv block 得 51%（`111000` 节奏），运行时
  `decoded/packets` ≈ 97%。大概率是当初的 block 统计口径漏了 BlockGroup，
  但没有重新统计确认。当前实现按"这一包是否解出 EL 帧"判断。
- **画面方向**：CPU 原型预测 +6% 变亮，实测某段 −1.2% 偏暗。残差围绕零点对称
  （−13…+17），不同片段方向不同，原理上说得通，但**没有第二份独立实现交叉验证**。

#### 诊断开关（默认全关）

```
VEYRA_DOVI_P7_DUMP=1   着色器把 EL 残差画成灰度（黑 = 该帧无残差）
VEYRA_DOVI_TRACE=1     逐帧 RPU + 合成绑定计数
界面开关                专业页 → 杜比视界设置 → 合并增强层
```

### 3. 下一步（按顺序，每步都要编译+运行验证）

#### P3-4 与 mpv（gpu-next）同片对拍
    D:\DSH\.dsh-tmp\dovi-test\tools\mpv（v0.41.0 / libplacebo v7.376）
    建议：同片同段截图对比高光过渡；Veyra 侧用 p8_30s.mkv + 打开 HDR 曲线 + 打开跟随

#### P4 HDR10 元数据一致性
    applyHdrMetadata 仍用静态 hdrCurve 做 clamp（刻意如此，避免每帧改显示端元数据）；
    P4 要决定：跟随 DV 时显示端元数据是否改为按片段/按片峰值上报

### 4. 绝对不要再踩的坑（每一条都真踩过）

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

### 5. 用户偏好

- 全部输出用中文
- 结论要有证据（日志/哈希/对表），不要凭记忆下判断
- 删除一律进回收站；临时产物放 D:\DSH\.dsh-tmp\
- 验收前完整部署（exe + qml + shaders），并报告哈希
- 验收通过前不推送任何东西



---


## 二、Profile 5 验证记录（原 DOVI_P5_VERIFICATION.md）

This file is the evidence behind the profile-5 base-layer conversion: what was
run, against which sample and reference decoder, and what came out. It exists so
the reported comparison can be re-checked without re-deriving the method.

Everything below was produced with `tools/dovi_p5_probe`, which runs the
**shipped** `YuvToLinearRgb.dxil` over a decoded frame with the same root
constants `EnhanceGraph::convertInput` packs, plus its `--scan` / `--history`
modes, which report the RPU-derived conversion parameters without touching the
GPU.

### Reproduce

This repository has no CI configuration, so every number below is a local run.
They are reproducible with the committed tooling — the probe, the comparison
helper and the test target:

```
cmake --build <build> --target veyra_dovi_p5_probe veyra_repair_contract_tests
<build>/veyra_repair_contract_tests.exe                                 # 249 checks, 0 failures

PROBE=<build>/veyra_dovi_p5_probe.exe
SAMPLE="<the profile-5 file, see below>"
$PROBE --input "$SAMPLE" --time 0   --scan 1500 --sequential            # 1.
$PROBE --input "$SAMPLE" --time 0.5 --later 600  --history              # 2.
$PROBE --input "$SAMPLE" --time 95  --later 1200 --history
$PROBE --input "$SAMPLE" --time 95  --out p5-on.raw                     # 3.
$PROBE --input "$SAMPLE" --time 95  --out p5-off.raw --no-dovi
ffmpeg -ss 95 -i "$SAMPLE" -frames:v 1 \
  -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
  -f rawvideo -pix_fmt rgb48le ref95.raw
python tools/dovi_p5_probe/compare_raw.py pq p5-on.raw  ref95.raw       # conversion
python tools/dovi_p5_probe/compare_raw.py pq p5-off.raw ref95.raw       # control
$PROBE --input "$SAMPLE" --time 95 --out p5-sdr-on.raw  --sdr           # 4.
$PROBE --input "$SAMPLE" --time 95 --out p5-sdr-off.raw --sdr --no-dovi
$PROBE --input "<HDR10-only file>" --time 95 --out hdr10-new.raw        # 5.
$PROBE --input "<HDR10-only file>" --time 95 --out hdr10-old.raw --shader <pre-change shader dir>
python tools/dovi_p5_probe/compare_raw.py f2 hdr10-new.raw hdr10-old.raw
```

`compare_raw.py`'s `pq` mode does the domain conversion the comparison needs: it
takes the probe's linear scRGB output (1.0 = 80 nits), goes back to BT.2020 with
the player's matrix and re-encodes PQ, so both sides live in one domain.

### Tests

| suite | result |
| --- | --- |
| `veyra_repair_contract_tests` | **249 checks, 0 failures** |
| `veyra_preset_library_tests` | 236 checks, 0 failures (untouched by this PR) |
| `veyra_effect_chain_tests` | 204 checks, 0 failures (untouched by this PR) |

The profile-5 checks this PR adds or updates:

```
PASS DV P5 base layer is pinned to the routed HDR10 container so the RPU conversion can run
PASS DV P5 does not report an RPU conversion before its parameters are actually in effect
PASS DV P5 reports the IPT-PQ-C2 conversion once the RPU parameters are in effect
PASS DV P5 conversion uses the RPU matrices and the per-frame reshaping curve
PASS DV P5 refuses an RPU whose colour block is empty
PASS DV P5 refuses a reshaping method it cannot reproduce instead of converting with the identity
PASS DV P5 flat mappings convert to the identity regardless of playback history
```

The first of those replaces the assertion that expected profile 5 to stay
`Unsupported`, which the new route made false.



### Sample

| | |
| --- | --- |
| File | `Wednesday S02E01 2022 2160p NF WEB-DL DDP5 1 Atmos DV H 265-HHWEB.mkv` (7.66 GiB) |
| Video | HEVC Main10, 3840x2160, yuv420p10le, full range, 23.976 fps |
| Dolby Vision | profile 5, level 6, `bl=1 el=0 rpuDeclared=1 compatibility=0` (no HDR10/SDR base layer) |
| Colour tags | matrix/transfer/primaries carry no usable VUI colour; the file is routed to the PQ/BT.2020 container and the colour comes from the RPU |

Second sample used for the non-P5 regression: `Spider-Man No Way Home.2021.2160p.BluRay.Remux.HEVC.HDR10…mkv`
(HEVC Main10 HDR10, **no** Dolby Vision metadata at all).

### Reference decoder

```
ffmpeg version n9.0.1-11-ge47273f4d9-20260831 (deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0)
libplacebo filter present; the reference command is
  ffmpeg -ss <t> -i <file> -frames:v 1 \
    -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
    -f rawvideo -pix_fmt rgb48le ref.raw
```

`--no-dovi` on the probe is the control: it clears the enable flag in the
constant block, so the shader takes the ordinary BT.2020 YCbCr path — the
"play it as HDR10" behaviour that renders profile 5 green.

### 1. Every frame carries usable parameters

```
veyra_dovi_p5_probe --input <file> --time 0 --scan 1500 --sequential
```

```
scan summary frames=1500 usable=1500 noSideData=0 missingColour=0 unsupported=0 placeholder=119
```

* 1500 consecutive frames (about 62 s) — **no frame without parameters, none
  unusable**, so the new refusal path never fires on this stream.
* 119 frames carry a *flat* (placeholder) reshaping: 0.0–1.0 s (the fade-in),
  9.3–12.3 s, 43.3–44.0 s and 48.3–48.4 s (genuinely black stretches). Those are
  converted with the identity for the affected components rather than with a
  mapping that would blank the frame.

### 2. The conversion does not depend on playback history

`--history` reaches the same frame three ways — a fresh seek, sequential
decoding from the start, and a backward seek after playing a later part — and
compares every matrix and curve value:

```
veyra_dovi_p5_probe --input <file> --time 0.5  --later 600  --history
veyra_dovi_p5_probe --input <file> --time 95   --later 1200 --history
```

```
# a flat frame
fresh-seek  pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
sequential  pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
seek-back   pts=0.500 side=1 usable=1 active=1 placeholder=1 curve0=[0.000000,0.000000,1.000000] curve1=[0.000000,0.000000,1.000000] curve2=[0.000000,0.000000,1.000000]
history independent=1 target=0.500 later=600.000

# a shaped frame
fresh-seek  pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
sequential  pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
seek-back   pts=95.012 curve0=[0.015062,0.088975,0.534110] curve1=[-0.137397,-0.000000,1.291761] curve2=[-0.092896,-0.000000,1.293561]
history independent=1 target=95.000 later=1200.000
```

The playback state is now cleared per frame (and on `open`/`seek`), so a
backward seek into the opening produces exactly what a fresh open produces.
`RepairContractTests` asserts the same property at the builder level
(`DV P5 flat mappings convert to the identity regardless of playback history`).

### 3. HDR10 output vs the reference decode

```
veyra_dovi_p5_probe --input <file> --time 95 --out p5-95-on.raw
veyra_dovi_p5_probe --input <file> --time 95 --out p5-95-off.raw --no-dovi
ffmpeg -ss 95 -i <file> -frames:v 1 -vf "libplacebo=apply_dolbyvision=1:color_trc=smpte2084:format=rgb48" \
       -f rawvideo -pix_fmt rgb48le ref95.raw
```

The probe writes RGBA16F linear BT.709 scRGB (1.0 = 80 nits); the comparison
multiplies by 80, goes to BT.2020 with the player's own inverse matrix and
re-encodes PQ, then compares against the reference's 16-bit PQ values:

| run | mean abs | p50 | p95 | bright-area PQ RGB (probe / reference) |
| --- | --- | --- | --- | --- |
| conversion **on** | **0.00354** | 0.00255 | 0.01501 | 0.5210/0.5322/0.5308 vs 0.5210/0.5324/0.5311 |
| control (`--no-dovi`) | 0.05410 | 0.03779 | 0.15231 | 0.7482/0.8377/0.7822 vs 0.5210/0.5324/0.5311 |

The control is 15x worse and its bright area is the washed-out green picture,
so the comparison is measuring the conversion and not noise. The residual 0.0035
is the difference between libplacebo's exact piecewise reshaping polynomials and
the three-term `{1, sqrt(x), x}` projection the shader can afford (the exact
curves score 0.0007 on the same metric, applying no reshaping at all scores
0.041, and skipping the matrices entirely — the control above — 0.054).

### 4. SDR output route

The same conversion feeds the pre-existing BT.2390 SDR tone map when the display
is SDR (`--sdr` clears the scRGB bit):

| run | mean | max | clipped at 1.0 | near black |
| --- | --- | --- | --- | --- |
| conversion on | 0.0209 | 0.9893 | **0.000 %** | 26.6 % |
| control | 0.0630 | 1.0000 | 0.81 % | 40.8 % |

Mean absolute difference between the two: 0.0339. So the conversion is applied on
that route as well, and the tone-mapped output stays inside range where the
control clips. (This is a range/consistency check; the colour accuracy claim
above is the PQ-domain reference comparison, which is where the two decoders can
be compared in the same domain.)

### 5. Non-P5 regression

An HDR10-only file (no Dolby Vision metadata, so the profile-5 block stays
disabled) decoded at t=95 with the shipped shader and with the shader from
before this change:

```
veyra_dovi_p5_probe --input <HDR10 file> --time 95 --out hdr10-new.raw
veyra_dovi_p5_probe --input <HDR10 file> --time 95 --out hdr10-old.raw --shader <pre-change shader dir>
```

```
samples=33177600 meanAbs=2.151e-09 max=4.883e-04 frac_equal=0.999972 frac_le_1step=1.000000
```

99.9972 % of samples are bit-identical and every sample is within one FP16 step
(max 4.9e-04, mean 2.2e-09): the added constant block and branch do not disturb
any path that is not profile 5.

### 6. Hardware versus software decoding

`veyra_dovi_p5_probe` decodes in software on purpose: the comparison needs the
frame in system memory next to an independent reference decode. The hardware
(D3D11VA) path was verified in the player itself, on the same file, with the
same shader:

```
[source-dovi] profile=5 level=6 compatibility=0 bl=true el=false rpuDeclared=true route=2 nativeOutput=0 rpuApplied=0
[source-dovi] profile 5 base layer converted as IPT-PQ-C2: RPU reshaping (per-frame, projected on {1,sqrt(x),x}) -> …
[source-dovi] profile 5 constants ycc_to_rgb=[1.000000,0.097534,0.205200|1.000000,-0.113892,0.133179|1.000000,0.032593,-0.676880] lms_to_rgb=[3.238759,-2.325560,0.086741|…]
[source-dovi] profile 5 RPU carries a flat (placeholder) reshaping on this frame; the affected components use the identity
[resolution]  source=3840x2160 base=3840x2160 nr=1920x1080 flow=1920x1080 fg=3840x2160 output=3840x2160
```

The matrices the player packed on the hardware path are the same values the
probe reports for the same frame, playback ran for the full 20 s window with no
refusal message, and frames were presented throughout.

### What this record does not establish

* The exact-piecewise-polynomial accuracy (0.0007) is not what ships: the shader
  uses the three-term projection described above.
* Non-polynomial (MMR) reshaping is not implemented; such a frame is now refused
  instead of being converted with the identity.
* The probe has no hardware-decode mode, and the libplacebo comparison is a
  software-decode comparison.



---


## 三、P7 双层合成算法（原 DOLBY_BL_EL_COMPOSITING.md）

来源：**ETSI GS CCM 001 V1.1.1 (2017-02)** 第 5.4 节（公开规范，本地副本
`D:\DSH\.dsh-tmp\dovi-follow\etsi\gs_ccm001.pdf`，提取文本 `etsi\alg.txt`）。
FFmpeg 的 `AVDOVIRpuDataHeader` / `AVDOVIDataMapping` 已经把 RPU 解析成这些参数
（`coef_data_type` 注释："lavc always converts to fixed"）。

### 0. 输入

- BL：基础层图像，三个分量 `S0`（luma）/`S1`,`S2`（chroma），`BL_bit_depth` 位
- EL：增强层图像，同样三个分量，`EL_bit_depth` 位；EL 分辨率通常是 BL 的水平/垂直各一半
- RPU 参数（每帧）：`curves[3]`（piece-wise）、`nlq[3]`、`coef_log2_denom`、
  `bl_video_full_range_flag`、`disable_residual_flag`、`ext_mapping_idc_0_4`

### 1. BL 逆映射（5.4.2）

#### 1.1 pivot 索引（5.4.2.2）

对每个样本 `s`，找到 `pivot_value[cmp][i] <= s < pivot_value[cmp][i+1]` 的 `i`，
即 `pivot_idx`。`s` 超出范围时 clip 到首/末 pivot。

#### 1.2 多项式（luma，cmp=0）（5.4.2.3.2）

```
if (s <  pivot_value[cmp][0])                        s =  pivot_value[cmp][0];
if (s >  pivot_value[cmp][num_pivots_minus2[cmp]+1]) s =  pivot_value[cmp][num_pivots_minus2[cmp]+1];

ss = 1; shift = 20;                 // 2*(maximum BL_bit_depth)
vv = 0;
for (i = 0; i <= poly_order_minus1 + 1; i++) {
    vv += poly_coef[i] * (ss << shift);
    ss *= s;
    shift -= BL_bit_depth;
}
vv = (vv < 0) ? 0 : vv;
v  = vv >> (4 + coefficient_log2_denom);
v  = (v > 0xffff) ? 0xffff : v;
```

#### 1.3 MMR（chroma，cmp=1,2）（5.4.2.3.3）

需要**先把 BL luma 下采样到 chroma 分辨率**（边界按像素复制）。特征向量 `tt[]`
（20 位定点，order 1~3）：

```
tt[0] = 1<<20;
// order>=1
tt[1]=s0<<(20-BL_bit_depth); tt[2]=s1<<...; tt[3]=s2<<...;
tt[4]=(s0*s1)<<(20-2*BL_bit_depth); tt[5]=(s0*s2)<<...; tt[6]=(s1*s2)<<...;
tt[7]=(tt[4]*tt[3])>>20;
// order>=2
tt[8]=(s0*s0)<<(20-2*BL_bit_depth); tt[9]=(s1*s1)<<...; tt[10]=(s2*s2)<<...;
tt[11]=(tt[4]*tt[4])>>20; tt[12]=(tt[5]*tt[5])>>20; tt[13]=(tt[6]*tt[6])>>20;
tt[14]=(tt[7]*tt[7])>>20;
// order>=3
tt[15]=(tt[1]*tt[8])>>20; ... tt[21]=(tt[7]*tt[14])>>20;

rr = mmr_constant*tt[0];  cnt=1;
for (i=1; i<=MMR_order; i++)
    for (j=0; j<7; j++) { rr += mmr_coeff[i][j]*tt[cnt]; cnt++; }
rr = rr < 0 ? 0 : rr;
v  = rr >> (4 + coefficient_log2_denom);
v  = v > 0xffff ? 0xffff : v;
```

### 2. EL 逆量化 NLQ_LINEAR_DZ（5.4.3.2）

输出 `r` 是 **17 位有符号**：

```
T = fp_linear_deadzone_threshold[cmp];
S = fp_linear_deadzone_slope[cmp];
R = fp_hdr_in_max[cmp];

rr = e - nlq_offset[cmp];
if (rr == 0) { r = 0; }
else {
    sign = rr < 0 ? -1 : 1;
    rr <<= 1;  rr -= sign;  rr <<= (10 - EL_bit_depth);
    dq = rr * S;
    TT = (T << (10 - EL_bit_depth + 1)) * sign;
    dq += TT;
    RR = R << (10 - EL_bit_depth + 1);
    if (dq >  RR) dq =  RR;
    if (dq < -RR) dq = -RR;
    r = dq >> (coefficient_log2_denom - 5 - EL_bit_depth);
}
```

### 3. HDR 信号重建（5.4.3.3）

`out_bit_depth`：BL 的传输特性是 BT.1886 → 14；否则（ST 2084）→ `hdr_bit_depth`
（典型 12）。**我们的 P7 片源 BL 是 PQ，所以走 12 位分支。**

```
MAXOUT = (1 << out_bit_depth) - 1;
h = v;
if (!disable_residual_flag) h += r;
h += (1 << (15 - out_bit_depth));
h >>= (16 - out_bit_depth);
h = h < 0 ? 0 : h;
h = h > MAXOUT ? MAXOUT : h;
```

### 4. 转换到 ST 2084（5.5）

**BL 已经是 ST 2084 时整段跳过**（规范原文："this conversion process shall be
omitted"）。我们的 P7/HDR10 片源正好属于这种情况 —— 省掉 RGB 往返与 BT.1886 反
EOTF，是这条链路最省的一处。

### 5. 对我们的实现含义

- 每帧需要的参数全部可从 FFmpeg 的 `AVDOVIMetadata` 取：
  `rpu_data_header`（bit depth / coef_log2_denom / disable_residual_flag / ext_mapping_idc）
  + `vdr_dm_data.mapping`（curves[3] / nlq[3]）
- 多项式与 MMR 都是 20 位定点，**适合直接搬进 compute shader**（int64 需降到 32 位或
  用双 32 位模拟；`tt[]` 的 20 位定点乘法要防溢出）
- luma 走多项式、chroma 走 MMR 且需要下采样 luma —— 意味着合成 pass 不是逐像素独立，
  chroma 需要 luma 的 2×2 邻域
- EL 是 1080p、BL 是 4K：EL 需要 2× 上采样，且 Dolby 的 EL 是"半分辨率残差"

### 6. 未取到的细节（下一步要补）

- `pivot_value[]` 的具体推导（pivot 编码位宽与对齐），第 17 页开头提到但提取不全
- `fp_hdr_in_max` 与 EL 的对应关系（在 5.3.4 的语法定义里）
- `ext_mapping_idc` 的非零分支（扩展映射）是否出现在我们的样本里

（2026-10-17 更新：`pivot_value` 在本样本里就是 RPU 的 `pivots[]` 原值，见下节实测。）

### 7. 本样本的实测参数与原型结果（2026-10-17）

样本：`D:\share\Dolby Vision\[Dolby] ... Full Enhancement Layer Test`（1471 个 AU）。

**参数**（`VEYRA_DOVI_MAPPING_DUMP=<path>` 导出的前 12 帧，逐帧不同）：

```
coefLog2Denom=23  blBitDepth=10  elBitDepth=10  vdrBitDepth=12
disableResidual=0  extMappingIdc=0  blFullRange=0  nlqMethod=0 (LINEAR_DZ)
curve[0] luma  : 9 pivots [0,128,256,...,1023]，8 段全部 polyOrder=1、polyCoef=[0, 8388608, 0]
                 → 斜率 1.0，逆映射恒等（12/12 帧都是）
curve[1] Cb    : 2 pivots，单段 MMR order 3，mmrCoef=[0,6391320,0,0,0,0,0 | 3195660,0,...]
curve[2] Cr    : 同上，主系数落在 tt[2] 位置
nlq[0..2]      : offset=512, inMax=1048576, slope=2048, threshold=0
```

**关键结论**：P7 的 BL 就是标准 HDR10，**luma 逆映射是恒等**，所以合成可以简化为
`h12 = (s_bl << 6 + r_el + 8) >> 4`，EL 的贡献全部来自 NLQ 之后的那一项。

**原型验证**（Python，luma-only；脚本留在 `D:\DSH\.dsh-tmp\dovi-follow\compose_luma.py`）：

| | 值 |
|---|---|
| EL luma 非零比例（第 3 帧） | 74.7%（504..589，零点是 512） |
| BL-only 均值 | 267.32（12bit） |
| 合成后均值 | 283.60（**+6.0%**） |
| 差异 | max +77 / min −7，74.5% 像素有变化 |
| 差异分布 | 光滑区整体提亮 + 细节区高频补足 + 上下黑边完全不动 |

**一个必须在产品实现里处理的问题**：EL 的前两个 AU 解码输出**全 512**（零残差），第 3 帧起
才有真实残差；与 dovi_tool 的 EL 流错误数完全一致，说明这是 EL 流自身的参考结构问题
（不是提取错误）。所以实现里需要"EL 帧不可用时按零残差回落"，而不是丢弃整帧。

**还没做的**：chroma 的 MMR 合成、EL 的 2× 上采样用双线性（原型用的最近邻，会出块效应）、
用 Control 片做正确性对拍。

### 8. 实测：EL 的覆盖率与解码策略（2026-10-17）

Dolby FEL 测试片全片统计（`D:\DSH\.dsh-tmp\dovi-follow\stats_el.py`）：

```
blocks=2878  (= BL 帧数，每块 1 个 VCL NAL)
withEl=1471  (51.1%)
elFirstSlices=1471         每个带 EL 的 block 恰好一整帧
每块 EL slice 数：{0: 1407, 8: 1471}
节奏：111000 111000 …      前 3 帧有 EL、后 3 帧没有
```

**结论：EL 不是逐帧都有的**。所以：

1. "EL 前两帧全 512" 不是解码失败，而是那两帧**本来就没有 EL**
2. 解码器会看到**不连续的参考链**，默认策略是把后续帧全丢弃 → 必须容错解码：
   `err_recognition = 0` + `AV_CODEC_FLAG_OUTPUT_CORRUPT`
3. "空包"是常态（连续 3 帧没有 EL 是设计），**不能**因为空包就禁用 EL 解码
4. 合成时：没有 EL 的帧按零残差处理（等价于 `r = 0`）

实测（`VEYRA_DOVI_EL_DECODE=1`，12 秒播放）：解码器用 EL 自己的 SPS 打开
（722 字节 extradata，1920x1080），`decoded` 持续增长到 100+，无崩溃，
默认路径（不带开关）行为完全不变。

对应代码：`MediaFileSource::feedEnhancementLayer`（提取 type 63 → 剥 `7E 01` →
只送 VCL NAL → 容错软解），开关 `VEYRA_DOVI_EL_DECODE`。

### 9. 性能与回落（2026-10-17）

**性能**（4K FEL 片，12 秒，EL 软解 2 线程）：

```
[source-dovi-el] decoded=2   avgCost=5.54ms over 4 packets     ← 解码器预热
[source-dovi-el] decoded=100 avgCost=0.47ms over 104 packets   ← 稳定后
```

24 fps 每帧预算 41.7 ms，EL 平均只占 **约 1%**，而且只有一半的帧带 EL。
GPU 侧只是多一张 1080p R16 纹理和每帧一次行拷贝上传。结论：性能不是问题。

**回落**（开关打开但片源/参数不适用）：

| 情况 | 行为 | 验证 |
|---|---|---|
| Profile 8 片源（无 EL） | 条件 `profile==7 && el && 开关` 不成立，第二条解码器根本不开 | 打开开关播 p8_30s：**0 条 EL 日志**，exit=0 |
| RPU 无法表达（非恒等 luma 曲线 / 扩展映射 / 非线性 NLQ） | `doviP7ConstantsSupported` 返回 false，`dv6.w` 保持 0，走原路径 | 单测 `DV P7 constants refuse a non-identity luma curve` |
| 该帧没有 EL（EL 稀疏，51%） | 不上传纹理，`dv6.w` 保持 0，走原路径 | 与"零残差合成"数值等价，差 <0.1% |
| EL 解码器打不开 / 送包被拒 | 累加后禁用整条 EL 路径，不影响 BL | 代码路径 |
| 开关关（默认） | 完全不进这条路径 | 默认冒烟 exit=0 |

**结论**：性能可忽略，回落是分层的（片源级 / 参数级 / 帧级 / 解码器级），任何一层出问题都退回
已验收的 BL+RPU 播放，不会只剩黑屏或错误画面。

### 10. 画面验证（2026-10-17）

做了两次运行对齐的亮度对比（各连拍 10 帧，播放位置已清空，避免"继续播放"错位）：

```
关闭增强层: 17.8 18.3 18.7 19.1 19.5 19.8 20.1 20.6 20.9 21.2
开启增强层: 17.6 18.0 18.5 18.8 19.2 19.6 20.0 20.3 20.7 21.0
逐帧 |差异| = 0.23~0.29，整体 −1.2%
```

**结论**：增强层确实在改变画面（关闭时应当逐帧完全相同）。方向偏暗是因为该段
EL 残差偏负；残差本身围绕零点对称（实测 −13…+17），不同片段方向不同。

#### 验证过程中踩到的三个坑（都会伪装成"功能没生效"）

1. **`dv6.w` 写到了 `params[24]`（dv6.x）**，着色器读到的永远是 0 → 合成从未执行；
   而单测断言写的是同一个错槽位，所以测试一直绿。
2. **着色器从未部署**：只复制了 exe，`.dxil` 还是旧的（20596 vs 21808 字节）。
3. **播放位置记忆**：两次运行第二次会从上次位置继续，画面根本不同步，对比毫无意义。

#### 一个仍未定论的判断

离线统计 mkv block 得到 EL 覆盖率 51%（`111000` 节奏），但运行时
`decoded=1200 / 1238 packets` ≈ 97%，采样时也几乎每帧都有 EL。两者矛盾，
说明当初的 block 统计口径有问题（很可能把 BlockGroup 漏了）。**当前实现按
"这一包是否解出 EL 帧"判断**，这在残差诊断里表现正常，但覆盖率数字仍待重新统计。

#### 诊断开关（都默认关闭）

- `VEYRA_DOVI_P7_DUMP=1`：着色器把 EL 残差直接画成灰度（黑 = 该帧无残差），
  配合 `shaders/YuvToLinearRgb.hlsl` 里的 `p7Dump` 分支使用
- `VEYRA_DOVI_TRACE=1`：逐帧 RPU 与合成绑定计数
- 界面开关：专业页 → 杜比视界设置 → 合并增强层（默认关）



---


## 四、代码位置索引

| 功能 | 文件 |
|---|---|
| P5 转换 | `include/veyra/pipeline/DolbyVisionP5.h` |
| P7 增强层 | `include/veyra/pipeline/DolbyVisionP7.h` |
| BL+EL 合成 | `include/veyra/engine/DoviBlElCompose.h` |
| 曲线适配（L1 跟随）| `include/veyra/engine/DoviCurveAdapter.h` |
| 源侧路由 | `include/veyra/source/DolbyVision.h` |
| 色彩转换 shader | `shaders/YuvToLinearRgb.hlsl` |
| 验证工具 | `tools/dovi_p5_probe/` |

**当前路由行为**（`src/source/MediaFileSource.cpp`）：

```
[source-dovi] profile=8 level=7 compatibility=4 bl=true el=false rpuDeclared=true route=4 nativeOutput=0 rpuApplied=0
```

- `route=4` = HLG 基础层（P8 + compatibility=4 时）
- `nativeOutput=0` / `rpuApplied=0` = 不输出原生 DV、不应用 RPU
- 这是作者既有设计，P8 按 HLG HDR 播放

---
