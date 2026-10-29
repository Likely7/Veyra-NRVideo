# HDR / 杜比视界测试素材清单（2026-10-29）

本地留存的样本，以及每个样本已经验证到哪一步。做回归时照这张表跑一遍，五种 HDR
格式全都能覆盖，不用再翻聊天记录确认“这个片子当时测出什么”。

## 素材

| 文件 | 格式 | 实测结论 |
|---|---|---|
| `D:\share\星期三\www.UIndex.org - Wednesday S02E01 ... DV H.265-HHWEB\*.mkv` | **P5**（IPT-PQ-C2，Netflix WEB-DL） | 转换正确、画面正常。RPU 提供真实 `ycc_to_rgb` / `rgb_to_lms` 矩阵，日志为 `profile=5 ... route=2` + `converted as IPT-PQ-C2` |
| `D:\share\Despicable Me.2010.2160p...mkv` | **P7 单轨双层 FEL**（真实 UHD 盘 remux） | EL 覆盖 **100%**、配对放弃 **0** 次、曲线可评估 **98.5%**。不闪。**首选画质验收素材** |
| `D:\share\Dolby Vision\FEL TEST ST DL P7 CMV4.0 4000nits V4.mkv` | P7 + **CM 4.0** 元数据 | 曲线全 identity，`refused 0%`；残差 −329…359（真 FEL 内容）。体积小，启动快 |
| `D:\share\Dolby Vision\FEL RPU_EL测试.mkv` | P7 **曲线切换** | identity ↔ 8 段交替，`refused 1.3%`；EL 覆盖 100% |
| `D:\share\Dolby Vision\[Dolby] ... FEL Test {4K DoVi HDR10 & AAC 2.0}.mkv` | P7 **稀疏 EL** | **EL 只有 50.7%**，一半帧没增强层 → 人像高细节处会闪。**这是片源设计**（测播放器的稀疏处理），不是缺陷 |
| `D:\share\Dolby Vision\[Dolby] ... FEL Control {4K HDR10 & AAC 2.0}.mkv` | **纯 HDR10**（无 DV） | 对照组：同一段画面但没有 DV 元数据。用来做“DV 到底做了什么”的 A/B 基准 |
| `D:\share\Dolby Vision\杜比视界P10_AV1_1080p_官方测试流.mp4` | **P10**（AV1 10-bit，`av01` 封装） | 播放 + RPU（`levels=[6,1,2,4,5]`，L1/L2/L4/L5/L6）+ L1 跟随（`source=DV10`）全通 |
| `D:\share\hdr10+test_lake_2021_02_01.mp4` | **HDR10+**（SMPTE ST 2094-40） | 元数据每 10 秒切换一组（测试设计）。峰值/均值/起点见 `docs/HDR10PLUS_2026-10-29.md` |
| `D:\share\S01E01.2026.2160p.60fps.WEB-DL.HDR.Vivid.H.265.10bit.DDP5.1.mp4` | **HDR Vivid** | 识别正常（`windows=1`，亮度统计逐帧变化）；但 `toneMapping=0`，没有可应用的映射参数 |

## 怎么用

**回归一遍的推荐顺序与判据**：

```powershell
$env:VEYRA_DOVI_TRACE="1"   # 需要看跟随细节时
# 依次播放，播 15-20 秒即可
```

| 想确认什么 | 播哪个 | 日志里看什么 |
|---|---|---|
| P5 转换没退化 | 星期三 | `profile=5 ... route=2` + 有 `converted as IPT-PQ-C2`、**无** `no usable RPU conversion parameters` |
| P7 合成没退化 | Despicable Me | `EL coverage feed=N carried=N (100%)`、`delivered_without_layer=0`、`refused` 约 1.5% |
| P10 没退化 | 官方测试流 | `codec=av1`、`profile=10 level=4 compatibility=0 route=2`、`extension blocks=5` |
| HDR10+ 读取没退化 | hdr10+ lake | `HDR10+ scene metadata peak≈395/980/176 nits`（每 10 秒一组） |
| 跟随参数合理 | 任一带动态元数据的 DV/HDR10+ 片 | `dovi-follow ... source=DV7/DV10/HDR10+`，`highlightStartNits` 平滑变化 |
| 不该合成的地方确实没合成 | Control / HDR Vivid | **无** `dovi-el-gpu bound=1` 行 |

## 边界（哪些格式没有样本）

| 格式 | 状态 |
|---|---|
| **P8.1** | 无真实样本。用改 `dvcC` 的伪造文件验证过路由 |
| **P8.2**（SDR 基底） | 无真实样本。伪造验证：`route=3`（Sdr）+ 不钉 HDR 标签 |
| **MEL** | 无样本。`disable_residual_flag` 的判定逻辑从未在真实数据上跑过 |
| **双轨 P7** | 无样本。日志判据：`EL coverage ... carried=0` |
| **4K 60fps 的 P7** | 无样本。EL 是软件解码 4–8 ms/帧，高帧率下可能跟不上 |

## 两个坑

1. **`[Dolby] FEL Test` 会闪是正常的**——它只有 50.7% 的帧带 EL。要判断画质用
   `Despicable Me` 或 `CMV4.0` 那个。
2. **`hdr10+test_lake` 的元数据是极端的**（每 10 秒一组反差巨大的值）。它测的是
   “播放器跟不跟”，不代表真实电影的观感。真实 HDR10+ 正片还没验证过。
