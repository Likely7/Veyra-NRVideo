# 专业模式视频位置与播放条遮挡

基线34f479e，分支codex/pro-video-geometry-20261012。产品只改Main.qml、ProPage.qml，共享布局尺寸；EXE与引擎、SDK、运行库、模型保持原字节，没有合并main、推送、打包或发布。

## 原因

4557ab6（2026-10-11“正常内容节奏/限速状态”修复）给状态区增加开启选项和原因，ProPage将原172像素统计区改成随文字动态增高，但Main.videoRect仍扣固定172。这是本次普通窗口遮挡的直接引入点，属于上轮验收遗漏：当时只检查Qt文字和卡片边界，Qt截图不含原生视频子窗口，未比较真实HWND与videoArea/transport。

还存在更早的尺寸重复维护：Main固定32像素标题、36像素播放条，ProPage实际标题可变成72、播放条34或68；Main视频宽度额外少14像素。因此窄窗口还会出现纵向位置差40像素和更严重的底部遮挡。

真实生产EXE、GTA视频、相同私有环境的对照：

| 界面源码 | 普通窗口播放条遮挡 | 窄窗口播放条遮挡 |
| --- | --- | --- |
| 4557ab6之前（e8300d9） | 0（旧宽度仍少14、高度少2） | 本次只取普通窗口作引入点对照 |
| 本轮基线34f479e | 开启状态选项后56逻辑像素 | 900宽时90逻辑像素 |
| 本次修复 | 0 | 0 |

## 修正

ProPage持有videoColumnWidth、metersHeight、videoCardHeight和videoViewport，视频卡片与Main原生窗口使用同一份尺寸。videoViewport变化时事件同步，不轮询，不增加生产Timer、update或requestUpdate。标题换行、窄播放条、状态说明、语言变化和检查器宽度变化均使用实际响应式值。打开源前仍可计算非零尺寸，保留原本避免1×1交换链的要求；极简、节点、导出和全屏既有几何策略保持。

## 验证

路径以下均以E:/项目/Veyra/为前缀。脚本使用Python311的`-X utf8 -B -I`；GTA媒体为用户指定的4K Native.mp4，运行在私有data-dir，全程顺序测试。

- `scripts/acceptance/pro-video-geometry-test.py after --renderer d3d12|software --scale 1|1.5 --donor-app E:/项目/Veyra/tests/fg-capture-recovery-20261012/app --media E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4`。四种组合各13场景（52组合），实际Win32原生子窗口矩形与QML videoArea一致，播放条不重叠；包含选项开关、简中/英文/日文/繁中、720宽、检查器650宽、极简返回、全屏及返回。GPU1.5组使用永久脚本，其余首次使用相同临时夹具；收尾再用永久脚本完整复跑软件100%组。回执见reports/pro-video-geometry-20261012/after-*.json所指日志。
- 引入点对照：同脚本`before --baseline-ref 4557ab6^ --case normal-active-policy`，日志`logs/pro-video-geometry-20261012/before-d3d12-1-20261012-160403/receipt.json`。进程正常完成，普通窗口遮挡0；总几何判定仍失败是旧14/2像素间隙，不能称旧版所有几何都正确。当前基线负对照13场景记录于155358批次，普通56、窄90像素遮挡真实复现。
- Qt6.8.3 `qmlcachegen --only-bytecode -o E:/项目/Veyra/build/pro-video-geometry-20261012/Main.qmlc qml/Veyra/Main.qml`，ProPage同命令，均通过。本次是外置QML变更，复用34f479e已测EXE，无需重编C++，没有冒称全量clean构建。所有真实GUI正常播放、正常退出，无QML绑定/类型或D3D12错误。
- `tmp/pro-video-geometry-20261012/perf.py before|after d3d12|software`：4K视频、单层原版NR、内部1080p、强度1、自动调控关闭、无SR/FG、普通优先级。四组产品日志均确认requested=2 actual=2；夹具外部class采样因句柄权限不足未成功，不作为确认依据。各组约45秒，NR实际运行后取18个一秒GPU统计样本。GPU界面NR中位数5.852→5.839ms、UI提交中位数36→34次/秒；软件界面5.751→5.901ms、38→40次/秒，视频提交均30fps。仅后台窗口短测（activeSamples=0），不当作前台完整性能A/B或普遍提速证据。
- 同四组早期15–30秒进程CPU读数为GPU0.2476→0.2756 CPU秒/秒、软件0.2594→0.2271；原始全部采样保留。GPU25–42秒为0.2460→0.2566。短样本有波动，不能宣称CPU下降；没有增加持续全窗重绘路径。33ms插值、250ms状态和GPU优先级代码SHA保持，见validation.json。
- `git diff --check`通过；控制脚本verify保护65旧树：64完全不变，claude/xess-flow-tune-20261012有外部并行修改，收尾时该分支也有外部提交推进，均原样记录且未触碰/合入；其余原分支tip和main不变。源码/QML及所有EXE/DLL/模型/着色器的原件对照见reports/pro-video-geometry-20261012/validation.json。没有新增proprietary文件入Git。

实际矩形通过Win32 EnumChildWindows/GetWindowRect读取，与QML mapToItem结果及Qt DPR比较，允许原生物理像素舍入≤1像素。屏幕截取曾被其它窗口遮住，已删除该副本，不作为通过证据。150%使用Qt缩放参数，不冒充双屏真实DPI热切换验收；未连接反馈者采集卡，但本次视频几何与源类型无关，实卡仍待反馈者确认。

## 失败和边界

首次引入前对照只换Main/Pro，遗漏NodePage对StatusSummary的依赖，导致加载失败（160222批次）。已改为完整替换历史QML树并在结束恢复当前源码；该失败没有计入通过。夹具补独立VEYRA_LOG_FILE，避免默认追加日志混入前一轮，并让GUI加载失败返回失败；发生异常只结束夹具自己启动的进程。性能四组外部GPU class读取均失败，后两组增加query权限仍返回ACCESS_DENIED，原错误保留；实际普通优先级由每组独立产品日志中的requested=2 actual=2 state=1 ntstatus=0确认，没有将外部失败当成通过。

可用程序在tests/pro-video-geometry-20261012/app/veyra_qml_ui.exe，测试驱动已从Main移除，所有QML恢复当前源码。显示版本继承2.0.6-capture-fg-fix-20261012，EXE SHA256 c815906da37f79a0aa36446ab47563cd4ca6c57a9451217ce0452b40de0d9d35。最终commit/tag/bundle与完整验证路径见reports/pro-video-geometry-20261012/final.json。

收尾缓存清理两次被自动审批以“blocked by policy”拒绝，未返回更具体理由；第二次已经先独立核验12个明确路径均在本轮tmp内、无reparse point，并使用固定LiteralPath，仍被拦截。没有绕过审查继续删除，40,063,628字节缓存和8个空目录保留，记录cleanup-plan.json/cleanup.json。源码和验证证据不受影响。
