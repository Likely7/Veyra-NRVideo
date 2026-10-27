# 色彩页 P1 交付报告（2026-09-17）

隔离分支：`codex/color-tab-p1-20260917`（**未合并 main，未发布**，等人工验收）
存档点：`checkpoint/color-p1-archive-20260917`（= `65e69b3`）
方案：`docs/COLOR_TAB_LR_FEATURE_PLAN_2026-09-17.md`（v4）
执行记录：`docs/WORKLOG.md`（按 T0→T6 逐条，含命令与失败修复）

---

## 1. 交付内容

| 里程碑 | 状态 | 证据 |
| --- | --- | --- |
| T0 删预设页/日常预设入口 | 完成 | 代码级删除；`veyra_ui_contract_tests` exit 0 |
| T1 NR 剔除区改名 + 羽化 | 完成 | 剔除区羽化 0..64；`--smoke-settings` 步骤断言羽化到引擎 |
| T2a `ColorSettings` + schema v18 | 完成 | `veyra_repair_preset_tests` 全通过（含 v17 迁移用例） |
| T2b 源头单一融合调色 pass + 总开关 | 完成 | GPU 契约 10 项（关开关/中性逐字节等于旧链路） |
| T3 色彩页 UI 框架 | 完成 | 风琴折叠 + 滑块/数值框 + 一键还原/撤销 + 折叠位落 `ui-preferences.v1` v4 |
| T4 面板填充（亮/颜色/曲线/混色器/分级/校准） | 完成 | 23 项 CPU 烘焙断言 + GPU 逐项行为 |
| T5 LUT（.cube）导入 + 命名色彩预设 + `.vpcolor` | 完成 | `veyra_color_lut_tests` / `veyra_color_look_tests` / 烟测步骤 51–57 |
| T6 HDR 标准处理 | **部分完成** | 线性域调色 + LUT 输入空间拒绝已 GPU 验证；MaxCLL/MaxFALL 重算**未做**（见 §5） |
| T6 三入口一致性 | 完成 | `scripts/acceptance/color-three-entry-consistency.ps1` PASS（§4） |
| T6 全量回归与交付报告 | 完成 | §6 + 本文件 |

链路（v4）：`源 → 线性 FP16 →【调色：白平衡/校准 3×3 + 曝光 + 对数域曲线 + 混色器 + 分级 + 3D LUT】→ SR → NR → 残差 → 补帧 → blit → 字幕 → sink`。
调色与 ingest 融合在**同一次 dispatch**，总开关关闭时不建表、不采样、零开销。

## 2. 本轮顺手修掉的真实缺陷（都是验收过程抓出来的，不是测试自己闹脾气）

1. **载入 `.cube` 会概率性闪退**（严重）：`EnhanceGraph` 里 LUT 载入成功后的日志用
   `std::string(lutNameString().begin(), lutNameString().end())`，两次调用返回**两个不同的临时
   wstring**，区间长度是垃圾值 → 越界读 → `0xC0000409` fail-fast。GPU 合同测试 8 次里崩 2–3 次。
   现已改为一次性 UTF-8 转换。**任何用户只要在色彩页选一个 LUT 就可能中招，这是本轮最有价值的修复。**
2. **`.cube` 上传的 staging 行距不满足 D3D12 的 256 字节对齐**（除 16 的倍数外的 LUT 尺寸
   ——2/3/17/33 这些全都中）→ 现在按 256 对齐并逐行写入。
3. **导出（视频）与大图导出（图片）完全不应用调色**：`VideoExportJob` / `EngineControllerImage`
   构造 `EnhanceGraphDesc` 时漏了 `color` 字段，预览有调色、导出没有。三入口一致性验收直接抓到
   （修前 screenshot-vs-export 只有 19.50 dB，修后 49.29 dB）。
4. **色彩页的预设下拉与 LUT 下拉永远是空的**：用了 `SendDlgItemMessageW(window,…)`，而控件挂在
   滚动面板 `body` 上，消息静默失败、`CB_GETCURSEL` 返回 -1。
5. **保存预设后选中项被清空**：`refreshColourLooks()` 固定重置到“（未选择预设）”，保存完立刻点
   “应用”必然落空；现在保存/导入后自动选中刚写入的那一项。
6. **`ui-preferences.v1` v3 读不回来**（写 6 读 5）+ NR 剔除区 inspector 范围写错（T1 时修的）。

## 3. 调色 GPU 成本（方案要求 `gpuGradeP95Ms` + 基线对比）

融合派发无法在 GPU 上单独给调色打点（要独立时间戳必须拆第二个 dispatch，违反 v4“单一融合 pass”）。
因此用**同一素材、同一会话、只切换总开关**的 A/B，量 `GpuStage::Color`（= 转换 + 调色）的 p95：

命令：`veyra.exe <clip> --smoke-seconds 20 [--color-grade=1.0]`，取稳定后的 8/7 个采样点。

| 素材 | 关闭 | +1 EV | 差值 | 方案预算 | 结论 |
| --- | --- | --- | --- | --- | --- |
| 1920×1080 60fps | 0.034 ms | 0.175 ms | **+0.141 ms** | ≤0.15 ms | 通过 |
| 3840×2160 30fps | 0.308 ms | 0.639 ms | **+0.331 ms** | ≤0.35 ms | 通过 |

原始日志：`logs/color/grade-ab/{1080p,2160p}-{off,on}.log`。
结论按实：这是**同一次 dispatch 的增量**，不是独立 pass；`player-timing` 因此没有新增
`gpuGradeP95Ms` 字段，改为在本文与 WORKLOG 里给出对比数据。

## 4. 三入口一致性（预览/截图/导出）

`scripts/acceptance/color-three-entry-consistency.ps1`（静态图案，四轮运行：截图关/开、导出关/开）：

| 指标 | 数值 | 门槛 | 结果 |
| --- | --- | --- | --- |
| 截图 vs 导出帧（开调色） | **49.29 dB** | ≥36 dB | 通过 |
| 截图 vs 导出帧（关调色） | **50.30 dB** | ≥36 dB | 通过 |
| 调色前后差异（截图路径） | 19.66 dB | <30 dB = 肉眼可见 | 通过 |
| 调色前后差异（导出路径） | 18.43 dB | <30 dB = 肉眼可见 | 通过 |

结果 JSON：`logs/color/three-entry/ec292b53eb8048648f8887911f50a52d/result.json`。
预览本身（图的输出像素）由 `veyra_color_grade_gpu_tests` 逐字节断言；截图走同一个 sink。

## 5. HDR 与未完成项

已完成（`veyra_hdr_color_tests`，独立 BT.2390 参考实现）：

- **+1 EV = 场景线性 ×2，且发生在 tone mapping 之前**：`linearError=0.0012`（PQ）/`1.04e-05`（HLG），阈值 0.014；
- **LUT 输入空间不匹配必须拒绝**：HDR 内容选 sRGB、SDR 内容选 PQ 都会被拒（`colorLutNotice()` 非空、
  像素与无 LUT 逐字节一致），并且通过 `colorStatus` 在状态面板可见，不只写日志。

**HDR 导出的静态元数据（部分完成，按方案 §5.3 的兜底条款实现）**

已做：HDR 导出会把源文件的 **MaxCLL / MaxFALL 原样写进 MP4 的 `clli` box**（走
`videoStream->codecpar->coded_side_data`，由 FFmpeg movenc 落盘），调色开启时额外写一条
`[color-export] … carried over from the source and NOT recomputed … (marked as not updated)` 警告。
证据（本机真跑，命令与结果在 WORKLOG）：

- 源（x265 写的 SEI）：`maxCLL=1000 maxFALL=400`；
- 导出：`side_data_type="Content light level metadata", max_content=1000, max_average=400`，
  同时保留 PQ/BT.2020/HEVC Main10；
- 带调色导出：同一条元数据 + 警告，且画面确实被调色（与不调色导出的同一帧 PSNR 5.39 dB）。

**未完成（如实列出，不冒充通过）**

1. **导出时“重算” MaxCLL / MaxFALL**：需要“写 header 前先跑一遍全片直方图”的两遍流程
   （再加 mastering-display `mdcv` box），本轮只做了“原样带上 + 明确标注未更新”的兜底，没有重算。
2. ~~分组眼睛（每组 bypass）、点曲线编辑器、黑白混色器开关~~ —— **已在“专业型 UI 返工”中补齐**
   （见 §5.1）：分组 bypass 是真 bypass（烘焙时把该组按中性处理，数值保留），像素级验证过；
   曲线是真编辑器（网格/拖点/通道/拉平）；黑白混色器挂在混色器的“校正”下拉里。
3. 预设导出 `.vpcolor` 只带参数与 LUT **文件名**，不带 LUT 文件本体（换机导入需自行拷 `.cube`）。
4. 驱动叠加（NVIDIA App AI 插帧）与调色的相互影响未测。

### 5.1 专业型 UI 返工（2026-09-17 用户反馈后新增，五批）

用户反馈“全是滑条、没有分类、难用”，据此按 Lightroom/专业调色面板重做了色彩页：

1. **渐变轨道 + 数值文本**：色温（蓝↔黄）、色调（绿↔品红）、自然饱和度/饱和度（蓝→绿→黄→红）、
   色相行（彩虹）用渐变轨道；每行左标签、右数值、下滑轨；滑块**双击复位 / Home 复位 / Alt 拖动精修**；
2. **按住看原图 / 复制 / 粘贴 / 撤销 / 重做**：按住看原图用“中性调色”实现（不重建管线），
   撤销栈 32 步，拖动期间不写历史、松手压一条；
3. **颜色分级四色轮**：自绘色相/饱和度圆盘 + 明亮度条 + `H/S/L` 读数，2×2 网格；
4. **混色器专业布局**：“校正”下拉（色相/饱和度/明亮度/黑白）+ 8 色点条，一次只显示选中色系的滑块
   （选“黑白”即黑白混色器开关）；
5. **真曲线编辑器**：网格画布 + 可拖控制点 + RGB/R/G/B 通道 + 拉平，预览与烘焙同源；
6. **分组眼睛 bypass**（真 bypass，schema v19）+ 面板默认宽度 320 → 392。

smoke 断言（全部 exit 0）：渐变色轨/复位/撤销流程、`colour wheel drag hue=120.1 saturation=79.8`、
`luminance bar=-50.0`、`double-click reset pass=true`、`mixer mode=3 blackWhiteAsked=1 applied=1`、
`curve point added count=3 x=0.50 y=0.75 pass=true`、`curve flatten ... pass=true`、
`section eye bypass pass=true mask=1` → `restore pass=true mask=0`；
GPU：`bypassing the 亮 group renders exactly like the ungraded reference`（逐字节）；
截图：`logs/color/ui-preview-wheels2.png`、`ui-preview-mixer.png`、`ui-preview-curve4.png`、`ui-preview-eyes2.png`。

**尚未做的可选项**：分区图标、字号与间距的进一步抛光。

## 6. 回归与闸门

- `scripts/gates/delivery.ps1`（UI 返工后的最终代码上再次重跑）→ **DELIVERY SHORT GATE PASS**
  （`logs/delivery/e41b66176a824c2990bbe28f845cb369/result.json`，含 `color-page`、`player-sync`、
  `controls` 等用例）；
- 52 个测试程序逐个单跑：**45 个 exit 0**；7 个未执行，原因是需要真实硬件/素材或运行库放置策略
  （`capture_tests` 采集卡、`live_presentation_tests`/`ps5_*`/`hw_import_image` PS5 素材、
  `nr_ampere_tests` 被 `nr-adapter` 的“仅允许 staging/experimental 目录”策略拒绝、
  `wasapi_input_tests` 真实端点 —— 其 `--offline` 分支已通过）；
- 色彩专项：`veyra_color_grade_tests`（23 项）、`veyra_color_grade_gpu_tests`（连续 8 次全过）、
  `veyra_color_lut_tests`、`veyra_color_look_tests`、`veyra_hdr_color_tests`、
  `veyra_ui_contract_tests`、`veyra_repair_preset_tests`、`veyra_repair_contract_tests`（191/0）。

## 7. 人工验收建议（按这个顺序点一遍）

1. 打开任意视频 → 专业模式 → **色彩**页：开总开关，拖曝光/对比度/饱和度，看画面即时变化；
2. **曲线**组拉一个点、**颜色分级**转一个色轮、**校准**改一个滑块；
3. 一键还原 → 撤销还原（应回到还原前数值）；
4. 保存一个命名预设 → 改乱数值 → 在列表里选它 → 应用（应恢复）；导出 `.vpcolor` → 删除 → 导入（应回来）；
5. LUT 组：导入一个 `.cube` → 选它 → 调强度 → 换输入空间（选错应有拒绝提示）；
5b. **眼睛**：点分组标题右侧的眼睛，画面里这一组应立刻失效（数值还在），再点恢复；
5c. **色轮/曲线**：颜色分级四个色轮拖动、双击复位；曲线画布加点/拖点/双击删点/拉平；
5d. **混色器**：校正下拉切到“黑白”，画面转单色，8 色点条选不同色系时下方滑块跟着换；
6. **关总开关**：画面应与调色前完全一致（延迟回到零开销）；
7. 导出一段视频（带调色）→ 与预览/截图对比外观一致；
8. HDR 素材（PQ/HLG）：调色后应有正常亮度提升，不出现死白或颜色反转。

验收通过后才可以合并 main；发布另需授权。
