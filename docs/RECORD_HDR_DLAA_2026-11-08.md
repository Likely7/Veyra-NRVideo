# 录制触发、HDR超分与DLAA（2026-11-08）

## 用户范围和接手

原始需求：排查录制反复开关，修复超分与HDR同开失效，增加DLAA抗锯齿。工作树 `E:/项目/Veyra/worktrees/claude-record-hdr-dlaa-20261108`，分支 `claude/record-hdr-dlaa-20261108`，基线 `84e2cb5`（继承音量保存与全后端双重超分）。不合main、不push、不发布，不改真实配置、运行组件或驱动。用户要求完成汇报后关机。

Claude完成主体后额度耗尽，停在新增DLAA界面测试的套件验收。Codex先读原始聊天、日志与差异，保存23文件SHA256快照 `reports/claude-record-hdr-dlaa-20261108/CODEX-TAKEOVER.json`，继续补验收和发现的相关缺口。

## 实现合同

- 录制快捷键禁用长按自动重复；所有录制开始／停止记录触发来源，快捷键另记实际按键、scan code、虚拟键、repeat和事件间隔；启动及改键时记录有效映射。只记触发录制的按键，不输出日常输入内容。
- 原生HDR不再直接跳过FSR，以HDR上下文处理线性scRGB（BT.709，1=80nits），保留亮度合同。FSR、DLSS SR和DLAA对超出BT.709的负分量共用GPU恢复，避免裁到BT.709；双重FSR每次独立恢复。
- DLAA通过原有DLSS后端的DLAA模式运行，输入输出同尺寸；保留原SR的1:1旁路。共享光流、每实例独立历史，运动不可用时用显式零运动并reset。视频没有游戏引擎的抖动采样、真实几何运动和深度，不能宣传为游戏内DLAA等效。
- DLAA在抗锯齿编辑器与FXAA/SMAA/MLAA并列，按DLSS能力置灰，仍支持原节点插入位置。静态图片原样通过并显示原因；使用全局K/M/L模型，模型与光流切换须重建，普通滑条保持原热更新规则。
- DLAA预设库v20、模式会话v15与 `.dlaa` 旁文件保留旧文件；源码Git不收运行库、模型或SDK。

## 现场证据与判断边界

输入是用户 `E:/wechat/xwechat_files/wxid_kcmlgkgv70mv22_d585/msg/file/2026-11/logs.zip`。日志提取至本任务logs/field，RTX3060Ti、1440p60 NV12、165Hz HDR显示器。录制反复收到空原因的停止请求，文件正常收尾，不能解释为编码器崩溃或采集卡停流。原日志没有按钮／按键来源和有效快捷键映射，不能确定究竟是哪个外部操作触发。

同份现场日志里的图配置一直是sr=0，无法据此证明某一个已启用的SR后端在HDR下失败。正式2.1.3还不含已在本地main修好的QML内容缓存隔离；只能列为相关背景，不能据此认定本例根因。

## Codex补齐项

DLAA独立启用时保持模型／光流参数有效并在运行图拒绝错误热更新；独立零运动与0.9常量深度显式上传；HDR额外纹理和DLAA自身SDK预留纳入显存预算，分段SR之外仅增加DLAA自身预算。FSR HDR对广色域负分量的恢复独立于亮度重建，避免同一负分量被加两遍。

## 验收记录

接手后首先运行 `python -X utf8 -B E:/项目/Veyra/reports/claude-record-hdr-dlaa-20261108/quick_run.py suite`：174通过、0失败、1跳过（需显式输出目录的菜单栅格比较）。完整修复后重跑，175通过、0失败、0跳过。

所有新产物复用 `E:/项目/Veyra/{build,tests,logs,tmp,reports}/claude-record-hdr-dlaa-20261108`，只对测试子进程设置TEMP／TMP和独立data-dir。NR推理复用分支保持独立。


## 11 条审查意见的处理

| 编号 | 修复与验收 |
|---|---|
| 1 | 缺少同尺寸光流时用现有 FlowAdapt 缩放向量。1440p 输入配 1080p 光流与 4K 超分，4 帧均完成 DLAA，首帧后的 3 帧使用运动信息。 |
| 2 | NGX 核心和光流失败时去掉依赖的 DLAA。DLAA 执行失败标记为独立组件，进入现有恢复或设置回滚。三类恢复的设置合同均通过，FXAA／SMAA／MLAA 保留。 |
| 3 | `dual-any-hdr` 和 `dual-any-sdr` 支持按算法对独立运行。8 组 HDR 双重超分通过，每组 3 帧、6 次超分。4 组补测的 SDR 组合均在零运动和光流模式通过。设置事务单独通过，D3D12 错误 0。 |
| 4 | 完整验收、失败证据与路径见本文件、WORKLOG、两份 AGENTS 及 `FINAL.json`。 |
| 5 | DLSS SR 单次和双重均恢复超出 BT.709 的 scRGB 负分量。饱和色块平均差 0.000151 scRGB、亮度比 1.000283。FSR 同一块平均差低于输出精度、亮度比 1.000000。 |
| 6 | DLAA 只增加自身纹理及每实例 256 MiB 的 SDK 估算。已有分段超分仍按其独立规则预留 NR、VFG 和 20% 余量。 |
| 7 | `ShortcutOverride` 接收者可以是 Qt Quick item。记录最近一秒内与配置匹配的按键事件；未收到对应事件时区分绑定和实测事件。真实窗口 F8 开停均捕获 `QQuickRootItem`、VK `0x77`，两次 `ageMs=0`。 |
| 8 | 预设库测试每次使用独立 scratch，同一 TEMP 连跑两次均通过。 |
| 9 | 抗锯齿 QML 用例在 cleanup 恢复原模拟能力表，完整 QtQuick 175/0/0。 |
| 10 | DLAA、DLSS SR 和 FSR 共用 HDR 恢复的资源、描述符和 GPU 派发。 |
| 11 | 静态图、非 NVIDIA 或禁用增强的 DLAA 旁路只创建输出纹理，不预留指导纹理及模型空间。静态图逐字节一致，DLAA 求值 0。 |

## 最终验收

- 本机 RTX 5070。10 目标构建 72.3 秒通过，标签 `2.1.3-record-hdr-dlaa-test`，命令见 WORKLOG。程序、88 个 QML/JS 模块文件和 66 个着色器与当前源码／构建核对通过。
- `audit_checks.py` 的 30 次独立调用全通过。包括 10 项 GPU 图用例、8 组 HDR 双重超分、4 组 SDR 双重超分（各两种运动策略）、设置事务和单元测试。设置与兼容 274/0，词库缺译和占位符问题均为 0。原合并双重用例超时在 `logs/claude-record-hdr-dlaa-20261108/codex/dual-any-hdr.txt` 保留；按算法对隔离后通过。
- 两层同尺寸 DLAA 实际处理 12 帧，累计 24 次求值。HDR DLAA 的 3328 个原有负分量像素全部保留，合成亮色块亮度比 1.0022。GPU 图的 D3D12 调试层未报错。
- 最终启动器实际打开测试视频。F8 开始和停止，中间进入全屏，结束后退出全屏，总计仅两条录制触发。H.264 录制 1280×720，431 帧，约 14.39 秒，丢弃 0。`ffprobe -count_frames` 读回 431 帧。操作与结果见 `AUDIT-RECORDING.json`。
- 现场旧日志有 27 次启动、27 次停止、28 个正常收尾文件，其中 1 次因 HDR 格式变化分段。旧日志缺少具体触发来源，现场误触仍待新版日志确认。未用现场采集卡或 AMD 实卡复验。

## 交付位置

- 启动 `E:/项目/Veyra/tests/claude-record-hdr-dlaa-20261108/启动录制HDR与DLAA测试.cmd`，使用独立 `manual-profile`。
- `E:/项目/Veyra/reports/claude-record-hdr-dlaa-20261108/FINAL.json` 为最终回执。原始 11 条为 `CLAUDE-REVIEW.json`，逐条结果为 `AUDIT-RESOLUTION.json`。
- 测试证据保存在任务 logs 下的 `audit-20261108-174250`、`manual` 和 `quick-suite-software-audit.txt`。沿用既有 build 与 app，新增一段约 9.1 MB 的录制证据及测试日志，无新增完整包。
- `app213` 是与正式 ZIP CRC 一致的 1632 文件对照副本，逻辑 1,079,122,186 字节。删除请求被自动审批拒绝，理由 `blocked by policy`，仍保留，未回收该空间。
