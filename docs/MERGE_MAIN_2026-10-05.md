# 2026-10-05 性能优化与 Claude 现场修复合入本地 main

用户明确要求“先合并到main，然后有其他修复等你做”。本次只完成继承修复提交、本地合并、存档及进度同步，没有推送或发布，没有实施新的修复。

## 合并结果

- main 工作树：`E:/项目/Veyra/worktrees/main-merge-20261002`。
- 合并前 main：`354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff`。
- 来源工作树：`E:/项目/Veyra/worktrees/field-fixes-20261005`，分支 `claude/field-fixes-20261005`，继承 HEAD `2dbf26c`，包含 `codex/perf-nr-20261004` 的完整性能优化历史。
- 29 个未提交文件提交为 `6e9d8273c1d3ae5182d07cfec8c370b68bb4da23`，涵盖预热默认关闭/设置分类、采集节奏与电源节流、AMD NR 规范化、libass ASS 字幕、图重建后 GPU 计时以及验收文档。
- main 是来源分支祖先；`git merge --no-ff -m "Merge verified NR performance optimization and Claude field fixes" claude/field-fixes-20261005` 无冲突，合并提交 `f7ced91d920f072fb9637e95811e701b7e1bd876`，两个父提交分别为上述 main 和来源修复提交。
- 合并树 `b35201b90e1911da492f4aecf92ed59a41092d41` 与来源提交完全相同；后续 main 提交只更新 AGENTS/CURRENT_STATUS/WORKLOG/FIELD_FIXES/本合并记录。

## 存档与标签

Run：`merge-20261005T063850Z-1b5fef`。本轮所有新研发产物均在 `E:/项目/Veyra/`：

- `archives/merge-main-20261005/merge-20261005T063850Z-1b5fef/`：合并前 `before.bundle`、`before.json`、未提交原字节 ZIP、binary patch/index patch 和 SHA 清单；合并后增量 bundle 与最终 receipt。
- `logs/merge-main-20261005/merge-20261005T063850Z-1b5fef/`：逐命令日志、bundle 核验、进度 JSON 和独立合并核验。
- `tmp/merge-main-20261005/merge-20261005T063850Z-1b5fef/`：合并/核验夹具和 commit 文本，子进程 TEMP/TMP 同在此处。
- `before.bundle` SHA256：`15b44fccbd73f090d05c894b62035a34560ae0113059fdc6bc2660fc8a6f984b`。
- `checkpoint/pre-merge-perf-field-main-20261005` → `354b1c6`。
- `checkpoint/perf-field-branch-tip-20261005` → `6e9d827`。
- `checkpoint/merged-perf-field-main-20261005` → `f7ced91`。

## 验证与边界

合并前已实际完成的验证见 `FIELD_FIXES_2026-10-05.md` 第 6 节，证据为 `E:/项目/Veyra/logs/claude-handoff-20261005/takeover-20261005T061823Z-1ad4ac/`：既有 build-6 成功；第二版 NVIDIA ZIP CRC/1578 载荷/1605 对应源码/123 运行组件审核通过；五个相关测试 exit 0；约 45 秒七阶段真实播放切换、缓存命中及 GPU 计时通过。主程序 SHA256 为 `09b439d8faf4df121a0749b6eaaccf71de0b8c344d144feb1ea743cc7d973a14`。

本轮使用 `py -3.11 -B <本轮tmp>/merge-local.py commit-source|merge|tag-merge` 完成操作，`verify-merge.py pre-docs-v2` 确认合并父提交/树、两目标工作树清洁、桌面与其余 14 个工作树逐文件 SHA 保持。初次核验只因桌面 untracked 状态的目录折叠与归档逐文件枚举口径不同而失败；原失败 JSON 保留，改为同样的 `--untracked-files=all` 后通过，没有改变文件或放宽字节断言。最终文档提交后再核验允许的五个文档差异、源码一致性、清洁状态和其他工作树保护，并核验增量 bundle。

本次不需要重建：没有新增产品代码，合并 Git 树与已构建/复测版本相同；不把历史构建冒称本轮重新编译。未新提交 runtime/SDK/模型；既有测试包不变，分支和工作树均保留。

暂停后新接 OBS、用户采集卡后台问题、RX9000 实卡、其他 RTX 生命周期及可选尺寸主观画质仍是未验项。Claude 原性能验收仍为有条件通过；本地合并不把这些条件改写成通过。HDR/Dolby Vision PR、旧 Win32 删除、推送及 Release 没有本次授权。
