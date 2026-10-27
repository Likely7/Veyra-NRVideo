# FSR4 画质失败与 HDR 预览状态

## 最终状态：用户终止并回退

用户后来授权编译、不打包，因此曾继续验证尺寸布局修正；随后明确要求「停止吧，FSR4.1回退掉吧」，本次按最终决定撤回 UI、provider、格式适配和实验脚本。下文「未编译」等描述仅代表前一阶段，不代表后续没有编译。研究不再继续，HDR 状态提示独立保留。

取消前候选在 RTX 5070 上做过 1080p/1440p/4K 和真实影片的 GPU 验证；固定图案 4K MAE 从 16.34 降到 11.73，但纹理过冲、亮度误差和用户指出的整体画质问题不能据此判为通过。候选没有获得用户画质验收，也未发布；RTX 30/40 未测试。

历史证据：`E:/项目/Veyra/tests/fsr41-quality-20260918`；候选日志：`E:/项目/Veyra/logs/fsr41-nvidia-20260918/quality-final-build.log`。外部候选 provider 与已撤回测试包只作为历史产物保留，当前程序不再加载它们。回退结果以 WORKLOG 最新记录为准。

## 当前结论

用户试用界面测试包后明确反馈 FSR4「明显变糊、细节丢失」，本轮画质验收失败。用户要求不再构建；没有生成新 DLL、EXE 或测试包。现有 API、非空图像和持续运行结果保留为调用/稳定性证据，不能当作画质验收。

工作区：`E:/项目/Veyra/worktrees/fsr41-nvidia-20260918`，分支 `codex/fsr41-nvidia-20260918`。保留此前未提交的 UI 接入修改，不合并、不发布。

## 已确认的 FSR 尺寸缺陷

上游 `int3rrobang/fsr4-int8-reverse-engineering` 固定提交 `88635b94083965a7c3b5f64e099808b8ba2ce576`；本轮查询 GitHub HEAD 仍为此提交。源码在 `E:/项目/Veyra/deps/fsr41-nvidia-20260918`。

- `bench/tools/build_411_dll.py`：中间工作尺寸固定 960x540，输出工作类固定 1920x1080，17 项 tensor 尺寸由此生成。
- `bench/provider411/provider.h`：scratch 固定 20,880,256 字节。
- `bench/provider411/provider.cpp`：中间层使用 manifest 的固定 Dispatch 数量。
- `bench/shaders/reimpl411/pass0_post.hlsl`：行距 15392、边界 961/541 等常量固定。其他层也含固定偏移与布局。
- `bench/provider411/frame.cpp`：PRE/POST 的输出尺寸、头部常量和 dispatch 可变，但没有同步改变上述中间计算布局。
- 上游 `docs/PROVIDER_411_INTEGRATION.md` 明确说明 P4.8 仅修改头部尺寸，tensor 和中间 dispatch 仍固定；不同工作类需要重新推导或对应捕获验证。

Veyra 将实际 4K 输出尺寸交给此 provider，不能据返回成功认为模型支持了 4K。这是明确的尺寸适配缺口。**不能进一步推断输入必然被缩成 540p，也不能认定这已解释全部模糊。** 本轮只补充非 1080p 请求的诊断日志，没有伪装为算法修复，也没有新增加载门禁。

## 已有图像的补充检查

执行（未构建）：

```powershell
./tools/image_check/compare_sr.ps1 -Reference E:/项目/Veyra/tests/fsr41-nvidia-20260918/media-baseline/fsr-frame.png -Candidate E:/项目/Veyra/tests/fsr41-nvidia-20260918/media-fsr4/fsr-frame.png
```

同一影片帧、输出均 1920x1080：平均亮度 21.27 / 21.03，亮度 MAE 0.40/255，脚本边缘梯度 2.100 / 2.045，比值 0.974。此脚本梯度取蓝通道，只是局部清晰度线索，不是画质评分；增加噪点或锐化也能提高它。单帧结果不能替代用户的动态体验。

已有 `tests/fsr41-nvidia-20260918/4k/fsr-frame.png` 棋盘可见不均匀的边缘扰动；原测试仍通过，说明阈值不足。`Fsr41VideoTests` 对影片只要求亮度最大值大于最小值，对合成图的 MAE 阈值也过宽，不能承担细节保留验收。

## 修复路线与验收

1. 先回到模型布局匹配的 720p -> 1080p，固定同一 PTS 和输出尺寸，以未增强、现有 FSR3、FSR4 作对照；分离尺寸错误与时序细节损失。
2. 推导所有层的 tensor 尺寸、scratch 偏移/行距、dispatch 和 shader 变体，统一支持目标输出。优先使用许可明确的上游 host/selector 实现；部分层目前只有固定官方容器，不可仅改几个宽高常量便宣称通用。分块方案需要独立的重叠区域、运动坐标和历史验证，不能直接无缝拼接假定成立。
3. 尺寸一致后检查颜色空间、运动单位/符号、重投影和历史权重。现有零 jitter、估计光流、常量深度、固定曝光是视频输入局限；本轮未证实其中哪项造成模糊。不向真实视频虚报不存在的抖动采样。
4. 使用文字、细线、纹理、运动遮挡和切镜序列，在相同尺寸下检查细节保留、拖影和闪烁；分别验证中心/四角。指标只能辅助，不能以非空图像、锐化后的梯度或成功 counter 放行。

**FSR4 画质尚未修复。** 完整模型尺寸修复和时序 A/B 需要后续实现、构建与实际 GPU 图像验证。本轮遵从不构建要求，不将现有包重新标为修复包。

## HDR 的实际行为与源码修改

`EnhancementSettings::useHdrPreview` 要求 HDR 显示处于启用状态；`EnhanceGraphDesc::convertVideoHdr` 要求 HDR 输出。因此 SDR 显示环境下，预览根本不执行 TrueHDR 转换，保留 SDR；不是先转 HDR 再映射回来。HDR 离屏导出是独立输出路径，不依赖显示器支持。

SDR 显示器无法提供真正 HDR 的峰值亮度和动态范围。HDR 显得灰通常是 PQ/色彩空间被错误解释或缺少正确色调映射，不是功能成功的标志。

源码新增 `PlayerSnapshot::videoHdrStatus`，由引擎实际处理图状态提供；设置页在 RTX Video HDR 开关下显示运行、SDR 预览未转换、原生 HDR 无需转换、关闭或等待状态。独立于可能很长的 LUT 告警，预留两行并下移滑块，避免状态与参数重叠。**未编译、未进行新界面验证，当前测试窗口不会获得此修改。**

## 本轮验证与产物

执行源码阅读、上游 HEAD 查询、已有 PNG 目视/数值比较、`git diff --check`。没有新的 Create/Evaluate、GPU 回归、构建或打包；没有新增二进制、SDK、模型或图像进入 Git。新诊断日志将在以后编译运行时产生，本轮没有伪造运行日志。

已有 UI 包仍位于 `E:/项目/Veyra/test-packages/fsr41-ui-20260918`，既有证据位于 `E:/项目/Veyra/{logs,tests}/fsr41-ui-20260918` 与 `E:/项目/Veyra/{logs,tests}/fsr41-nvidia-20260918`；保留以复现失败，本轮没有创建额外中间包。下一项任务是修复模型尺寸合同并建立动态画质 A/B，不能继续按已通过候选交付。
