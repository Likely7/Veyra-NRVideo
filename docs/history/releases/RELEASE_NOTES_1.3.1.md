# Veyra 1.3.1beta 测试包说明（隔离分支构建，未发布）

**这是给用户实机测试用的 beta 包，不是正式发布。** 构建来源：分支
`codex/framegen-fsr-dolby-20260916`（2026-09-16 已合并进 main，merge `24e7de7`；未推送、未创建 Release）。

## 帧生成

- **DLSS 6X（50 系）**：能力驱动倍率，UI 只提供该 GPU 真正支持的上限（Ada=2X、Blackwell 到 6X）；
  超上限请求被明确拒绝并提示，不再把补帧整档关掉。实测 6X：445 真实帧 / 2215 生成帧。
- **XeSS MFG 解锁（2X/3X/4X）**：移植 OptiScaler 五处字节补丁，进程内修改、逐项校验、退出回滚。
  实测 4X：563 真实 / 1677 生成，退出 `rolled back 5/5`。
- **XeSS 节奏修正（本次重点）**：>2X 时提供方把一 burst 的生成帧背靠背推出，观感是"挤一串 + 空一截"。
  现在把中间帧交给提供方自己的调度器。4K/30fps 素材 4X 实测同 burst 内间距
  mean≈8.30ms（目标 8.33ms，min 8.19 / max 8.39），`refused=0`。
- **修掉 XeSS 2X 静默失效**：以前选 2X 会因为"无需解锁"被判失败，整个 XeSS 会话退回原生呈现
  （等于没补帧）。现在 2X 走 Intel 原生路径，实测 280 真实 / 276 生成。
- **AMD FSR 帧生成（2X，厂商无关）**：新增 `--fg-fsr` 与设置项「AMD FSR 帧生成 · 2X」。
  实测 1080p 226/222、4K 559/555；生成帧经时间中点验证（方块中心 1041.5 vs 理论 1040）。
- **40 系 DLSS MFG 解锁**：移植 MFGAdaUnlock-RenoDx（MIT）：两处架构比较 + PTX 中点修正
  + 8 个 kernel 描述符重定向，全部进程内；只在 Ada（deviceId 0x2680-0x28FF）生效，
  **50 系完全不进这段代码**（实测 6X 不受影响）。

## 超分

- **AMD FSR 超分（3.1.x）**：图内 SR 新档位「AMD FSR 超分 · 3.1.x（N卡可用）」，
  支持非 NVIDIA 形状（AMD 光流 + FSR 超分）。实测 205-226 次 dispatch 0 失败；
  同帧 4K 对照平均绝对差 0.31/255（内容正确）。
  **注意**：本片段上它比直通缩放略软（梯度能量比 0.888），不宣称更清晰。

## 音频

- **杜比/DTS 位流解码兜底**：采集卡送 Dolby Atmos / Dolby Audio / DTS 位流时解码回 PCM
  进既有 5.1 管线（AC-3 / E-AC-3 / TrueHD / DTS / DTS-HD，含 IEC 61937 解框）。
  本地用真实 AC-3/E-AC-3 位流验证过 6 声道与幅度映射。
- **采集音频手动选择**（用户要求）：采集面板新增「采集音频（变更需重连）」：
  自动 / 强制线性 PCM / 位流优先。

## 其它

- 采集卡窗口标题修复（连接串不再当文件名显示）。
- 工具层看门狗与内存闸：防止验证层挂住把机器内存打爆。

## 测试重点（按显卡）

| 显卡 | 建议测试项 |
| --- | --- |
| RTX 50 | DLSS 6X、FSR 帧生成 2X、FSR 超分 |
| RTX 40 | DLSS MFG 解锁 3X/4X（本次重点）、XeSS 4X 节奏、FSR 全系 |
| RTX 30 | FSR 帧生成 2X（未在 30 系实机验证）、FSR 超分、NR 兼容选项 |
| 任意 | 杜比/DTS 位流采集（需支持位流的采集设备 + PS5 设 Dolby 输出） |

40 系验收命令与期望日志：

```powershell
.\Veyra.exe --fg-multiplier 4 --smoke-seconds 15 <视频>
# 期望：[ada-mfg] ... unlock applied=1 gates=2 descriptors=8 kernel=1
#       [graph] FG capability available=true multiFrameMax=5
```

## 已知边界

- FSR 帧生成上限就是 2X（3.1.x 提供方每个真实帧只生成一张），UI 只给 2X。
- FSR 超分：HDR 输出下不启用；光流分辨率必须与源一致，否则退回直通。
- 杜比：本机采集卡不提供位流，位流分支需在有 Dolby 输出+支持位流的设备上验收；
  TrueHD / DTS-HD 只验证了解码器存在，没有真实素材。
- 40 系解锁行为未在本机验证（本机只有 50 系）；Ada 缺硬件 flip metering 是否冻结需实机结论。
- XeSS 4X 未移植上游时间戳/截止时间层；若仍不匀，那是下一层工作。

## 包内容

- `Veyra.exe`、FFmpeg 运行库、VC 运行库、`shaders/`
- `runtime/experimental/`（NVIDIA 组件）、`runtime_local/intel/experimental/`（XeSS）、
  `runtime_local/amd/fidelityfx/`（AMD FidelityFX SDK 2.3.0，MIT，AMD 签名）
- 许可证、第三方 notices、`release-runtime-manifest.json`
