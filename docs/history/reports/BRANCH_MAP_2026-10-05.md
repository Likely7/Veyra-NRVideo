# 分支审查 2026-10-05 / 2.0.4

统计相对整合前 main de18fc4；前列是 main 独有提交，后列是分支独有提交。用户其他工作树保留，不删除分支。

| 分支 | main独有/分支独有 | 处理 |
|---|---:|---|
| claude/field-fixes-20261005 | 2/0 | 已在main历史内，保留分支 |
| claude/rtss-compat-20261003 | 150/2 | 仅218c983/460aea9历史性能计划；已由PERF最终账本覆盖，不回退新版文档 |
| claude/ui-fixes-20261005 | 0/2 | 两条NR提交9eb3b1c/33d6685纳入；另有18份未提交UI文件已存档并移植 |
| codex/avermedia-51-switch-20260917 | 437/1 | 历史存档/诊断/撤回实验，非当前产品修复，不合入 |
| codex/beta-latency-ab-20260918 | 393/1 | 历史存档/诊断/撤回实验，非当前产品修复，不合入 |
| codex/capture-xbox-field-20261005 | 0/3 | 两条NR提交及Xbox恢复81b755b全部纳入 |
| codex/export-page-20261002 | 205/0 | 已在main历史内，保留分支 |
| codex/field-2.0.1-20261002 | 151/0 | 已在main历史内，保留分支 |
| codex/field-issues-20260930 | 207/0 | 已在main历史内，保留分支 |
| codex/field-upgrade-20261003 | 142/0 | 已在main历史内，保留分支 |
| codex/github-source-archive | 791/2 | 历史存档/诊断/撤回实验，非当前产品修复，不合入 |
| codex/minimal-edge-hdr-review-20261004 | 133/0 | 已在main历史内，保留分支 |
| codex/nr-strength-protection-20261005 | 0/2 | 两条NR提交9eb3b1c/33d6685全部纳入 |
| codex/obs-resize-20261002 | 158/0 | 已在main历史内；OBS工作树残留patch已由main较新实现覆盖，不重复移植 |
| codex/perf-nr-20261004 | 3/0 | 已在main历史内，保留分支 |
| codex/playback-smoothness-20261004 | 120/0 | 已在main历史内，保留分支 |
| codex/player-startup-speed-20261003 | 148/0 | 已在main历史内，保留分支 |
| codex/release-2.0.0-20261002 | 155/0 | 已在main历史内，保留分支 |
| codex/release-2.0.2-20261003 | 145/0 | 已在main历史内，保留分支 |
| codex/release-2.0.3-20261004 | 125/0 | 已在main历史内，保留分支 |
| codex/release-2.0.4-20261005 | 0/3 | 本轮整合分支；含capture/Xbox+NR，复制Claude UI原字节 |
| codex/rtss-restart-loop-20261004 | 123/0 | 已在main历史内，保留分支 |
| codex/runtime-size-20261004 | 131/0 | 已在main历史内，保留分支 |
| codex/smooth-motion-experiment | 591/1 | 历史存档/诊断/撤回实验，非当前产品修复，不合入 |
| codex/ui-overlay-release-20261002 | 158/0 | 已在main历史内，保留分支 |
| codex/ui-qml-migration-20260925 | 209/0 | 已在main历史内，保留分支 |
| codex/vfg-integration-20261003 | 134/0 | 已在main历史内，保留分支 |
| codex/vfg-research-20261003 | 141/0 | 已在main历史内，保留分支 |
| main | 0/0 | de18fc4起点；本轮合并目标 |

Claude最新用户授权与完成消息来自本地会话d79d2036-17a9-402a-8925-f9b9e0335fb2（2026-10-05 21:50–22:19 Asia/Taipei）；UI源头HEAD33d6685，未提交文件18项。原始工作区未修改，其补丁、文件ZIP及逐SHA在本轮archives。

旧OBS兼容已在main的apps/veyra-qml/main.cpp、bridge和SettingsPage实现并进一步加入RTSS重启策略。保留其未提交文件作历史，不覆盖现代版本。其余排除项包括AverMedia未完成诊断、1.4.2beta latency probe、最初源码存档及后来撤回的强制互斥Smooth Motion实验。HDR/Dolby PR13/14保持暂缓。
