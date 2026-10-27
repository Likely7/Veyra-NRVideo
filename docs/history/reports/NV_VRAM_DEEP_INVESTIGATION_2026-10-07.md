# NVIDIA 显存持续增长：原始日志重算与生命周期深查

日期：2026-10-07。用户最新纠正：**实际反馈主要发生在 NVIDIA，AMD 尚未发生过这类现场问题。** 当前优先排查 NV；AMD 静态池风险保留为独立、未实卡证实的代码隐患。

结论状态：**已确认 NV 的稳态增长，发现 NR 生命周期调用缺口；尚未确认现场分配所有者，不能宣称已经修复。** 本轮没有修改产品代码、DLL、模型、驱动或用户配置。正常播放测试使用既有发布包/本地 fix2 包的隔离副本；没有压力程序、竞争负载、人工占满显存、子 Agent、merge、push、Release 或关机。

工作树 `E:/项目/Veyra/worktrees/nv-vram-deep-20261007`；分支 `codex/nv-vram-deep-20261007`；起点 `aeb544cb63718787b9488be283ae54fc6ec3cc13`。独立不可变 `archives/nv-vram-deep-20261007/start.json` SHA256 `6c4d9e48a0149b74ca5919533a723908af37f4d719bfb7da6a0d0a9ab1df5a27`，包含 1707 个已有版本控制文件及两份日志身份。不修改旧任务 guard 或 baseline。

## 1. 找回最早的 NV 原始日志

两份日志仍在本机微信文件目录，上一轮没有找到，不能继续写成文件不存在。原件位置：

`E:/wechat/xwechat_files/wxid_kcmlgkgv70mv22_d585/msg/file/2026-10/`

| 文件 | 字节数 | SHA256，与 10-02 封存一致 |
|---|---:|---|
| `veyra-qml(8).log` | 18796703 | `03ba7ef42c6e55f7bf20909566fa635ed3eabd3e728c8ea257812d7e38bfb070` |
| `veyra-qml(12).log` | 9360645 | `d7f9793f1f38520a9a05988fbb5036a0244ca425fc332772e877b1298c6593c3` |

字节一致的工作副本位于 `E:/项目/Veyra/logs/nv-vram-deep-20261007/inputs/`。独立分析脚本 `reports/nv-vram-deep-20261007/analyze_field.py` 生成 `field/*-analysis.json` 和完整采样 CSV：log8 有 5039 个使用量样本，log12 有 2353 个；按进程会话、设置 revision、全屏状态、实际 graph generation 分段，再按每次 reset-lifecycle 分出无重置窗口。重置前后各排除两秒，避免将延迟观察计入稳定段。另用原始行独立核对关键区间。

共同现场条件：RTX5060Ti 16GB、驱动 `32.0.16.1692`（616.92）、采集 3840×2160/60 NV12、NR 内部 1920×1080、光流开启、SR/FG 关闭；显示 4K/165Hz、225% 缩放。运行库日志标为 `community-Lecram-RTX50`，**原现场未记录 DLL SHA，不能声称与本机当前 DLL 字节完全一致，也不能外推 NVIDIA 原版 NR 同样中招。** 文件路径分别指向 2.0.0beta6 / 2.0.0 包，不能把本轮 fix2 源码当作现场执行代码。

## 2. 无重建、无重置时仍持续增长

以下时间均为日志 UTC；使用量是 DXGI 进程 LOCAL 统计，MiB 为 2²⁰ 字节。超过 Budget 或显卡物理容量的计数不能解释成板载显存真的装下了这些字节，也不能由这一个值区分驻留、换页和私有缓存。

| 原始区间 | LOCAL MiB | 净增 | 平均 MiB/min | 日志事件数：图初始化 / 重置 / 场景切换 / SetWindowPos / SetWindowRgn |
|---|---:|---:|---:|---|
| log8 session7，10-01 13:09:43.188–13:16:04.144 | 1849→18501 | +16652 | 2622.67 | 全部 0 |
| log8 session15，10-01 13:56:55.541–14:03:12.762 | 1819→17325 | +15506 | 2466.35 | 全部 0 |
| log12 session3，10-02 13:10:25.501–13:15:00.690 | 4660→7696 | +3036 | 661.95 | 前四项 0；SetWindowRgn 49次 |
| log12 session3，10-02 13:27:04.767–13:33:37.474 | 15799→20687 | +4888 | 746.82 | 全部 0；期间仅重建交换链 |

这里的“图初始化 0”针对区间内部，开始前已有图；最后一行看门狗交换链重建有日志，**没有处理图重建**，不可称为全链完全不变。前三行没有看门狗事件。窗口API栏统计已记录事件，未进行该旧二进制的全量API跟踪，零日志不等于已证明没有任何未记录的重复调用。log8 session7/15 的命令槽高水位 4、呈现高水位 1；FG 实际计数为 0。它不是普通应用呈现队列越积越长、补帧倍率过大或每次改设置多保留一张图能够解释的问题；这些高水位也不能替代驱动内部队列的跟踪。

log12 还有同 revision、同处理图的窗口区间 13:17:11.959–13:18:15.743，LOCAL 固定 9522MiB；继续处理约 60fps。**帧数递增并不证明像素内容仍在运动**，采集游戏可能因焦点变化停动；窗口/全屏像素尺寸也不同。因此全屏是已有触发线索，尚不是已证实的独立因果变量。

短反例：log8 12:31:38.880–12:32:07.130，全屏、NR/光流均关闭，1199→1199MiB、处理 1688 帧。只有 28.25 秒，支持继续拆分 NR/光流组合，不能作为长期无泄漏验收。

## 3. 处理图没有配对调用 NR DLL 的 Shutdown1

当前产品调用链：

1. `src/pipeline/EnhanceGraph.cpp:1201` 创建 NR adapter；`:1205` 直接调用该模块的 `snippetInitExt`。
2. 关闭时释放 Feature、销毁 NR/core parameter blocks。
3. `:3294` 对 adapter 恢复 IAT 并 `unload()`；**没有显式调用 `snippetShutdown1`**。
4. NGX Core 随缓存生命周期继续存在，或在设备会话关闭时走 `NgxCoreHost::shutdown()`，这是 Core 链接入口，不能直接等同于已调用 NR DLL 的独立入口。

原始 log8 有 42 次 NR Init_Ext、42 次 Core Shutdown、**0 次显式 NR Shutdown**；log12 分别 34、34、0。没有 leaked parameter block 告警。此缺口早于 2.0.4 的 Core cache 优化，不能把优化版本当作最早故障起点。

交叉核对：本项目 `tools/nr_harness/frame_loop.cpp:854`、`tools/media_probe/main.cpp:915` 都在 Feature/参数释放后调用 `snippetShutdown1`；[Magpie 固定提交 27c5df9 的 NR 实现](https://github.com/SAOG0721/Magpie/blob/27c5df91177a29b33be612e98274169f3d2fca49/src/Magpie.Core/DLSSNRFilter.cpp) 也将 Init/Shutdown/IAT 归属给共享 runtime session，在最后一名所有者退出时执行。固定下载副本 SHA256 `46c75e125e172c1c0bffc811a7bb9e9cbb8ebeb6627dd80cafaa7ca374937712`；本次阅读的两份本地副本均与该固定身份一致。

**定性：这是需要优先核验的生命周期缺口，不是已经证实的 16GiB 泄漏根因。** 原始日志中的 `core-host Shutdown1=0x1` 无法单独证明 NR 私有会话已关闭；但缺少 wrapper 日志也无法证明 Core 没有间接清理。`FreeLibrary` 后打印“snippet module freed”也没有核对模块是否实际卸载，更不能等价为资源全部回收。需要显式所有权/模块驻留/分配退休证据。

即使证明遗漏关闭会保留 NR 缓存，它首先解释的是“停止/重建回收不完整”，**不能仅靠这一点解释单次 Init 后、不重建、不 reset 的每帧持续增长**。不能把两类问题硬凑成一个根因。

## 4. 本机正常负载对照

RTX5070 12GB / 616.56，2560×1440/320Hz / 100% 缩放；私有 profile、GPU Normal/class2。使用既有 M2 4K60 视频只做无重编码拼接成 240 秒，媒体 SHA256 `380acd1aee98bea66e3d270130d4d47d9053c8ae3e7840db54ee609018ac6070`。单层 Lecram NR / 内部1080 / NVOF / FG关 / 自动画面调控关；40秒窗口、150秒全屏、30秒退窗。只测试正常播放，不人为制造泄漏。

| 对照 | 稳态全屏 LOCAL | 主 Qt 提交 | 呈现合同 | 结果 |
|---|---:|---|---|---|
| 发布2.0.4，专业页 | 2059→2059MiB，去转换预热后142.796秒 | 8652帧 | Present(0,0) | 未复现，harness通过 |
| 发布2.0.4，极简页、减少动效、隐藏全屏控件 | 1831→1831MiB，141.663秒 | 0帧；视频继续60fps | Present(0,0) | 未复现，harness通过 |
| 本地fix2，同极简配置，附停止后15秒观察 | 1800→1800MiB，142.614秒 | 0帧；视频继续60fps | Present(1,0) | 播放采样未增长；尾部断言失败，见下 |

旧 EXE `d01329aa6d9fbd6ac2d1eee294782f4cb9d233490595ed2e3e1a9ea12c16b2d6`；fix2 EXE `65fc23e33598d7efd77ea50ba8062a89beaacb29eb14369ffc78d03563312626`；两包 Lecram DLL 均 `f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc`。每轮复核母包全部文件 SHA 不变。测试 QML 只改自有副本，原件/用户 profile 保留。

独立 PresentMon 2.3.1 只针对自有 PID 和唯一会话名尝试抓取，返回 exit6 / ETW access denied。没有提升权限、改组权限或停止其他 ETW 会话；后续不重复尝试。**本轮没有实际 PresentMode/独立翻转/MPO 分配证据，不能宣布这些路径已被证明。**

可用的只读诊断已验证：`Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory` 可按自有 PID 记录 WDDM DedicatedUsage / SharedUsage / TotalCommitted，结合进程 PrivateBytes / 工作集 / 句柄与实际模块驻留；保留每个 LUID/phys 实例原值，不把这些值与 DXGI usage 或逻辑资源大小强行相减后命名为“驱动泄漏”。脚本、命令参数、原始日志、失败及完整收据在 `E:/项目/Veyra/reports/nv-vram-deep-20261007/` 与同任务 `logs/`。

停止后的独立采样给出反证：fix2 全程样本中，停止播放后 NR DLL 与 nvofapi64 均不再驻留；WDDM DedicatedUsage 比停止前末次采样减少 **1316.18MiB**，SharedUsage 减少54.86MiB，PrivateBytes 减少1481.53MiB。引擎日志已有 graph/Core/source/device 关闭完成。**本机可以回收大量资源，不能把未显式调用 snippet Shutdown 直接定性为本次现场根因。** Core DLL 仍映射也不等于 Core 会话仍初始化。

该次 harness 在尾部错误地要求 `nrActive=false`，返回 exit3，原失败收据保留。`nrActive` 实际读取旧 snapshot；停止后 `running=false`、FPS=0，但旧 NR 标志仍可保留，不能拿它当资源存活接口。修正测试断言后，独立正常播放/停止契约用例 **51.313秒 exit0**：再次确认模块卸载、DedicatedUsage减少1203.90MiB，未用这个短用例替代长稳。另一47.047秒的候选用例因未先确认上一进程退出而被主动结束，不计结论；事后按UTC核对实际两个播放器生命周期相隔283ms，没有重叠。后续 runner 加入启动前拒绝其他播放器仍存活的检查。

本轮没有做重建周期或同一PID30分钟验收，没有复现4K显示器/225%/616.92/真实采集组合。旧发布版与fix2的默认呈现合同不同，表中已明确；两者都未增长，**不能把默认 VSync 改动宣传为显存修复**。

## 4.1 可交付的只读现场采集工具

新增 `scripts/diagnostics/collect-nv-vram.ps1`，采用本轮已在真实RTX进程验证的外部只读采样方式。每次目标采集时长最多280秒；不启动/停止播放器、不注入、不改驱动或任何应用配置、不要求管理员、不创建ETW。校验PID、EXE路径与进程启动时间，避免PID复用混淆；输出必须显式指定，父目录须存在，拒绝覆盖已有文件。模块首次出现时记录磁盘文件版本、字节数和SHA256，缺失/不可访问字段明确记 unavailable；无GPU计数的进程记 `gpuAvailable=false`，不伪填零。文件SHA不等于内存映像校验；运行中替换文件会混淆身份，需记录并重启独立对照。

示例，PID与绝对路径须换成实际目标；本机研发输出仍只允许E盘：

```powershell
powershell -NoProfile -File scripts/diagnostics/collect-nv-vram.ps1 `
  -ProbeProcessId 12345 -OwnedExe 'E:\Veyra\veyra_qml_ui.exe' `
  -OutputPath 'E:\项目\Veyra\logs\nv-vram-deep-20261007\field-01.jsonl' -DurationSeconds 280
```

继续记录同PID可再运行下一段，使用新的输出文件名，不重启播放器；记录用户切换的时刻并结合应用日志，脚本不猜测全屏、处理效果、DPI或像素内容。PowerShell解析及无GPU进程的缺字段检查通过；实际GPU采样已由前述同API helper完成，不能把本工具当作分配归属追踪器或显存修复。

## 5. NV 优先的下一步与放行条件

1. **先查 NR runtime 的实际所有权和回收。** 将直接 Init/Shutdown/IAT 的所有权设计在 device/runtime 会话层，所有 Feature/参数先释放，最后所有者再关闭 runtime；近期图缓存和多 NR 层必须共同计数。不能在每层析构时各调用 Shutdown，把别的存活 Feature 一起杀掉。对照最后会话退出前后内存、真实模块驻留、已知自有资源/堆退休；分别记录 Core 与 snippet 的结果和 SEH。
2. **稳态增长独立定位。** 受影响 NV 同机、同输入内容、同显示像素尺寸，分别测试 NR关、NR开零运动、NR开NVOF。再单独比较 Present(0,0)、VSync 和正常窗口/无边框全屏；记录驱动版本、NR实际SHA、主 Qt 帧数、source内容差分、三套 fence 的提交/完成差、图/Feature generation。没有独立 NVOF 对照前，不能将“NR+NVOF”归咎其中某一个。
3. **获得 GPU 分配/销毁时间线才判所有者。** 现场经用户启动的独立 ETW/GPUView/PresentMon 或同等诊断只覆盖目标进程；结合自有资源分类计数，区分自有资源、NR/光流私有对象、Qt、呈现/驱动附加处理。模块出现不能证明功能开启，更不能证明谁分配了持续增长的资源。现有 NVIDIA App 功能入口/手动选择保持，不复活此前已撤销的插件强制拦截。
4. **只在匹配条件下做修复 A/B。** 首个明确反例就是本机：Qt停止提交帧而NR继续正常工作，未自动造成大幅增长。1440p/616.56/文件播放的未复现不能覆盖4K/225%/616.92/真实采集。换驱动、DSR、注册表、NVIDIA App、HDR或用户其他应用设置不由 Agent 自动执行。
5. 每个本机测试进程 ≤300秒，构建 ≤900秒，不并行运行 GPU 测试。不用造压填满显存验证；一旦真实增长超过诊断阈值或逼近预算就结束当轮、保留现场。最终现场验收仍须原受影响设备、同一进程/同条件观察，报告实际样本时长及增长率；不能把若干次独立短测相加冒充同PID长稳。

已有可选全屏显存保护是缓解触发、退出全屏后停止新增的措施，默认行为本轮不改。它没有释放既有保留量的证明，不能写成“根治”。旧看门狗的交换链/处理图重建多次失败也不再作为任何模块被排除的证据。

AMD 的静态裸资源池与模块卸载隐患继续归档；当前无对应 AMD 现场，优先级降为独立后续项。上述 NV 证据与修复验收不以它替代。
