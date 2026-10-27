# 当前分支归并核对（2026-10-16）

用户要求整理各分支，包括 Claude 的工作，合入本地 main。本轮先核对提交祖先关系、Claude 最新会话、未提交文件内容及历史停止决定，再判断是否需要合并。没有新功能、推送、发布或重新打包任务。

## 结果

核对基线 main 为 `0cfa8a8c8770bd394ed9df923c8d86724590e560`。GitHub `nrvideo` 的远端分支查询只有 main，且指向同一提交。89 个本地分支中，84 个分支的 tip 已在 main 祖先链内，包含 18 个 Claude 分支；另外 5 个分支的具体情况见下表。**当前已完成的修复没有发现漏合并，本轮没有新增产品代码合并。**

读取 Claude 最新两个主会话：`15638198-4910-4628-aab4-c96c62aebd18` 最后停在 2.0.8 telemetry 回归；其后续夹具修复、构建和测试交付已经进入 main，见 [2.0.8 接手报告](TEST_2.0.8_REPORT_2026-10-15.md)。`283ff08e-8858-480a-b6d6-d6177d1414ce` 是 10-14 的手柄计时状态排查，用户中断，没有新的提交或未提交内容变化。

## 已归并的主要来源

以下 tip 均由 `git merge-base --is-ancestor <tip> main` 核验为已包含；不是本轮重新执行合并。

| 来源分支 | tip | 内容 |
|---|---|---|
| `claude/release-2.0.8-20261015` | `f074e40` | Claude 的 2.0.8 集成与审查收尾 |
| `claude/field-20261015` | `18fa6f3` | 采集格式/帧率、图标恢复、GPU 失败恢复及原尺寸 VSR 策略 |
| `claude/fg-gpu-home-20261014` | `6a97dc1` | DLSS 性能模式、运算 GPU、游戏模式、Home 面板与手柄转发 |
| `claude/latency-min-20261013` | `e550f96` | 延迟链路、XeSS 时序和可变刷新 |
| `codex/issues36-37-bmd-20261013` | `345f87f` | 节点界面、旋转及 BMD 输入设置修复 |
| `codex/update-check-20261013` | `4d944de` | 手动/启动更新检查与顶部提示 |
| `codex/latency-chain-audit-20261013` | `f91c771` | 延迟诊断与证据记录 |
| `claude/xess-flow-tune-20261012` | `e46bbdf` | XeSS 光流与边缘处理 |
| `claude/cadence-exact-20261012` | `8d11cad` | 内容节奏及 60→30 修复 |
| `codex/pr34-capture-settings-20261012` | `49eb6bd` | 真实采集格式与设置保存 |
| `claude/vfg-mixed-20261011` | `1d4c33f` | VFG 混合档位 |
| `codex/test-2.0.8-handoff-20261015` | `3107e00` | 接手验收夹具、测试与交付收尾 |

## 保留的 5 个历史分支

| 分支 | 独有提交 | 处理依据 |
|---|---|---|
| `claude/rtss-compat-20261003` | `218c983`、`460aea9` | 独有提交只涉及 `PERF_PLAN_NR_2026-10-03.md`；main 的该文档与分支逐字一致，已由 `37bc0c0` 收入。没有遗失内容，不需再次合并。 |
| `codex/avermedia-51-switch-20260917` | `8819ca8` | 用户在 09-18 已结束该方案，提交专用于保留未完成圆刚诊断探针；WORKLOG 有明确收尾记录。 |
| `codex/beta-latency-ab-20260918` | `24bc11a` | 为保持 1.4.2beta 引擎不变而建立的旧版延迟对照程序，不是当前产品修复。 |
| `codex/github-source-archive` | `8556fc7`、`2d2a3a6` | 与开发 main 无共同祖先的旧公开源码存档；其文档明确说明独立历史关系，见 [源码存档记录](SOURCE_ARCHIVE_2026-09-08.md)。 |
| `codex/smooth-motion-experiment` | `27c17eb` | 用户已改为 NVIDIA App 开启说明、允许自行叠加；旧实验的强制互斥与独立程序策略已被取代，见 [现行说明](SMOOTH_MOTION_HELP_2026-09-14.md)。 |

保留全部分支和工作树，不改写历史、不删除旧实验。

## 未提交内容保护

77 个工作树逐一检查；5 个工作树共 509 个未提交/未跟踪文件，与 `archives/release-2.0.8-20261015/start.json` 的状态和逐文件 SHA-256 完全一致，没有新变化。其余历史工作树 HEAD 也保持一致。

| 工作树 | 文件数 |
|---|---:|
| 桌面旧 QML migration 工作树 | 466 |
| `dlss45-sr-20261011` | 9 |
| `jelly-latency-20261012` | 5 |
| `obs-resize-20261002` | 11 |
| `ui-fixes-20261005` | 18 |

这些文件仍为原地保留的旧稿，没有将其作为新修复提交。完整分支和 SHA 清单在 E 盘任务证据中。

## 本轮验证与边界

实际执行分支枚举、祖先关系检查、`git cherry`、5 个历史分支的独有提交/差异检查、77 工作树状态及 509 文件哈希对照、Claude 会话读取和 `git ls-remote --heads nrvideo`。只追加本记录和 WORKLOG，提交后检查差异、主线干净状态及全部其他分支/工作树保护。

没有产品代码、QML、着色器、构建配置或运行库变化，因此本轮未重新构建、未执行 GPU/采集/播放回归；现有 2.0.8 验收结果及其已知限制仍按原报告理解，不新增通过声明。没有 push、Release、重新打包或关机操作。

证据路径：`E:/项目/Veyra/reports/branch-integration-20261016/{audit,final}.json`；开工状态：`E:/项目/Veyra/archives/branch-integration-20261016/start.json`；只读核对脚本：`E:/项目/Veyra/tmp/branch-integration-20261016/audit.py`。
