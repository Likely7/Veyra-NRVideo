# GPU DIS 实验光流接入与验收记录

## 范围与存档

用户确认 PS5 清晰度已基本与 chiaki-ng 相当，本轮接入之前讨论的 GPU DIS 光流供切换比较，不继续改 PS5 码率/采样，也不虚构官方高质量模式接口。不加音频缓冲设置，不关机，不发布。

开工前将另一轮已完成、尚未提交的三个 P1 修复存档为 `3ae4d5c`，tag `checkpoint/gpu-dis-preintegration-2026-09-13`。这些修复不是本轮 GPU DIS 工作。施工分支 `codex/gpu-dis-integration`。

完成门槛：可在专业设置选择/恢复原后端，预设可保存，复用现有图与时序，构建通过；真实 GPU 方向、重置和下游 NR/FG 测试通过；许可与打包依赖完整。效果与性能不作先验承诺。

## 实现

1. 光流选择增加 `GPU DIS 光流 · FAST 实验`，枚举追加为 2；NVOF 默认、FidelityFX 和原预设含义不变。NVOF 档位、AMD 半分辨率仍只作用于各自后端。悬停说明解释计算资源竞争及实验边界。
2. 仅引入公开 DIS 源码子集，固定上游 `cb7523b5104fc914dc501767c3139b43c2067af7`。FAST、双向、两个槽；不引入整个工具箱、SDK、模型或 GPU Block。保留 Apache/BSD 许可与来源。着色器严格沿用上游宏及编译参数，未简化计算冒充原算法。
3. 现有源空间编码 RGB → GPU BT.709 R8 亮度 → DIS 双向运动 → 越界/非有限值拒绝、亮度匹配/往返一致性置信度 → 原共享 FlowAdapt → NR、SR、DLSSG/XeSS 消费者。矢量为 current→previous，单位为光流输入像素，消费者分别按目标尺寸缩放一次。
4. 不改变显示颜色、PS5 解码、音频、队列容量或播放循环。光流计时沿用 GpuStage::Flow，进入当前耗时卡和总增强处理耗时。DIS 单独记录实际执行计数，不冒充 NVOF 调用。
5. 共用图的首帧/seek/丢帧/切源/尺寸与设置切换 reset。reset 帧不复用旧运动。provider 不跨帧积累运动历史，只接收当前图的相邻 A/B。所有描述符按两个 parity 槽隔离，在原 upload fence 退休后重写；provider completion 指向下一次 ring 提交，关闭前排空队列。无额外逐 pass CPU 等待、常规 GPU→CPU 回读。
6. 通用 D3D12 计算，但此公开实现要求 DoublePrecisionFloatShaderOps；不支持时初始化明确失败，使用既有设置回滚。输入每轴暂限 32–4096（这是 DIS 光流输入限制，不是导入图片/视频上限）。未实测 AMD/Intel 显卡，不声称全卡验证完成。
7. 包脚本递归复制着色器子目录，保留此开源子集的许可。未运行发布/上传；NVIDIA 白名单不变。

## 本轮文件

- 后端：`include/veyra/guidance/GpuDisOpticalFlow.h`、`src/guidance/GpuDisOpticalFlow.cpp`。
- 图接法：`include/veyra/pipeline/EnhanceGraph.h`、`src/pipeline/EnhanceGraph.cpp`。
- 设置：`include/veyra/engine/EnhancementSettings.h`、`apps/veyra/SettingsWindow.cpp`、`apps/veyra/ui/SettingHelp.h`。
- 构建/着色器：`CMakeLists.txt`、`cmake/VeyraGpuDis.cmake`、`shaders/GpuDisLuma.hlsl`、`shaders/GpuDisValidate.hlsl`、`third_party/gpu-dis/`。
- 测试：`ExperimentalBackendTests.cpp`、`RepairFgTests.cpp`、`RepairPresetTests.cpp`。
- 文档/打包：本记录、WORKLOG、双语 README、BUILD、THIRD_PARTY_NOTICES、`scripts/package-portable.ps1`。

## 验证证据

构建目录 `out/remoteplay/product-repair`；本机 RTX 5070 / 616.56。日志目录 `logs/gpu-dis-20260913/`，构建日志 `out/gpu-dis-*-build*.log`。单次测试限 240 秒。

在 vcvars64 环境运行：

```text
cmake --build out/remoteplay/product-repair --target veyra veyra_experimental_backend_tests veyra_repair_preset_tests veyra_repair_fg_tests --parallel 6
veyra_experimental_backend_tests.exe dis <logs>/dis
veyra_experimental_backend_tests.exe dis1080 <logs>/dis1080
veyra_experimental_backend_tests.exe dis-xess <logs>/dis-xess
veyra_experimental_backend_tests.exe nvof1080 <logs>/nvof1080
veyra_repair_fg_tests.exe dis
veyra_repair_fg_tests.exe dis-sr
veyra_repair_preset_tests.exe <logs>/preset-fixed.v1
```

- DIS 640×360 与 1920×1080：48 个源帧，46 次双向计算；首帧和方向切换 reset 不运行旧帧光流，窗口 resize 包含在测试中；D3D12 debug error=0。640 样本运动中位数右移=(-1.97559,-0.01671)、左移=(1.90723,-0.00354)，符合 current→previous 方向。
- DIS＋XeSS：43 个 SDK 报告的生成帧，46 次 DIS，debug error=0。不是屏幕扫描帧率或视觉质量验收。
- DIS＋NR＋DLSS FG：12 源帧、11 生成帧全部通过非重复、非线性混合、位置与 PTS 检查，保留 lease 时拒绝覆盖。NR/DLSSG Create/Release result=0x1，seh=0。
- 再加入 RTX Video SR 到 3840×2160、NR 1920×1080：12 源帧、11 生成帧全部 contentValid=11；末帧位置误差约 1.32 像素。非真实 PS5 画质验收。
- 预设 roundtrip 最终通过（42 项既有迁移案例也通过）。初次失败是新增测试保存 DIS 后未清理，破坏后续旧断言的条目数；已修正测试清理并确认恢复读取仍等于保存的 DIS 设置。原失败日志与 results.json 保留；最终 additional-results.json 中 preset-fixed=0。
- 同一 1080p 随机纹理平移、同一设备、各 45 个已完成 GPU 光流计时：DIS 中位数 17.2824ms / 均值 17.2826ms；NVOF 中位数 1.15165ms / 均值 1.21748ms。**本公开 DIS 实现在该负载明显更慢，不应改为默认，也不能宣传提升性能。**这是单个合成用例，不能外推所有 GPU、游戏画质或整机延迟。

完整增量构建 `cmake --build out/remoteplay/product-repair --parallel 6` 115/115 完成。`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair` 23/23 通过，47.156 秒，证据 `logs/delivery/ca204954e1e64ee4aeddf7a41cf5f1e6/result.json`。该 gate 主要覆盖既有默认 NVOF 与媒体/导出路径；DIS 证据以以上专门测试为准。`git diff --check` 通过；包脚本 PowerShell 语法检查无错误。最终 exe SHA256=`114E00EDD266182BD2556FD1150C487FE3C4B6348112A63AA7706BFCE554BCFA`。patched avcodec-63.dll SHA256=`0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`，未退回旧 FFmpeg。桌面“Veyra PS5 测试版”快捷方式指向此构建。无独立 Reviewer；代码自查。尚未执行：真实 PS5/采集卡使用新后端、游戏视觉对比、长时压力、多 GPU、HDR 屏幕验收、正式便携包安装与上传。

用户操作：关闭并重新启动桌面 PS5 测试版 → 专业模式 → 光流与补帧 → 光流·运动估算 → GPU DIS。切换走原有图重建及回滚，可能短暂停顿；不需要重新配对 PS5。比较时固定 NR/SR/FG 参数及场景，结束可选回 NVIDIA NVOF。
