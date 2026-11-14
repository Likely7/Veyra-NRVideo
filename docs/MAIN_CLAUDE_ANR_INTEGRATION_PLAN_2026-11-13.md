# Claude 与 Codex/ANR 合入本地 main（2026-11-13）

用户明确要求整理Claude的后续修复与Codex当前内容并合入main。允许本地整理、提交、构建/回归与本地main合并；不包含push、Release、驱动设置修改或关机。此前各分支“不合main”的历史限制到此被本次授权覆盖。

起点main8332c8a。Codex链bc4cbfa包含此前尚未合main的全效果遮罩/NR节点、美颜删除/PSNTools、去重界面精简、去Boost、送帧/采集/日志、自动识别/60→30，以及完整ANR FG。Claude活动树基于6e3747a，20个未提交文件原字节存档在E:/项目/Veyra/archives/codex-main-claude-anr-20261113；在独立树原样提交d4be12d，不在Claude脏树提交、stash或重置。

Claude内容：到期帧先送、图前/VFG后不等待的检查点；1%low按Present调用开始计时；图像比较提前停止并延后参考拷贝；Reflex睡眠异常关闭；日志关闭同步；0–1ms自动期限余量；可完成重档补帧的相位上限；长期拒绝补帧时真实帧即就绪即送、连续8对准入后恢复。保留VFG高档窗口DWM竞争、预算探测偶发慢帧等现有未解决结论；不擅自重启已撤销的等待/预读/预算实验，不新改GPU优先级。

合并在独立worktrees/main-claude-anr-20261113 / codex/integrate-claude-anr-20261113进行。只有AGENTS和WORKLOG追加冲突，保留双方；C++和翻译自动合并后做语义检查。旧main检查点checkpoint/pre-main-claude-anr-20261113、Claude快照分支及ANR分支保留可追溯。

独立build/tests/logs/reports/tmp任务codex-main-claude-anr-20261113，全在E:/项目/Veyra；不复用/覆盖Claude共享构建。并发2，构建≤900秒、每测试进程≤300秒。先构建组合产品和相关工具，运行日志/时序/调度/预览/合同/预设/ANR设置、i18n及QtQuick；再复测真实图ANR全倍率/调试层/HDR与原生DLSS共存、生产引擎自动去重/60→30/VFG/ANR回放、切换/保存/重启、产品导出。若设备未被其他进程占用，做T911动态/静态按实际画面标注的短测；不抢设备、不停用户进程。没有其他GPU或屏幕端测量不能扩大验收。

相关检查通过后合并本地main；同时更新当前入口与执行记录、保留精确源码/运行组件身份和回退点，确认原Claude树与旧ANR树未被改动。源码Git不进DLL/SDK/模型/测试视频。ANR核心许可仍未明确，只在本机私有运行目录提供组件，不公开分发。旧Release二维码/公开资产保持原样。
