# 分支盘点（2026-10-02）

接 `BRANCH_MAP_2026-10-01.md`。用户要求「把各分支内容整理好，合并到 main」。只描述**本地**仓库，没有 push、没有 Release。

## main 现状

`main` = `6360b62`。10-01 之后并入：

| 合并提交 | 分支 | 内容 |
|---|---|---|
| `ab1fe96` | `codex/xbox-20261001`（含 `codex/moonlight-20261001`） | PC 串流、Xbox 串流、美乐威低延迟、现场修复第 1–4 批 |
| `931eade` | `codex/export-page-20261002` | 导出页队列、MKV / 多音轨 / 内嵌字幕、取消重试、完成提示音 |
| `fb47a14` | `codex/tooltip-nr-20261002` | 悬停提示不被画面挡住、列表模式 NR 总开关、beta 7 说明 |
| `b205d29` | `codex/export-xbox-fixes-20261002`（含 `codex/gpu-monitor-20261002`） | GPU 占用单卡监控、导出慢帧 / 显卡重置处理、导出默认值、第 6 批 |
| `6360b62` | `codex/i18n-20261002` | 繁中 / 英 / 日界面语言 |

本地 68 个分支里 63 个已完整并入 main（分支名只是旧指针，内容都在 main 里）。

## 没有并入 main 的分支（有意保留）

| 分支 | 未并入的提交 | 原因 |
|---|---|---|
| `codex/github-source-archive` | `8556fc7` `2d2a3a6`（09-08） | GitHub 公开仓库的独立历史，和 main 没有共同祖先，不能也不应合并 |
| `codex/smooth-motion-experiment` | `27c17eb`（09-14） | 软件内 Smooth Motion 强制互斥实验，已被 09-14 用户决定取代（只给 NVIDIA App 开启说明），AGENTS.md 要求保留作历史存档 |
| `codex/beta-latency-ab-20260918` | `24bc11a`（09-18） | 给旧 1.4.2beta 引擎加的延迟对比测试；main 里的 `tests/integration/CaptureLatencyComparisonTests.cpp` 是功能更多的新版 |
| `codex/avermedia-51-switch-20260917` | `8819ca8`（09-18） | 圆刚 5.1 任务关闭时存档的**未完成**诊断探针，提交说明即为存档，不是产品功能 |

## 工作区里的未提交改动（不并入）

| 位置 | 内容 | 处理 |
|---|---|---|
| 桌面原目录 `C:/Users/123/Desktop/Veyra DLSS Video Player`（`codex/ui-qml-migration-20260925`） | 62 个文件，全部停在 09-27 13:07 之前：P0/P1 调色工作搬到 E 盘之前的起点 | 与 10-01 存档 `archives/merge-2.0.0-20261001/desktop-tracked-changes.patch` 逐字节相同；之后的版本已在 main。按规定不改动桌面原目录 |
| `worktrees/xess143-compare-20260921-r1`（游离） | 6 个文件：把 XeSS 节奏控制退回 1.4.3 行为的 A/B 对比实验 | 排查用的对照版本，不是产品改动 |
| `worktrees/dlss-recovery-baseline-20260920`（游离） | 1 个测试文件：1.4.3 基线上的测试调整 | 同上 |

## 可选清理（未执行，需用户另行同意）

- 删除 63 个已并入 main 的旧分支名（提交本身都在 main 里，随时可以从提交号恢复）。
- 删除已完成任务的 worktree 目录（`worktrees/` 下 17 个，及桌面 `out/` 下的旧工作区）。
