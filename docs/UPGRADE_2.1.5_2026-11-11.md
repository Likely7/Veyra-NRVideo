# 升级到 2.1.5（定制版 + 作者版双切换）

2026-11-11。作者发布 2.1.5，包在 `D:\下载\DLSS5\Veyra-2.1.5-NVIDIA-win64-portable`。

## 结果

- 定制分支：`test/merge-215`（`eee01fbe`），在 `test/merge-214` 基础上 merge `v2.1.5`（`dc6f6e1e`）。
- 部署目录：`D:\Program\Veyra-2.1.5-NVIDIA-win64-portable`。
- 定制 exe：`1f5b20167929da0003069072bae14820ffa4a44d3b5bfa25da4bfdb1bf529895`（Ninja 构建）。
- 作者 exe：`87d6b491bd3fdb793c6e3feb2a8ae0b2c9edeb3be0f392b32be4661a510509bd`（与作者包 manifest 一致）。
- 双切换实测：作者版 ↔ 定制版均启动成功，当前停在定制版。
- `scripts/verify-build.ps1` 全过：构建、三个单元测试（567/462/334）、部署一致性、运行时 0 条 ERROR。

## 上游 2.1.5 更新（2.1.4..v2.1.5，7 个提交、16 文件）

- **去重后的补帧节奏**：图内补帧按去重后的内容时间轴安排呈现，减少 40 FPS 内容 / 60 Hz 采集时相邻帧忽快忽慢。
- **异常采集时间戳校正**：驱动时间戳持续偏离稳定回调节奏时按实际采集节奏推进；正常 59.94/60 Hz 时间戳照常使用，音频同步仍读驱动原始时间。
- 其余为发布记录（版本号、更新说明、微信群二维码）。

## 合并与冲突

2 个冲突：

| 文件 | 处理 |
|---|---|
| `docs/CURRENT_STATUS.md` | 保留两边（上游段在前、本地记录在后） |
| `src/engine/EngineController.cpp` | `liveLoop` 的 lambda 捕获列表：采用上游 2.1.5 版本（`smoothAnchor` → `smoothTransportAnchor` 改名，新增 `captureClockCorrected`、`driverPts100ns`），并补回本地定制的 `seekDemuxDone` |

`seekDemuxDone` 是本地 seek 修复带来的捕获变量，上游 2.1.3/2.1.4/2.1.5 都没有，合并时容易丢。

## 部署与验证

- 部署结构同前：`.orig`（作者）/ `.custom`（定制）、`qml.orig` / `qml.custom`、两个 `.cmd`、`版本切换说明.txt`。
- 着色器差异仍为 2 个（`PresentBlit_ps.dxil`、`YuvToLinearRgb.dxil`）。
- 用户数据从 2.1.4 迁移：`user-data-2.0.0-p5`（8 文件）与 `user-data-2.0.0-author`（3 文件）；预设库/会话版本号未变（库 21 / 会话 16），2.1.4 的存档直接可用。
- B 站登录态在 `%LOCALAPPDATA%\Veyra`，不受影响。

## 未验证

- 2.1.5 的采集预览改动只在采集卡上体现（40 FPS 内容 / 60 Hz 采集 / VFG 3× 的 P99 与 1% low），本机无采集卡，未实测；
- 本地定制功能（B 站、DV、字幕、缓冲门控、自动补帧倍数）只做了启动检查，未逐项回归。

## 其它

- `scripts/verify-build.ps1` 的默认部署目录已更新为 2.1.5。
- 2.1.4 部署目录保留，可回退。
- PR #80 未随本次升级更新（只推代码的规则不变）。
