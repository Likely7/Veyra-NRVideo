# HDR / Dolby Vision PR 审查与待审批适配方案

## 建议决定

吸收 #13 的 P5 输入识别、FFmpeg RPU 元数据接线和回归探针，改用成熟实现的完整颜色参数与重塑算法；吸收 #14 的亮度设置保存修复和可关闭的场景亮度入口，重做其统计与状态接点。**两份 PR 均不直接合并。当前只完成静态审查、独立数值反例和方案，HDR 产品代码未施工。**

这不是“他全部做得不好”：#13 的方向正确，还补了缺失 RPU 拒绝、逐帧参数、seek 和参考解码对比；#14 修复了真实的设置回退与长场景滑杆不生效问题。但当前完整性、色度与时序问题会影响其他素材，不能靠一部电影正常就通过。

## 审查对象与证据边界

| PR | 固定 head | 最新实际范围 |
| --- | --- | --- |
| [#13](https://github.com/Likely7/Veyra-NRVideo/pull/13) | `f08934b8a535bb0ac085cd8f5689b9029b416726` | P5 IPT-PQ-C2 基层转 PQ/BT.2020，然后进入已有 HDR/SDR 路线；12 文件 |
| [#14](https://github.com/Likely7/Veyra-NRVideo/pull/14) | `57f2541b3173b5285661ddf6c448ffb2722f6433` | 原生 HDR 场景亮度管理、参数保存/UI、部分构建修复；26 文件 |

两者 merge-base 与当时 main 均为 `66cd3e50590766e5a654528ab61e491dc4d30c83`，不是彼此包含关系。2026-10-04 再查远端，head 未变、均 OPEN，无 status check。PR 创建日期为 2026-10-03（UTC）。固定 blob、diff、SHA 清单在 `E:/项目/Veyra/downloads/minimal-edge-hdr-review-20261004/`；没有 checkout/执行贡献者代码，没有代发评论。

#14 标题已过时：`e7879e0` 撤回 HDR→SDR→TrueHDR，`57f2541` 撤回 HDR→SDR 的四个自定义参数。最新版 TrueHDR 保持 SDR 输入，不按旧标题设计功能。原 SDK 的产品范围也为 SDR→HDR：[NVIDIA RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk)。

#13 文档报告 RTX 5080、一份 P5 影片的 1500 帧元数据扫描、PQ 域对 libplacebo 的单帧误差与短播放，以及 249/236/204 个本地检查。它有用，但不是本机验收、多影片验收或原生 DV 输出证明；SDR on/off 差异也不是 SDR 参考准确性证明。本轮没有运行其 GPU probe 或参考影片。

## 已确认的源代码问题

| 优先级 / 定位 | 问题与后果 | 适配方式 |
| --- | --- | --- |
| P1 #14 `src/pipeline/EnhanceGraph.cpp:2,1419,2199` | 引用并使用 `DolbyVisionP5.h`、常量和 `resolved.dolbyVisionP5`，但固定 head 没有该头文件、P5 参数成员和对应 shader 转换。单独 PR 的提交树不完整。作者本地成功不能证明干净 checkout 可构建。 | 先去掉半套 P5 接点，或在完整 P5 适配后显式接入同一接口；不得靠本地未跟踪文件补洞。 |
| P1 #13 `include/veyra/source/DolbyVisionRpu.h:164` / shader `DoviP5ToPqBt2020` | 只复制 RPU 矩阵，忽略 `ycc_to_rgb_offset[3]`；shader 固定色差减 0.5、亮度不减 offset。不能代表全部 RPU 的颜色合同。 | 按 RPU offset/normalization 处理。FFmpeg 明确提供这些字段；libplacebo 也显式消费，而不是固定中心值。 |
| P1 #13 `fitDolbyVisionP5Curve` | 64 个样点拟合为 `{1,sqrt(x),x}`，损失原始分段多项式；MMR 直接不支持。最新代码会拒绝不能描述全部分量的帧，不再静默当普通 YUV 显绿。 | 完整保留 pivots、分段 polynomial 与 MMR；优先移植 libplacebo 的成熟算法，固定提交与许可证。 |
| P1 #13 `dolbyVisionP5CurveIsPlaceholder` | 只凭曲线斜率近零就把常量映射改为 identity。将一个样本的黑场猜测写成普遍语义，会破坏有意的黑场/淡入。逐帧纯函数已解决先后播放不一致，但不证明该替换正确。 | 根据解码后的有效 RPU 和参考结果处理，不能凭系数猜 placeholder；加入真实淡入、黑场、重复元数据的对照。 |
| P1 #14 `HdrSceneMapping.h` / `analyzeLuma` | P010/P016 的原始高字节直接作为 full-range PQ bin；没有合法范围展开，且编码域 luma 不等于解码后的线性亮度。接 P5 后原始 IPT 统计更不能当 PQ 亮度。 | 在显式颜色解码及 DV 转换后的 HDR linear RGB 上计算亮度统计。 |
| P1 #14 `HdrSceneReduce.hlsl` / graph `reduceSrv` | shader 是 `Texture2D<float>`，硬解 array 分支绑定 `TEXTURE2DARRAY`；资源维度不匹配。 | 验证 2D / array-slice 两条合同，或移到统一非 array 的工作纹理，避免两种统计源。 |
| P1 #14 `EnhanceGraph.cpp:2032–2061` | 实际新增 64×36 浮点网格 GPU pass、9216 B 回读和 8 个槽。把上次槽的数据标成当前 PTS/frame，同时送入共有 cadence/scene 检测。存在统计延迟、场景切换错位及开关影响补帧 reset 的风险。 | GPU 统计只返回计数/参数，不回读图像网格；携带 epoch/frame/PTS/fence。保持原 cadence 合同，过期统计丢弃，不引入逐帧 CPU wait。 |
| P1 #14 状态生命周期 | 新增 gain、peak、ramp、lastPTS、measured 和 readback-valid 没随已有 open/seek/source/reset 清理。旧统计可能跨源/跨 seek 混入。 | 纳入统一 reset，源 epoch 改变即失效；重复帧不推进，seek 后与 fresh-open 对齐。 |
| P2 #14 `HdrSceneReduce.hlsl` | 硬解是先对每格 PQ 值平均再统计，软解是抽点统计。均值的分位数不等于像素分位数，细小高光会被抹掉。 | 使用相同域、相同采样策略的 GPU histogram/reduction；比较软硬解统计误差，单独检查星点、字幕、高光与黑边。 |
| P2 #14 `EnhanceGraph.cpp:1910` | transition 用 PTS，但 response 是固定每帧 alpha，24/60 fps 的响应不同。 | 用 PTS 差和毫秒时间常数；暂停、重复、跳帧、seek 有明确合同。 |
| P2 #14 资源/参数 | 即使开关关闭也创建统计资源、加载新 shader；用 float 通道按位装 gain/peak 参数，注释仍有已撤回的 EV/shoulder。 | 按能力/开关建资源，失败只回退该功能；显式参数 buffer，更新实现文档与 diagnostics。 |
| P2 #14 UI/模型 | 同一亮度组重复出现在 Pro“显示”和 Settings，Node 没有对应编辑器；默认 target=1000、范围400..4000没有显示器实测依据。“暗场自动抬”还可能改变作品有意的曝光。 | 默认关闭，命名为可选“场景亮度调整”；把显示器输出参数与内容效果分开，参数只有一个真源。 |

静态审查脚本 `scripts/acceptance/hdr-pr-review.py` 不运行 PR 代码。其独立 ST2084 反例在 `logs/minimal-edge-hdr-review-20261004/hdr-pr-static-findings.json`：合法范围 P010 中约 1000 nit 的中性像素，按该 8-bit 原始 proxy 会被读成约 **655 nit**；黑电平会被读成 **0.102 nit**，刚好超过其 0.1 nit 黑场排除线。数值用于证明 range 错误，不是新 tone mapper 的画质验收。

颜色参考：[FFmpeg AVDOVIColorMetadata](https://ffmpeg.org/doxygen/9.0/structAVDOVIColorMetadata.html)；[libplacebo 固定源 colorspace](https://github.com/haasn/libplacebo/blob/92b5ac6db79f4d680eb656692f7bf51e9606f42a/src/colorspace.c#L1921)；[完整 reshape 算法](https://github.com/haasn/libplacebo/blob/92b5ac6db79f4d680eb656692f7bf51e9606f42a/src/shaders/colorspace.c)。后者 LGPL-2.1-or-later，与 Veyra GPLv3 可兼容移植，施工时逐文件标注来源、固定 commit、改动与 notice，本轮未移入产品。

根签名：[D3D12 上限](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits)是整个签名64 DWORD。#13 的60个常量加descriptor tables已接近上限，但不能因此牺牲 RPU 精度；使用显式 CBV/SRV 传完整参数。不要把该 pass 的剩余预算说成所有 D3D12 程序只能用60个常量。

## 当前 AMD NR / VFG 分支的适配差异

1. 当前 `FrameGenerationBackend::Vfg`、`NrRuntime` AMD 值和质量参数不在 PR 基线。保留所有既有枚举ID、2X–8X/三档、AMD能力判断与不支持时的实际提示；不能用 PR 原文件覆盖当前实现。
2. 当前 legacy `PresetStore` v28 保存 VFG；#14 v28 却是读取已撤回 HDR→SDR 参数，二者含义冲突。当前 library v7 保存 VFG质量，#14 legacy v7读取撤回的HDR参数；当前 chain-session v5 保存VFG质量，#14 v5/v6保存HDR亮度/transition。要为统一格式分配新版本/标识并备份、迁移、验证旧文本，不能只把 `maxVersion` 取大。库的不同分叉 v7/v5不能仅凭版本号猜布局；判不明保留文件并明确报错，不静默覆盖。
3. `ExportJobManager.cpp` 的 Shared **整份复制 EnhancementSettings**，所以不能说“PR丢了导出亮度参数”。实际适配工作是更新当前 ABI8 的布局版本与 frozen 日志，验证父进程改设置不改变已启动任务；#14保持ABI7且检查sizeof，不是手写字段遗漏。
4. `YuvToLinearRgb` / EnhanceGraph 同时涉及当前 HDR保留、NR残差合成和 VFG 10bit 路线。P5在输入颜色解码处转换一次；不能在各后端重复 RPU、range或tone map。仍不能把 NR 的 SDR模型推理称为原生HDR推理。
5. VFG/HDR色度、RX9000 NR/HDR实卡尚未验收；scRGB与各补帧后端只按当前真实能力展示，不能用“8X已支持”推断全部HDR模式通过。

## 推荐的界面编排（等待审批）

### 三类状态分开

- **输入**：自动检测，只读“SDR / HDR10 / HLG / Dolby Vision P5/P7/P8”，位深与元数据状态。P5成功显示“DV P5 已转换”；缺有效RPU显示具体原因，不能只写一个 DV 绿色徽标。
- **HDR画面处理**：SDR显示“RTX HDR：SDR→HDR”开关与现有四参数；原生HDR显示“场景亮度调整（默认关）”。P5转换属于解码，没有让用户猜的转换开关。自动调整强度为主参数；时间响应与过渡折叠进高级。
- **实际输出**：只读“SDR / HDR10 / scRGB”，Windows HDR、当前显示器、实际回退原因。显示器峰值/校准属于此处，不能放进NR版本或把片源峰值当显示器峰值。

### 页面落位

| 页面 | 编排 |
| --- | --- |
| 专业·列表 | 效果区放唯一 HDR 处理卡，处在补帧前；“显示”页只给输出状态与“显示设置”入口，不重复四个场景滑杆。 |
| 节点 | 输入卡显示源格式/DV转换状态；唯一HDR处理节点使用列表同源编辑组件并保持补帧前；输出卡显示最终格式并链接显示设置。不能增加第二套亮度配置。 |
| 设置·显示 | Windows HDR/当前屏幕状态、HDR10/scRGB偏好、显示器峰值（自动读数标为报告值，允许手动校准）。内容效果留在链/预设，不放这里。当前203 nit参考白若无可调后端，仅显示真实固定值。 |
| 极简/全屏 | 仅显示简短输出徽标；详细“DV P5→HDR10”在源信息/专业页，避免把控制胶囊塞成参数面板。 |
| 导出 | 输出SDR/HDR10按实际编码能力列出，显示最后格式/位深/颜色标记。P5转HDR10后不复制原RPU而宣称导出了DV；不提供未实现的DV认证输出选项。 |

用户流程示例：

```
SDR文件       → NR/SR/颜色 → [RTX HDR，可选]          → FG → HDR10显示
HDR10/HLG     → 已有HDR颜色合同/增强 → [亮度调整，可选] → FG → HDR10或可用scRGB
DV P5 + RPU   → 精确转换到统一HDR工作域 → 同上          → FG → HDR10/SDR
DV P7兼容基层 → HDR10基层（明确未使用增强层）→ 同上
```

全局设备参数、列表/节点的内容参数、每个导出任务的输出合同分别有单一真源。预设可以保存创作效果，不能未经提示改当前显示器校准；迁移旧全局HDR输出偏好要保留用户当前值。上述新参数/状态接口缺口在审批后的功能分支补齐，不能先画能点但不生效的控件。

## 审批后的施工与验收顺序

1. 再存档、开独立 HDR 适配分支。先吸收小范围构建/保存修复，解决缺失 P5 依赖与版本分叉；当前黑边修复保持独立。
2. 适配完整P5颜色算法与参数buffer。严格比对libplacebo参考的多素材、offset、分段、多项式/MMR、黑场/淡入、seek/fresh-open、缺失RPU、软解/2D/array硬解。PQ与SDR分别做参考对比，普通SDR/HDR10/HLG/P7/P8不退化。
3. 独立接原生HDR亮度统计和状态机，保留关闭时原结果。测试合法/全范围、窄高光、字幕/黑边、24/30/60fps、pause/repeat/skip/seek、跨源/显示器、hist延迟；看实际GPU耗时与队列驻留，不用“没有全帧回读”冒充“没有新pass”。
4. 用同一组件接列表/节点/输出状态，验证参数保存、重启、旧预设与两种链模式、worker冻结与导出标记。能力不足时显示真实回退，不伪装HDR成功。
5. HDR10/scRGB/各FG、NR开关、TrueHDR SDR输入、DV转SDR和导出按本机真实能力验收。无法在本机验证的RX9000/其他型号、物理峰值和屏幕端到端效果单列，不能外推。
6. 提交完整构建、针对性颜色/时序/UI证据后，按用户本次审批范围整合。当前没有实施HDR、merge、push或Release。

审批对象：**上述分拆吸收、精确P5转换、默认关闭的场景亮度调整及三类界面编排。**
