# 2026-11-08 本地 main 修复整合

本轮按用户要求合并当前修复。main 基线 `0079ed9`，验收的组合代码为 `77d6c09`。防闪烁方案暂停。

| 来源 | 合入内容 |
|---|---|
| `claude/volume-dual-sr-20261108`，`84e2cb5` | 音量自动保存和恢复，DLSS SR／FSR／RTX 视频超分可混合执行两次超分 |
| `claude/record-hdr-dlaa-20261108`，`8123c82` | 录制快捷键防重复与触发日志，HDR 广色域恢复，DLAA 及 11 条审查修复。该提交包含音量和双重超分分支 |
| `claude/nr-reuse-20261107`，`a375afa` | 原 39 个工作树文件按接手时内容提交。包含默认关闭的 NR 复用、色温修正、提交帧率和 1% low、NR 界面与图标 |

旧分支按 Git 祖先关系和先前整合记录检查。已合入的缓存、HIP 自检、全屏进度条及 RTX SR 无需重放。历史脏树保存原状，Lecram 及 AverMedia 诊断存档保留独立。用户授权本地 main 整合，GitHub 仍使用原发布版本。

## 组合冲突

- `docs/WORKLOG.md` 保留共同基线和双方追加。
- `EnhanceGraph.cpp` 的显存估算合计 NR 复用、DLAA 和 HDR 恢复。DLAA 读取相邻帧的光流并先完成源尺寸阶段，NR 可以为自己的历史推理改写跨帧运动。DLAA 在需要新尺寸时使用专用适配纹理。

## 验收

- 重用 `E:/项目/Veyra/build/claude-screen-recording-20261022`，标签 `2.1.3-main-fixes-test`，16 目标构建 147.7 秒。回执 `E:/项目/Veyra/logs/codex-main-fixes-20261108/build-20261108-214335/receipt.json`。
- `verify.py` 的 25 项检查中，初次 QtQuick 异常退出 `0xC0000409`，事件记录指向测试目录的 Qt6Core。使用固定 Qt SDK、独立数据目录和日志重新运行同一可执行文件，175/0/0。保留首次日志，原因未确认。
- NR 复用单元和 GPU、色温表、呈现计时、预设与设置、翻译、设置事务通过。AMD 模拟链路 128/0，D3D12 错误 0，适用的是合成合同。
- DLAA 1440p 光流适配、双实例、HDR、DLSS SR／FSR HDR 和 5 组 HDR 双重超分通过。真实程序在 1440p 片源开启 4K 超分、DLAA 与 NR 每 2 帧复用 1 帧，三项同时生效，无 engine ERROR。音量 29% 重启恢复。
- FSR→DLSS SR 的实际 4K 混合导出完整输出 60 帧，逐帧相邻对齐比较错位 0。
- 共用测试运行目录仍是 `E:/项目/Veyra/tests/codex-claude-merge-20261106/app`。88 个界面文件一致、101 个运行 DLL 哈希保持。复用现有构建和运行目录，仅新增针对性测试证据和私有测试配置。

测试入口 `E:/项目/Veyra/tests/codex-main-fixes-20261108/启动main组合测试.cmd`，独立 `manual-profile`。最终回执 `E:/项目/Veyra/reports/codex-main-fixes-20261108/FINAL.json`。真实增强验收为 RTX 5070，现场录制误触根因和 AMD 实卡结论沿用对应修复记录。
