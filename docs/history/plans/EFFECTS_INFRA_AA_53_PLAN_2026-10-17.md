# 效果器扩展基础 E0 与 #53 空间抗锯齿：施工方案（2026-10-17）

分支 `claude/aa-53-20261017`（worktree `E:/项目/Veyra/worktrees/aa-53-20261017`），基线 main `26c0d61`。上位计划：`E:/项目/Veyra/reports/claude-plan-48-53-beauty-20261017/PLAN_48_53_BEAUTY_2026-10-17.md` 第 3、4 节。授权与用户原话见 `AGENTS.md` 顶部同日条目。

存档：`E:/项目/Veyra/archives/claude-aa-53-20261017/`（`start.json`：88 工作树 / 100 分支 / 主检出 64 个脏文件逐文件 SHA256；`source-before.bundle` verify 通过，76,295,892 字节，SHA256 `f22a36f998b4d0b7eaa181537fc68b2c32fb2a75229a508f0af65d4803041095`）。产物目录：`{build,tests,logs,tmp,reports,archives}/claude-aa-53-20261017`。

## 1. 范围

两件事，按顺序，分开提交：

1. **E0 效果器扩展基础**（抗锯齿与美颜共用，只在本会话做一次；美颜会话合入 `claude/effects-infra-20261017`）。
2. **#53 空间抗锯齿**：FXAA 中/高/极高、SMAA 低/中/高/极高、MLAA（阈值/强度）、实验“时域 SMAA”（T2x/4x NoJitter，默认关）。

不做：带抖动的 SMAA T2x/4x、会改分辨率的 Anime4K 3D AA 放大与 SGSR、整套 MagpieFX 通用运行时。

## 2. 现状（main `26c0d61`）要点

- 调色在图里有三类位置：①列表/节点首个调色融合进输入转换，列表额外调色在 `srcRgba_` 上逐个原位执行（`additionalColorInstances_`）；②节点模式的“槽”调色（SR 前/NR 前/NR 层间/链尾），由 `EnhanceGraphDesc::preSrColor/preNrColor/interNrColorTarget/tailColor` 按**调色参数序号**判定，`EnhanceGraph` 里各有一份维护计数、输出指针、视图和派发代码；③这些判定同时驱动 `validateFixedExecutionPlan` 的次序校验。
- `kMaxChainNodes=16`，`toChain()` 满载静默覆盖最后一格；`EffectType` 6 项；`GpuStage` 25 项，`GpuTimer` 用 32 位掩码；导出共享协议 9、预设库最高 13、链会话最高 8。

## 3. E0 设计

### 3.1 数据与格式
- `EffectType` 末尾追加 `AntiAliasing`(6)、`Beauty`(7)，既有数值不变；`effectTypeCount` 与所有定长数组（目录、计划资源计数、列表顺序表）随之 8 项。目录项新增 `hidden`：`Beauty` 在 E0 起即隐藏；`AntiAliasing` 在 E0 提交内隐藏，抗锯齿提交中放开。
- `kMaxChainNodes` 16→32；`toChain()` 满载不再覆盖，返回明确失败（`EffectChain::overflow` 标记，`validateChain` 拒绝）；`NodeGraphLayout` 等相关数组随常量增长。
- `AntiAliasSettings`（方式、质量档、MLAA 阈值/强度、时域实验与历史权重）进入 `ChainNode`/`EnhancementSettings`（实例 0 为 `antiAlias`，第 2 实例为 `additionalAntiAlias`，与调色同构），含校验、`activeVideoSettings`、`sameVideoConfiguration`、`requiresGraphRebuild`。
- 导出共享协议 9→10（`bytes=sizeof(Shared)` 校验保留）；预设库最高 v14（只有含新类型/超过 16 节点的内容写 v14，旧内容仍写最低够用版本）；链会话最高 v9。旧版读到更高版本明确拒绝并保留原文件；导出 worker 版本/字节不符拒绝。
- `GpuStage` 增加 `AaNode0-1`、`BeautyNode0-5`、`BeautyDetect`（共 34 项），`GpuTimer` 掩码 64 位；`enhancementProcessingMs` 把抗锯齿区间并入增强链耗时；旧 Win32 遥测名表同步。

### 3.2 插入槽通用化（图内）
- 新接口 `SlotPass`（`createViews/dispatch/reset`，同尺寸输入→输出的简单 pass）；`ColorGradeInstance` 实现它，`AntiAliasPass`、以后的美颜 pass 也实现它。
- `EnhanceGraphDesc` 用**按计划步骤**的 `placementOfStep(step)` 取代四个按调色序号的判定：返回 `Ingress / SourcePost / PreSr / PreNr / InterNr(target) / Tail`。判定条件逐字沿用旧四个函数，仅把“邻接的调色”推广为“邻接的简单 pass”（调色、抗锯齿、美颜）；旧函数保留为薄封装，`EffectChainTests` 里的既有断言继续成立。
- 列表模式（无 `runtimeNodeOrder`）按类型固定：调色=Ingress，抗锯齿=SourcePost，美颜=Tail。节点模式头部区域（第一个 SR/NR/保护之前）的前导调色仍走输入转换融合；其后的简单 pass 进 SourcePost，其间的调色（含参数 0）作为槽内节点调色，保证“按链顺序执行”。
- 图内每个槽一张 `SlotList`（按计划顺序的 `{pass, output, 计划步骤, 尺寸, 计时阶段}`），统一做资源创建、视图绑定、派发与计时；原调色四类派发/视图/计数/日志代码改为遍历槽列表，调色输出逐字节不变。
- 新槽：`SourcePost`（输入转换与列表额外调色之后、任何放大之前，源分辨率）供抗锯齿；`Tail` 在列表模式预留给美颜（NR 组含保护收尾之后、RTX HDR 之前，NR 先行时在 SR 之后）。
- 对比视图“原画”仍取抗锯齿之前的 `srcRgba_`；`workRgba_` 与 SR/NR 的“阶段输入”改走 `stageInput` 访问器。

### 3.3 界面接点
- `effectCatalog/effectAvailability` 新 id `aa`、`beauty`（隐藏项不进入菜单）；节点页编辑器分发、色标；列表处理顺序条；`nodeTimings`/`stageTimings`；四语言翻译。

## 4. 抗锯齿设计（E0 之后）

- 着色器：`shaders/MagpieCompat.hlsli`（`GetInputPt/GetInputSize/GetOutputSize/Rmp8x8/GetFrameCount` 与 PS 风格 pass→CS 包装）、`shaders/Fxaa.hlsli`、`shaders/Smaa.hlsli`、`shaders/Mlaa.hlsli`；CMake 增 `veyra_add_compute_shader_variant`（同源多预设，`-D` 宏）。`ComputePass::create` 增可选静态采样器。
- 颜色域：线性 FP16 工作纹理；先用一个小 pass 把线性颜色转成感知亮度纹理（SDR sRGB 曲线，HDR=PQ 归一化亮度，负值/超范围夹到 0 后编码）；边缘检测只读它；混合与邻域重采样仍作用在线性纹理，HDR 高光与负分量保留。
- `AntiAliasPass`：持有各变体 PSO、`edges/blend` 中间纹理、SMAA 查找纹理、时域历史；seek/切镜/断点/尺寸变化/重建重置历史。
- 位置：列表 `调色→抗锯齿→SR→NR→保护→(美颜)→RTX HDR→补帧`，NR 先行 `调色→抗锯齿→NR→保护→SR→…`；节点模式最多 2 个实例，RTX HDR 之后禁止。
- 界面：“画质”页最上方（超分辨率之上）新卡片“抗锯齿”；节点“添加节点”新增“抗锯齿”；预设/会话/继续上次/导出带参数。
- 许可：补回 FXAA 的 NVIDIA 2011 声明，保留 SMAA/MLAA MIT 声明与 Magpie GPLv3；`THIRD_PARTY_NOTICES.md` 逐文件记录来源提交、改动与 SHA256；SMAA 查找表 `AreaTex.dds`/`SearchTex.dds` 取自 Magpie 同一提交，随构建复制到 shaders 目录。

## 5. 测试与回归门槛

- **调色逐字节回归（E0 门槛）**：`veyra_effect_slot_regression_tests` 每配置一个进程渲染一帧，输出 8 位结果、FP16 线性输入、对比参考的 SHA-256。配置：列表默认（SR+NR+3 调色）、NR 先行、6 调色、双 NR+保护、节点 SR 前/NR 前/NR 层间/链尾各一组、混合（SR 先行与 NR 先行）、仅 SR 链尾、拆分 NR。在**未改产品代码**的基线上先录制，E0 后逐项对比必须全部相同；基线先自比对两次确认确定性。
- 单元：`veyra_effect_chain_tests`（链校验与上限、满载拒绝、计划与槽判定、会话/预设往返、旧版拒绝）、预设库/会话/导出协议测试、GpuTimer 掩码测试、UI 契约/翻译检查。
- 抗锯齿：合成锯齿图（多角度细线、文字/UI）边缘阶梯指标、平坦区几乎不变、HDR 无 NaN/截断/负值丢失；时域实验的重置与拖影；1080p/1440p/4K 各档 GPU 耗时实测。
- 约束：单测试 ≤300 秒、构建 ≤900 秒、每类配置只测一次、不做压力测试；GPU 计时前确认无他人 Veyra 进程。

## 6. 提交计划

1. `tests:` 切片回归 harness + 本方案 + AGENTS 授权（未改产品代码，用于录制基线）。
2. `E0:` 效果器扩展基础（回归通过后建 `claude/effects-infra-20261017` 指向它，不再移动；后续修正用 `[E0-fix]` 前缀单独提交并记入 WORKLOG）。
3. `AA:` 抗锯齿（着色器、AntiAliasPass、图接入、桥接/QML/翻译、预设/会话/导出、许可、测试与报告）。

## 7. 进度

### E0（2026-10-18，子会话额度用尽后由协调会话完成）
- 实现与 3.1/3.2 一致，差异：槽判定接口名为 `EnhanceGraphDesc::slotOfStep(step)`（返回 `PassSlot`），图内槽列表为 `sourcePostPasses_/preNrPasses_/preSrPasses_/interNrPasses_[]/tailPasses_`（`SlotPass`）；抗锯齿在本提交中由 `EnhanceGraphDesc::antiAliasExecutor=false` 拒绝执行、目录隐藏，美颜同样隐藏且不进入运行顺序。
- 设置接点：`describeAntiAlias()`（图描述、图片与分块路径）、`applySettings` 结构变化重建/阈值类为统一量、导出冻结计数校验、预览计算队列与自动 NR 池及最近图缓存的准入排除抗锯齿、“重置本页”清空抗锯齿、桥接目录与 `addEffect` 拒绝隐藏类型、整链预设替换的类型排序包含新类型。
- 32 节点使 `EffectChain` 22,416→45,584 字节；汇编清单统计的栈帧随之约翻倍（`QmlPlayerBridge` 构造 214→400 KB，`EngineController::run` 385 KB，`PresetLibrary::parse` 78→147 KB）。`veyra_qml_ui`（含界面、引擎与导出 worker 线程）与旧 `veyra` 链接 `/STACK:8388608`（只保留地址空间，按需提交）；已核对 PE 头 8,388,608。
- 旧 Win32 遥测阶段名表补齐：原表缺 `Fg6/Fg7` 两项（其后名称整体错位、末两项为空指针），现按枚举逐项对齐并以 `static_assert` 固定长度。
- 验证：`veyra_effect_chain_tests` 全部通过（新增 E0 断言 33 条：默认无新节点、溢出拒绝、双实例往返与校验、重建规则、列表位置、节点 SourcePost/PreNr/PreSr/InterNr/Tail 与混合槽、美颜不入图、21 节点链）；`veyra_preset_library_tests` 全部通过（新增 14 条：v14 独立文件且旧库字节不变、精确重载与应用、v13 头夹带新类型拒绝、未知新版本拒绝、18 节点链、会话 v9）；`veyra_nr_model_settings_tests` 26/26；`veyra_ui_i18n_tests` 0 失败；调色切片回归 `slot_regression.py app-e0b … --compare golden-baseline.json` **12/12 逐字节相同**（`tests/claude-aa-53-20261017/slot-e0-final.json`）。

### #53 抗锯齿（2026-10-18）
- 着色器（14 个 `.dxil`）：`AntiAlias.hlsli`/`AaLuma`（感知亮度）、`AaFxaa`（预设 3/4/5，补回 NVIDIA 2011 声明）、`Smaa.hlsli` + `AaSmaaEdges`/`AaSmaaWeights`（低/中/高/极高四个变体）/`AaSmaaBlend`、`AaTemporalResolve`（实验不抖动 T2x/4x）、`AaMlaa*` 三个 pass；CMake 新增 `veyra_add_compute_shader_variant`；`ComputePass::create` 可选静态采样器（点/双线性，clamp，默认关，既有根签名不变）。SMAA 查找表由 `scripts/generate-smaa-lookup-tables.py` 从 Magpie 同一提交的 DDS 生成 `src/pipeline/SmaaLookupTables.cpp`（校验载荷 SHA256），不新增随包文件。
- **与方案的一处差异**：HDR 边缘检测没有用 PQ。PQ 把 SDR 亮度区间的对比压缩约 3–4 倍（100→200 nit 的 PQ 差约 0.07，sRGB 相对白约 0.27），用这些算法按 SDR 调好的阈值会漏检大部分边缘。改为“相对参考白的 sRGB 传递，白以上以同斜率对数延伸”（SDR 白=1.0，HDR scRGB 以 203 nit 为白），SDR 与 HDR 片源在同一亮度段得到同样的检测，高光仍能形成边缘；所有混合仍在线性光完成。
- `AntiAliasPass`（`include/veyra/pipeline/AntiAliasPass.h`）：各方式的 pass、中间纹理、查找表上传、时域历史（seek/切镜/时间线断点/重建即丢弃）；方式/档位/时域模式改变重建，MLAA 阈值/强度与历史权重/拒绝为实时统一量（`applySettings`）。图内经 E0 槽列表执行：列表模式在源尺寸槽（输入转换之后、超分之前），节点模式按位置进入 SourcePost/PreNr/PreSr/InterNr/Tail；无编译计划的分块大图路径在源尺寸逐实例执行。GPU 计时 `AaNode0/1`，列表“GPU 阶段耗时”与节点耗时条显示。
- 界面：“画质”页最上方（超分辨率之上）“抗锯齿”卡片（方式、质量、MLAA 阈值/强度、实验时域累积与历史权重/拒绝，摘要显示当前方式），处理顺序条出现“抗锯齿”芯片并可点击定位；节点“添加节点”出现“抗锯齿”（最多 2 个），节点卡片用同一编辑组件 `AntiAliasEditor.qml`。桥接 `antiAliasEnabled/antiAliasParams/antiAliasParamsAt/setAntiAliasParameter(At)`；列表实例首次使用时插入在调色之后、超分之前。四语言翻译 22 条补齐。美颜仍隐藏。
- 许可：`THIRD_PARTY_NOTICES.md` 新增条目（Magpie d687997 文件对照、FXAA NVIDIA 声明、SMAA/MLAA MIT、DDS 与生成表 SHA256、Veyra 改动、未移植项）。

### 验收（2026-10-18，本机 RTX 5070，每项一次）
- 合成锯齿图（旋转矩形、圆、1 像素细线，640×360，与 16×16 超采样线性光参考比较）：所有方式平坦区与关闭时逐位相同（208,229 像素）；形状边缘平均误差（8 位）关闭 32.19 → FXAA 8.00、SMAA 低 7.01 / 中 7.31 / 高与极高 6.76、MLAA 10.47；细线 50.54 → FXAA 34.85、SMAA 低/中 10.58、高/极高 15.80、MLAA 39.13。
- HDR（P010 PQ BT.2020 → scRGB FP16）：FXAA/SMAA/MLAA 无 NaN/无穷，平坦高光、背景与 709 外绿色色块（2080 像素全部保持负分量）逐位不变，1000 nit 高光 12.547 保持，边缘被处理。
- 时域 T2x/4x：重置帧确定（历史被忽略），之后帧累积历史；历史权重 0 实时生效。MLAA 强度实时修改等于以该值重建。
- 节点（真实 NGX SR+NR）：SR→抗锯齿→NR 在 PreNr 槽 2560×1440 执行；抗锯齿→SR→NR→抗锯齿 两实例分别在源尺寸 640×360 与链尾 2560×1440 执行，GPU 计时两项都有数据。
- 分块大图（2900×1700，6 块，无编译计划）：MLAA 与整图逐位相同；SMAA 在 1,479 万字节中 620 字节相差 ≤3（双线性归一化坐标随纹理尺寸取整，分布均匀、不集中于接缝）。
- D3D12 调试层：SDR 四种、HDR SMAA、时域 4x 均无错误。
- 导出 worker（真实子进程）：冻结设置携带抗锯齿，worker 日志创建 `SMAA high` 并记录源尺寸派发，30 帧完成。
- 真实界面（隔离配置、自己的进程、播放合成片源）：39 项检查通过，列表开关/方式/档位/时域规则、节点添加与上限、模式切换后列表配置保留；播放中每次结构修改都重建并执行抗锯齿，阈值类修改不重建；截图 `logs/claude-aa-53-20261017/gui-*/pro-quality-anti-alias.png`。
- GPU 耗时（中位数，ms；稀疏合成内容 / 4K 密集细条纹最坏情况）：1080p FXAA 0.031、SMAA 0.074–0.075、时域 0.126、MLAA 0.073；1440p 0.051 / 0.124–0.126 / 0.215 / 0.124；4K 0.146 / 0.294–0.299 / 0.549 / 0.296；4K 密集 FXAA 0.355、SMAA 低中 0.547、高极高 0.726、时域 1.21–1.26、MLAA 0.536。测时前确认无其他 Veyra 进程。
- 回归：调色切片回归仍 12/12 逐字节相同；单元/GPU/QML/翻译各套件见下方“最终核对”。
- 顺带修复（main `26c0d61` 已有）：`TiledImageProcessor` 把整图编译的执行计划原样带进分块，图以“fixed execution plan output/count mismatch”拒绝，`veyra_image_dimension_tests` 的 6 个分块调色用例在 main 上全部失败。产品路径（`EngineControllerImage`）本来不带计划，分块现在明确丢弃整图计划、按列表基础顺序执行，6 个用例与整图逐位相同（maxError8=0）。
- 最终核对（2026-10-18，同一份构建 `tests/claude-aa-53-20261017/app-final2`，全新临时目录，日志 `logs/claude-aa-53-20261017/final-sweep-20261018-155900`）：15 套中 14 套通过——effect-chain、preset-library、nr-model-settings、ui-i18n、pipeline-contract（52 项）、color-grade-gpu 与 lifecycle、video-hdr、nr-temporal-gpu、QML quick、导出 aa、调色切片回归 12/12、抗锯齿 GPU（不含测时）、真实界面 39 项。image-dimension 的 11 个尺寸/分块用例全部通过，唯一失败的 NR 分块用例找不到 `runtime_local/config/ngx-local.json`：测试把源码根目录作为窄字符串编译进去，`E:/项目` 非 ASCII 路径无法解析（与本改动无关，ASCII 路径构建才能跑）。preset-library 在同一临时目录连跑第二次会失败（`testDlssSrModels` 留下 sidecar，main 同样如此），每次全新临时目录则通过。`veyra_amd_nr_graph_tests` 仅编译通过，未运行（需要假 AMD 运行库布置）。

## 8. 未验证项（预先声明）
AMD/Intel 实卡、用户 Switch 素材、长时间运行、光学延迟均不在本轮验证范围；本机 RTX 结果不外推其它硬件。
