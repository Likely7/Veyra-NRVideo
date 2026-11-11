# 2026-11-05 本地 main 整合与分支整理

## 范围和存档

用户授权整理各分支并合入 main，包括 Claude 后续工作；同时研究 NR 网状调节器。本轮不 push、不 Release，不修改已有正式包、真实配置或共享测试程序。

合并前 main：`8eda262f283e25f0d45e8bde6903e0b806458d82`。存档标签：`checkpoint/pre-main-integration-20261105`。活动树仍为 `E:/项目/Veyra/worktrees/screen-recording-fix-20261022`；先在 `codex/integrate-completed-20261105` 验证，再快进 main。

证据目录：`E:/项目/Veyra/reports/main-integration-20261105`。其中 `BRANCHES-BEFORE.json`、`WORKTREES-BEFORE.json`、三个原始差异及 SHA256 清单保存合并前状态；`CLAUDE-SNAPSHOTS.json` 记录 Claude 工作的提交与原作者来源。

## 合入内容

| 工作 | 来源提交 | 结果 |
|---|---|---|
| 全屏进度条拖动 | `4895549` | 拖动期间保持交互状态，取消／禁用清理延迟跳转 |
| RTX30 VFG 实验兼容 | `edad6e9` | SM86 进程内兼容与可验回滚，40／50 保留原生路径，MIT 来源保留 |
| NVENC 导出关键帧 | `5731c54` | H.264／HEVC 的 IDR 周期同步原 GOP120，普通 I 帧仍不冒充可随机跳转关键帧 |
| RX7000 NR 多 GPU 修复及诊断 | `ecfbb0f`、`b27c0df`、`77fcd89`、Claude `19a8680` | NVAPI 按当前上下文匹配 LUID；worker 在加载 HIP 前选择设备；保留阶段／stderr／异常诊断 |
| 英伟达低延迟增强档提示 | Claude `3593e52` | 高负载建议“开”，说明增强档可能掉帧及增加功耗；四语和测试同步 |
| RTX 视频超分三个版本及混合双重超分 | Claude `ba00ce3` | 标准／高码率／流媒体，各自档位；第二次超分独立选版本；适配超采样和预设保存 |

合并提交为 `5209442`、`974fa9f`、`79d1ac9`。冲突只在记录追加、CMake 源文件清单及显卡能力判断：保留 VFG Ampere 和 VideoSuperRes 两个实现；兼容流媒体的 RTX30+ 能力判断与 VFG 的 SM86 入口。增加五条能力回归，覆盖新超分支持 30／40／50、拒绝 RTX20／AMD，不修改实际能力实现。

## 排除与历史工作树

- 延迟预算实验已按失败方向撤销，分支不存在，产品接口扫描没有重新引入该功能。停止研究的决定保持有效。
- RIFE 已按用户最新要求删除；本次只合保留的视频超分，不因分支旧名字包含 rife 而恢复代码。
- Lecram 两条暂停分支及 `codex/avermedia-51-switch-20260917` 诊断存档不合入。
- 六个旧脏树保留原位。DLSS SR K/M/L、光流细化、全屏退出、软件绘制、旧界面修正已由较新的 main 实现覆盖，不拿历史补丁覆盖现行实现。桌面旧树和共同 `.git` 保留。
- 已合并且没有被任何工作树检出的冗余本地分支用 `git branch -d` 清理；不强删未合并工作。检查中的工作树分支继续保留，并明确它们已经合入、不是下一步待办。具体前后 SHA 及保留原因见 `BRANCH-CLEANUP-PLAN.json` 和最终回执。

## 本轮验证

构建命令使用 `scripts/acceptance/build-recording-handoff.py --task codex-main-integration-20261105 --root <活动树> --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.2-test --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared`，目标共 19 个，134.4 秒通过。新能力断言另增量构建 1.3 秒。配置从当前合并源码重建，已经清除之前预算实验的构建配置影响，继续使用 patched PS5 FFmpeg。

运行脚本、逐项命令、退出码及日志均位于证据目录。测试 app：`E:/项目/Veyra/tests/codex-main-integration-20261105/app`；日志：`E:/项目/Veyra/logs/codex-main-integration-20261105`；所有 TEMP／TMP、CUDA 缓存及用户数据均隔离到本任务目录。

已通过：视频超分设置 240、VFG 设置 683、能力 80、VFG 模块兼容／回滚 342、HIP 单元 15／真实 HIP6 枚举与可见性 19、worker 诊断 14、跨进程 GPU 协议 54、原生 VFG GPU 2425；效果链、预设库、NR 模型设置、UI 合同、i18n 及翻译占位检查通过。真实主程序的三版本／双重超分夹具 15 项通过，无 QML 错误。

七组 SR 生产导出，每组 60 帧：标准／高码率／流媒体单次与 NGX→NGX、NGX→VFX、VFX→NGX、VFX→VFX 四种双重组合；输出帧数一致，420 帧相邻帧对齐检查无错位，非黑帧。NVENC 两种编码各 270 帧，均有第 0／120／240 帧 IDR，MP4 原始 stss 为 1／121／241。未把统计的源帧匹配称为完整画质验收。

首次测试脚本漏传部分测试的输出目录参数、漏部署 Qt QML 模块、漏建截图输出目录，保留失败日志后纠正夹具；没有为这些夹具问题修改产品实现。初次过量复制的非目标测试 EXE 已从本任务 app 清除，只保留 19 个目标。供体中的旧用户配置链接已从本任务 app 移除，原供体未改。

冷缓存第一次高码率模型加载约 12.4 秒，触发 engine-hang 无进展诊断，栈在 NVIDIA PTX JIT；随后恢复并通过全部界面步骤。保留冷启动证据，不将其报成“全程无错误”或已解决加载停顿。

修正夹具后的完整 QtQuick 为 **146 通过／0 失败／0 跳过**，含全屏 seek、低延迟提示、菜单布局及截图用例，46.8 秒完成。热缓存真实主程序复测 15 项再次通过，模型加载约 1.3–1.5 秒，engine／QML 错误均为空；见 `QML-FINAL.json` 与 `ui-sr-families-ui-warm.json`。

本机为 RTX5070。RX7000 用户可开启 NR 是此前现场反馈，本轮只有 HIP6／协议／CPU 合同验证；没有本地 HIP7 NR 推理。RTX30 实卡和新超分 HDR 本轮未验证。新的 VFX DLL 仍仅在本地测试运行目录，Release manifest 未扩展，不把本次整合当成正式包发布。

## NR 网状调节研究

见 [研究与三个候选方向](NR_WEB_CONTROL_RESEARCH_2026-11-05.md)。建议从四轴“变化网”加六轴“保护网”开始，参数直接映射、保留数值、默认轮廓和原画对比；模型与运行设置另列。本轮没有改 NR 界面、保存格式或算法。
