# 2026-10-10 已验修复本地合并

用户在测试包交付后明确授权“合并到main吧”。本轮只本地合并/记录，不推送、发布、变更版本、重新打包或关机，不删除旧分支/工作树，不派子Agent。

- main合并前：`0e7018adab9b211d76dd05da40cfbaeb9d77cf2c`，工作树 `E:/项目/Veyra/worktrees/main-merge-20261002`，干净。
- 实测来源：`codex/sr-visual-quality-20261010`，`48d8f3464d64ef5e7ee672bbca3526d1bccfca7e`，干净；main为其祖先，11个提交/108文件待合入。
- 执行：`git merge --no-ff codex/sr-visual-quality-20261010 -m "Merge tested 2.0.6 subtitle, playlist, capture and settings fixes"`。
- 功能merge：`ff80a49ad199334cafb79de79a8ee1b1f66c8aee`，无冲突，两个parent分别为上述main/tip。`git diff --exit-code codex/sr-visual-quality-20261010 HEAD`通过，整个Git树相同。
- 后续只更新本文件、AGENTS、CURRENT_STATUS、WORKLOG，产品代码/QML/shader/CMake/测试/翻译不改；最终main、checkpoint和bundle以 `E:/项目/Veyra/reports/merge-fixes-20261010/final.json` 为准。

包含此前已验PGS/SUP等字幕及压缩Matroska支持、播放列表/文件夹导入和PR31采集音频输出恢复、BMD原生输入与默认关闭的开关、设置交换链保留/失败回退、手动数字输入焦点、多语言长字布局、关闭效果器编辑不触发GPU重建及抗闪烁设置保存。既有HDR补帧兼容开关默认关闭。SR局部/整帧静态校验、额外纹理/pass、档位/曝光实验没有合回；SR生产实现与本次实验前c664a67一致，保留更早真实执行门控/首帧修复。

## 复用的实际验收

`E:/项目/Veyra/reports/sr-visual-quality-20261010/final.json`实际passed=true，prepare.py重新核对来源commit、构建/测试/CMake回执SHA、当前构建源文件SHA及最终便携包/源码ZIP SHA。合并树完全相同、后续只有文档，因此没有重复GPU测试或冒称新跑了一遍。

- 撤回后完整16EXE构建：`logs/sr-visual-quality-20261010/build-20261010-152739/receipt.json`；最后仅GUI显示标签/Windows资源的153535构建，EXE SHA `870623fec3b265bf9f3ce0837449e90934ec47164d22eb8b484e33ee95dbfc4c`。
- 原生/no-SDK配置及相关对象：`build-20261010-153030/receipt.json`。
- 13组回归：`tests-20261010-152929/receipt.json`，SR/presenter/slider/menu/components/lifecycle/video/rollback/inactive/inactive-node/chain/contracts/hdr全部通过。
- 封装后GPU及软件QML两种私有配置GTA6播放：各463真实帧提交，exit0，生产包文件前后SHA相同。这个计数不是屏幕刷新或端到端延迟测量。
- 测试包 `test-packages/fixes-20261010/Veyra-2.0.6-fixes-20261010-NVIDIA-win64-portable.zip`，714,646,109字节，SHA `1350c723d56bd8aba13afbb09f959f591f1e6ce819f324e4b3fcb7dbc9103b9c`；1626文件逐项SHA/CRC及48运行组件清单通过，1614文件母包原件保持。对应应用源码/FFmpeg-dav1d-zlib源码和许可证已在同目录，现包不变。

以后从main构建须显式提供外置 `VEYRA_DECKLINK_SDK_DIR`；打包使用 `E:/项目/Veyra/deps/ffmpeg-subtitle-formats-20261009/installed`，保持zlib、PS5 slices256、dav1d和对应源码/许可证。不以本机5070验收替代AMD/BMD/反馈者实卡、全编码器/串流或NV显存增长根治结论。

## 存档与保护

开工 `E:/项目/Veyra/reports/merge-fixes-20261010/prepare.py` 保存不可变 `archives/merge-fixes-20261010/start.json`，46既有工作树/495脏文件逐项HEAD/status/SHA；本轮只允许main工作树前进。旧任务start/guard/包/源码分支保持，不为了允许merge修改旧保护规则。

合并前标签 `checkpoint/pre-merge-fixes-main-20261010`、来源标签 `checkpoint/merge-fixes-source-tip-20261010`；增量 `source-before.bundle` 已verify，包含前main及实测分支。功能merge和最终main另打 `checkpoint/merged-fixes-main-20261010` / `checkpoint/merge-fixes-main-final-20261010`，增量 `source-after.bundle` 再verify。结束核对其余45工作树、495脏文件、实测源码分支、产品树、包SHA、main干净及仅四份文档差异，结果在reports同名/final.json。
