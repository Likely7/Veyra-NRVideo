# Claude 修复整合验收记录（2026-10-11）

本次只执行用户授权的本地 main 整合。起点 `5ba186a91e868c28b4b5ec46c9166aa321d90998`，隔离分支 `codex/merge-claude-fixes-20261011`。六个已完成 Claude 分支及已验 UI 完整 tip 依次 --no-ff 合入，所有负优化 revert 随完整分支保留；来源和顺序见 [方案](../plans/MERGE_CLAUDE_FIXES_PLAN_2026-10-11.md)。实测候选 `9c14ee28a9175aa326fb0aa09c15bfa5808e0f0e` 已通过功能 merge `3a08ce8cf28de6d0f83c7b025bd4a7f54fe37b27` 合入本地 main，完整 Git 树相同；随后仅更新四份收尾文档，产品源码与实测输入保持一致。最终 main、checkpoint/bundle 和保护核对由 `E:/项目/Veyra/reports/merge-claude-fixes-20261011/final.json` 确认。

## 保留的修复与适配

- VFG 跳过预算组时保留输入历史与负载提示；自动降低倍率、连续 CUDA 等负优化保持撤回。
- 呈现统计和重复/禁用但已完成计算的组参与配对节奏校准；交换链预等实验保持撤回。DXGI Present→vblank 统计不是屏幕光学延迟。
- NVIDIA 信息浮窗/游戏加加的兼容选择、8 秒无进展栈/小型 dump 诊断与有界退出。现场卡死仍是候选修复，不能把注入成功当真实根因已复现。
- CPU 可见采集画面的内容去重/有界节奏与 60→30；硬解来源保留没有 CPU 图像时的原有兜底。微小物体可能不落在 64×36 采样点，最迟保持上限会留帧；未宣称所有画面无误判。
- 文件音频独立读包、2.5 秒/4096 包/16MiB 有界队列及慢读诊断；seek 仍同步清空与预填充，稳态端点线程不读盘。
- Elgato 8 位卡内色调映射及非 MK.2 P010 混合色度范围，手动范围优先；官方控制属性失败边界保留。
- 软件 UI 图标与虚线使用缓存 SVG Image，GPU 原 Shape、33ms 统计不变，整窗重绘和 QuickPrivate 方案保持撤回。

源码自动合并没有冲突。只有 AGENTS/WORKLOG 的独立追加历史冲突，用三方对照保留全部记录，没有对源码或翻译做 union。人工复查共同接点发现 `hangPreviousStatus_` 声明在 `watchdog_` 后，逆序析构会先释放回调读取的字符串；本轮在 EngineController 析构体内等待 worker 后显式 reset watchdog，提前解除回调，避免退出竞态。没有顺手更改其它模型、倍率或调度默认值。

## 本轮构建和回归

`py -3.11 -I scripts/acceptance/merge-claude-fixes-build.py` 成功：50 个变更/头文件依赖闭包对象重新编译，19 个 EXE 重链接，76.6 秒。显式包含新 EngineWatchdog、Bridge MOC、翻译资源、DeckLink 及 YuvToLinearRgb shader。其余已验对象、静态库和 SDK 以 SHA 核对复用，**不是 clean 全量重编**。GUI SHA256 `d4e5aa25cf78120c51eb7793165452101b10798bfd67e1c6cdc724f6ba9d078a`，仍是本地 2.0.6 开发标识，没有改发布版本。

`...merge-claude-fixes-configure.py` 实际配置当前完整 CMake：QML、PS5、Moonlight、Xbox、DeckLink、Magewell 和现有实验运行接口保留；Watchdog 编译单元、8MiB 回放测试栈、无 QuickPrivate 均核对。私有滚动 probe 只替换主程序测试驱动，链接本轮全部产品对象，生产 EXE 不含该驱动。

`...merge-claude-fixes-test.py` 26 项全部 exit0：效果合同/时间线、HDR/采集颜色、呈现生命周期、设置重建及回退、关闭项编辑的列表/节点两路径、SR 已有回归、数字输入/菜单/组件、VFG GPU、60→30、音频抖动/起播跳转/欠速/慢读/旧读法对照/倍速、反复跳转和文件端点。文件播放使用用户 GTA 原视频；内容节奏使用已有测试渲染文件，不替换用户媒体。

`py -3.11 -B scripts/acceptance/merge-claude-fixes-gui.py pixels`：生产 Main/ProPage、150% DPI、3 NR 层、80 步滚动至100px，QScreen 只读已呈现像素。旧 QML 对照残影1237像素、完整重绘后的头部越界209像素；合并后 OBS/RTSS 兼容选择均0，37图标 Ready，空闲3秒分别3/2帧，没有持续绘制。PNG、逐项 JSON 和人工截图复核保留。这里没有重新执行 OBS 实际采集或 RTSS 真实注入。

`...merge-claude-fixes-gui.py watchdog`：12秒延迟注入在8047ms报告，10帧栈、1个1239193字节 dump，随后恢复并正常退出；永久阻塞注入在8172ms报告，10帧栈、1个1245672字节 dump，关闭后3015ms结束等待并终止本进程。两个进程 exit0，预期 engine-hang 错误日志单独校验，未把其它错误过滤掉。这证明诊断/恢复/退出机制，不证明反馈者的驱动卡死根因。

## 普通调度性能核对

`...merge-claude-fixes-gui.py performance`：GTA 原4K30、NVIDIA原版 NR 单层内部1080p/强度1/风格0，SR/FG/自动调控关，实际 GPU 调度 class2。每例40秒有效播放、29个前台一秒样本。NR 数字为一秒 GPU 均值的中位数；CPU 为进程 CPU 秒/墙钟秒，既不是整机百分比，也不是纯 UI 耗时。

| 界面 | NR 前→后 | UI 次/秒 | 进程 CPU 秒/秒 |
| --- | --- | --- | --- |
| gpu | 5.847270 → 5.862661 ms（+0.26%） | 36 → 35 | 0.262846 → 0.237482 |
| obs | 5.850922 → 5.820384 ms（-0.52%） | 35 → 36 | 0.219905 → 0.253086 |

GPU 对照是原已验完整包与整合产品；软件对照使用同一整合 EXE，仅切换旧/新 VIcon 和 ProPage，以排查绘制退化。四例视频提交均30fps，不冒充物理屏幕刷新率。原短测守卫：UI增加≤3次/秒、NR增加≤3%、CPU增加≤15%。首轮软件 CPU +15.09% 超线，因此没有直接计通过或放宽阈值；产品不改，以新→旧倒序额外复测两例。倒序 NR 5.949594→5.796090ms，UI 35→36次/秒，CPU 0.238783→0.235530秒/秒。倒序本身及两种顺序平均 CPU 0.229344→0.244308（+6.52%）通过同一15%守卫；所有 NR/UI 守卫保持。首轮失败和复测数据一起保留，不把这几次短测当严格性能等价证明，更不宣传算法提升或跨机保证。统计 QML SHA `e88541114240fad417f7d1f08bde0b8236964ff7e9005c8fc8214743c006958b` 保持。

## 证据、失败与边界

所有新构建、日志、私有配置、缓存和诊断在 `E:/项目/Veyra/{build,logs,tests,tmp,reports,archives}/merge-claude-fixes-20261011`。本任务 `reports/.../validation.json` 固定构建/测试/像素/性能回执 SHA；`archives/.../start.json` 和 source-before.bundle 为不可变保护点，原54工作树/496脏文件、七来源分支、原测试包1626文件逐项核对保持。

初次完整配置遗漏旧缓存的显式 protoc 路径，退出1；补齐旧已验工具位置后配置通过。随后检查脚本误用正斜线匹配 Windows Ninja 反斜线，断言失败；修正检查后完整配置/私有 probe 编译链接通过。日志均保留，不计为产品故障或伪造通过。GUI Python -I 隔离模式无法读取既有用户站点 psutil，GUI按既有 -B 方式运行，无新依赖安装。i18n 合并前后均29项未翻译/语言，placeholder问题0，未新增缺口。

Elgato/Blackmagic 真卡、现场 5060Ti 卡死和所有叠加软件组合、AMD 串流及长时间显存增长没有在此次本机全面验收。Claude 文档中的旧实验 FPS/音频改进只作原记录，本轮没有冒用那些数据。NVIDIA 显存持续增长仍未根治。没有推送、GitHub 发布、重新打包或关机；原测试包不代表已包含本轮修复。
