# 分支整理与 2.0.0 合并记录（2026-10-01）

> 用户决定（2026-10-01）：2.0.0 基本没有大问题，先把 2.0.0 各分支整理并合并到 `main`，存档，更新文档。剩下的小 bug 和功能之后再逐项对。
> 本记录只描述**本地**仓库；`nrvideo/main` 仍停在 1.4.4，**没有 push，没有 Release**。

## 1. 结论

- 2.0.0 的 7 个分支**全部指向同一个提交 `53a2d31`**（`chore(ui-mig): close scope guard integrity loop`），没有任何分支独有的提交。这几个名字只是不同阶段留下的指针，不是 7 条并行的开发线。
- `main` 是它们的直接祖先（落后 63 个提交、没有分叉），所以合并没有冲突。
- 但 P0 之后到 2026-09-30 的全部工作（Codex 与 Claude Code 的 P1–P5、反馈修复、延迟优化）**从未提交**，一直是 E 盘工作区里的未提交改动。这次一并提交为两个提交（源码/测试/脚本；文档），再合并进 `main`。
- 合并用 `--no-ff`，`main` 上有一个明确的「2.0.0 合并」提交；需要整体撤销时可以 `git revert -m 1`。

## 2. 存档（合并前）

目录：`E:/项目/Veyra/archives/merge-2.0.0-20261001/`（`SHA256SUMS.txt` 逐文件校验）

| 文件 | 内容 |
|---|---|
| `veyra-all-refs-before-merge.bundle` | 合并前**全部引用**（分支、标签、远端跟踪）的完整 git bundle，`git bundle verify` 通过；可 `git clone` 或 `git fetch` 回来 |
| `refs-before.txt` / `branch-inventory-before.tsv` | 合并前每个引用的 SHA；本地分支按「已在 main / 随本次并入 / 未并入」的分类 |
| `worktrees-before.txt` | 合并前的 worktree 清单 |
| `eworktree-tracked-changes.patch` / `eworktree-untracked/` / `eworktree-status-before.txt` | E 盘工作区提交前的未提交状态（已跟踪文件补丁 + 未跟踪文件原件） |
| `desktop-tracked-changes.patch` / `desktop-untracked/` / `desktop-status-before.txt` | 桌面原目录（`C:/Users/123/Desktop/Veyra DLSS Video Player`）合并前的未提交状态；见 §5 |

Git 标签：

| 标签 | 指向 |
|---|---|
| `checkpoint/pre-merge-2.0.0-main-20261001` | 合并前的 `main`（`df41580`，1.4.4 发布后的文档修正提交） |
| `checkpoint/2.0.0-branch-tip-20261001` | 提交后的 2.0.0 分支尖（合并的第二父提交） |
| `checkpoint/merged-2.0.0-20261001` | 合并后的 `main` |

## 3. 2.0.0 分支（合并前都在 `53a2d31`）

| 分支 | 处理 |
|---|---|
| `codex/fg-fsr-xess-20260930` | **已删除**（与 `main` 合并后同一提交，没有独有内容） |
| `codex/field-issues-20260930` | 保留：E 盘工作区 `worktrees/p0-p1-r53-20260927` 当前检出在这个分支（本次提交和合并的来源） |
| `codex/p0-p1-r53-20260927` | **已删除**（与 `main` 合并后同一提交，没有独有内容） |
| `codex/p2-nr-20260927` | **已删除**（与 `main` 合并后同一提交，没有独有内容） |
| `codex/p3-node-backend-20260927` | **已删除**（与 `main` 合并后同一提交，没有独有内容） |
| `codex/p4-functions-20260928` | **已删除**（与 `main` 合并后同一提交，没有独有内容） |
| `codex/ui-qml-migration-20260925` | 保留：桌面原目录当前检出在这个分支，且有未提交改动（见 §5） |

删除只删了分支名，提交本身都在 `main` 里；恢复某个名字用 `git branch <名字> 53a2d31c`。这两个保留的分支之后不再使用：桌面目录换到 `main` 后即可删除 `codex/ui-qml-migration-20260925`，E 盘工作区用完后即可删除 `codex/field-issues-20260930`。

## 4. 其余本地分支（不属于 2.0.0，本次没有动）

**已经在 `main` 里的 55 个**：都是 1.x 各阶段的修复/实验/发布分支，提交已经在 `main` 历史中，分支名只是指针，可以随时删除而不丢任何提交。为免误伤，本次没有删（其中若干还被 worktree 检出）。

<details><summary>名单</summary>


`agent/av-scheduling-repair`, `agent/remoteplay-integration`, `agent/veyra-v1-loop`, `checkpoint/color-p1-archive-20260917`, `codex/5060-presets-subtitles-20260918`, `codex/5090-capture-fg-20260921`, `codex/bounded-full-chain-20260922`, `codex/capture-audio-latency`, `codex/capture-color-144beta-20260920`, `codex/capture-decode-latency-20260916`, `codex/capture-fg-clock-repair-20260914`, `codex/capture-formats-20260914`, `codex/capture-ui-sync-20260919`, `codex/color-tab-p1-20260917`, `codex/cross-monitor-20260919`, `codex/dlss-recovery-20260920`, `codex/export-mkv-d3d11-repair-20260917`, `codex/fg-backend-switch-20260919`, `codex/fg-cadence-audit-20260920`, `codex/fg-independent-repair-20260922`, `codex/fg-scheduling-repair-20260919`, `codex/fg-stability-20260921`, `codex/fg-utilization-20260921`, `codex/frame-pacing-20260918`, `codex/framegen-fsr-dolby-20260916`, `codex/fsr41-nvidia-20260918`, `codex/gpu-dis-integration`, `codex/hdr-multichannel`, `codex/media-codec-compatibility`, `codex/playback-nr-20260920`, `codex/post140-field-repair-20260917`, `codex/processing-metrics-and-app-icon`, `codex/ps5-decoded-frame-strip`, `codex/ps5-fg-overload-audio`, `codex/ps5-reconnect-repair`, `codex/ps5-sampling-repair`, `codex/ps5-scheduler-telemetry-decode`, `codex/ps5-source-quality`, `codex/ps5-stream-recovery`, `codex/release-1.0.0`, `codex/release-1.0.1`, `codex/release-1.0.2`, `codex/release-1.1.0`, `codex/release-1.1.1`, `codex/release-1.3.0`, `codex/release-p1-repair`, `codex/rtx30-nr-safe-defaults`, `codex/screen-capture-20260919`, `codex/seven-audit-20260919`, `codex/slider-reset-20260919`, `codex/smooth-motion-help`, `codex/subtitle-popup-20260919`, `codex/user-issues-repair-20260915`, `codex/video-hdr-20260918`, `main`


</details>

**含独有提交、没有并入 `main` 的 4 个**（保持原样，是否合并/丢弃需要用户决定）：

| 分支 | 尖端 | 最后提交 | 独有提交数 | 说明 |
|---|---|---|---|---|

| `codex/avermedia-51-switch-20260917` | `8819ca80` | 2026-09-18 | 1 | AverMedia 诊断的未完成存档（有 worktree `out/worktrees/avermedia-51`） |
| `codex/beta-latency-ab-20260918` | `24bc11a3` | 2026-09-18 | 1 | 1.4.2beta 延迟 A/B 探针（有 worktree `worktrees/beta-latency-ab-20260918`） |
| `codex/github-source-archive` | `2d2a3a6c` | 2026-09-08 | 2 | GitHub 源码归档用的分支（对应 `origin/main`） |
| `codex/smooth-motion-experiment` | `27c17ebb` | 2026-09-14 | 1 | Smooth Motion 手动实验（普通版已改为只提供开启说明） |

## 5. 桌面原目录与 worktree

- **桌面原目录**（`C:/Users/123/Desktop/Veyra DLSS Video Player`）仍检出在 `codex/ui-qml-migration-20260925`，本次**没有改动它**。它有 50 个已修改文件，都是 2026-09-26/27 的旧状态；逐个核对过，对应文件在 E 盘都有更新的版本（E 盘修改时间为 9-27 至 9-30，是在它的快照上继续改出来的），少数几行旧写法被后续重写，没有丢失功能。它另有 3 个误生成的 CMake 构建目录 `veyra_preset_library_tests/`、`veyra_qml_easing_tests/`、`veyra_qml_quick_tests/`（约 375 MB，把测试目标名当成了构建目录），是垃圾，未纳入任何提交，也没有删除。
  - 建议：确认无用后，在桌面目录执行 `git checkout main`（很可能因为未提交改动被拒绝，先决定这些改动怎么处理；它们的内容已随 E 盘版本进入 `main`，旧状态另存在存档目录里）。
- **worktree**（本次一个都没删）：`E:/项目/Veyra/worktrees/` 下 10 个，加桌面 `out/` 下 2 个和 `C:/Users/123/.codex/worktrees/` 下 1 个，见 `worktrees-before.txt`。
  - `p0-p1-r53-20260927`：本次的工作区。
  - **不要清理**：`beta-latency-ab-20260918` 和桌面 `out/worktrees/avermedia-51`，它们检出的正是 §4 里含独有提交的分支。
  - 检出的分支已在 `main`（清理只是删目录，不丢提交）：`playback-nr-20260920`、`fg-independent-repair-20260922`、`fsr41-nvidia-20260918`、`frame-pacing-20260918`、`video-hdr-20260918`。
  - 分离头的对照副本：`v140-fg-compare-20260921`、`xess143-compare-20260921-r1`、`dlss-recovery-baseline-20260920`，以及桌面 `out/owner-baseline`、`.codex/worktrees/p0-p1-r53-20260927`（Codex 的）。
  - 是否清理由用户决定。

## 6. 之后怎么用分支

- `main` 是唯一主线。以后 2.0.0 的小 bug 和功能，各自从 `main` 开一个短命分支（如 `codex/fix-xxx-日期`），验收后合回 `main` 并删掉分支名。
- 不再保留“同一提交的多个阶段别名”。阶段点用 `checkpoint/*` 标签（现有 `checkpoint/ui-mig-*` 保持不动）。
- push、GitHub Release、Runtime Pack 上传仍需在对话中单独授权。
