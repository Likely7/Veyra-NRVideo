# NR 强度 5 与可选画面调控

2026-10-05，用户授权实施；起点本地 main `de18fc4f71843049dc7fd691898598c17af308f0`。
工作树 `E:/项目/Veyra/worktrees/nr-strength-protection-20261005`，分支 `codex/nr-strength-protection-20261005`。
开工 tag `checkpoint/pre-nr-strength-protection-20261005`，源码存档 `E:/项目/Veyra/archives/nr-strength-protection-20261005/source-before.bundle`，SHA256 `AFE2A7A4DC39A7BF7D230E65C0107F99D3F7F290FB00890B281F1D3443D1E286`。

## 行为合同

- 逐层 NR 总变化量范围 0–5；模型参数和其余残差分量范围不变。5 是一次 NR 输出与输入的残差外推，不是五次模型推理，也不保证等同多层 NR。
- 每层“画面调控”开关默认关闭；关闭时沿用现有残差算法，不暗中降低总变化量。开启可选择自动或手动；手动提供色相保护、色度保护、高光保护、局部压缩、时域稳定 0–1，切换模式不抹掉保存值。
- 自动保护按原图及当前像素的失控风险决定抑制程度，不使用逐帧自动曝光或额外 NR Evaluate。安全细节保留高强度，接近色域/高光边界的变化会受限制。不能承诺所有内容在完整局部增益 5 下都无失真。
- 借用 Magpie `27c5df91177a29b33be612e98274169f3d2fca49` 的 Oklab 色相投影、灰轴连续保护、局部软膝压缩与固定色相色域映射，记录 GPLv3 来源。Veyra 输入已是线性 FP16，不重复 sRGB 解码；HDR 使用 scRGB（1=80 nit）与 BT.2020 边界，不能套 SDR 0–1 裁切。
- 时域稳定复用现有有界历史、光流、原图补丁置信度及 reset；不增加未来帧或呈现等待。调控稳定和旧实验防闪独立保存，有效时域配置由共享描述生成；参数变化/reset 拒绝旧历史。无可信运动/切镜/间隔断点时立即旁路历史。
- 三入口与列表/节点共享实现；设置、预设、复制、重置、会话恢复、图片/视频导出均保留参数。旧预设默认调控关闭，只有新功能/强度>2 才升级文件 schema。

## 允许文件边界

`AGENTS.md`、本方案/执行记录/WORKLOG、README 与来源 notice；`include/veyra/engine/EnhancementSettings.h`、`GraphDescription.h`、新增调控 codec；`src/engine/PresetStore.cpp`、`PresetLibrary.cpp`；`include/veyra/pipeline/NrTemporalPass.h`、`src/pipeline/NrTemporalPass.cpp`、`EnhanceGraph.cpp`；`shaders/NrResidualComposite.hlsl`、`NrTemporal.hlsl`、新增 `NrCorrection.hlsli`；`src/ui/QmlPlayerBridge.cpp`、`qml/Veyra/NrLayerEditor.qml`、`i18n/catalog.json`；`CMakeLists.txt`、`cmake/VeyraShaders.cmake`；本功能定向测试及 `scripts/acceptance/nr-protection-*`。如发现必要新接点，先记录证据再扩充本方案/guard；不修改历史 baseline。

## 验收与交付

1. 构建生产程序、实际 HLSL 与相关预设/链/颜色/时域/QML 测试。每进程测试≤300s、构建≤900s，正常负载；日志/临时目录都在本轮 E 盘。
2. 实际 GPU shader：调控关闭与起点 shader 等价；0/1/2/5、零残差、保护区；灰轴/肤色/饱和彩色、近黑/高光、越界候选、signed HDR、有限值；自动与手动边界和零保护；运动/切镜/seek/reset、交替亮度与色度稳定，不以减闪掩盖拖影。
3. 真实 NR 本机短测，核对 Evaluate 次数、设置/预设/会话、列表/节点 UI、暂停刷新、图片/视频导出、继承修复。RTX 本机证据不代替 20/30/40/AMD 实卡或用户主观画质验收；不以像素数值检查宣称所有视频正常。
4. 本地完整 NVIDIA 候选、组件 hash/manifest、源码存档与日志。运行库沿用已审计本地候选，不修改原件、不进源码 Git。无新的合并/推送/Release。

## 当前进度

- 已建隔离工作树、tag 与已验证源码 bundle。
- README 范围包含中英文入口 `README.md`、`README_CN.md`、`README_EN.md`；仅补充本地功能状态及使用方法，已同步本轮 guard 白名单，不改历史基线。
- 实施与验收记录：`docs/NR_STRENGTH_PROTECTION_EXECUTION_2026-10-05.md`。

## 真人反馈续修：风格 1/2 色偏与手动参数（2026-10-05）

用户在首轮候选 `9eb3b1c` 反馈 NVIDIA 原版、强度 5，风格 1/2 色偏比风格 0 明显；需要更丰富且可见生效的调控参数。继续同一隔离分支，首轮完整包不覆盖；存档 tag `checkpoint/pre-nr-style-controls-20261005`。三个风格当前都调用同一纠偏 shader，接近灰轴的色相保护与固定色度预算存在遗漏风险，先以原版 runtime=3 做同帧 source/raw/auto 对照再决定自动策略。新增参数必须具有独立作用、真实 GPU 响应与列表/节点/预设/会话/导出贯通；旧 v29/v8 的五项手动配置可读，新增手动参数默认关闭以保持旧配置含义。沿用本方案文件范围与不可变 guard，不改 DLL、main、其他分支、发布包。

续修合同：自动按模型风格分别选择保护系数，不替换模型风格、不降低保存的总强度 5。新增中性色保护、原图色彩保留（保持亮度变化后的源色相/饱和度）、亮度保持和暗部保护；手动共九项，明确各项适用条件。自动力度 0–1 保存，0 无时域历史且保留原始残差，非零力度可实时调整。手动可显式采用当前风格的自动数值（含已选力度），自动／手动正常切换保留各自保存值。v29/v8 原五项手动配置读取时新字段为 0、自动力度为 1；仅新增字段非默认才写 v30/v9。新完整本地候选 `Veyra-2.0.3-nr-controls2-NVIDIA-win64-portable`；来源与运行库沿用，重验 libass 和实际 NVENC。续修前 bundle `archives/nr-strength-protection-20261005/style-controls/source-before.bundle`，SHA256 `19703EFD383E2B012AF6FFDE0663395CB2276D0A9FCA3D524DD6AE16D2EBF42C`。
