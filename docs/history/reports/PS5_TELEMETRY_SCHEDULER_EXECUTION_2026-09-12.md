# PS5遥测、停帧、FG调度与解码施工记录

目标active，未完成，不发布。当前分支codex/ps5-scheduler-telemetry-decode；开工adff31b，tag checkpoint/ps5-scheduler-decode-pre-repair-2026-09-12。方案PS5_TELEMETRY_STALL_FG_DECODE_REPAIR_PLAN_2026-09-12.md，用户要求全部完成。

## P0第一步：独立接收/解码观测

SessionInbox追加一秒有界窗口、完整视频payload速率/接收与解码fps、最后视频/音频/解码时间、实际解码调用平均/P95和接收后等待。不是UDP线速、不是网络端到端延迟。RemotePlaySource在submit/drain位置计时和计数，SessionSource安全共享只读inbox快照；Engine::snapshot直接读这些线程安全数据，不再只能等GPU循环推送。

FrameFlow记录最后GPU就绪、提交和Present；独立monitor每秒输出remoteplay-progress，即使解码owner卡在调用内仍能观察接收进度。视频停顿在Streaming状态下1秒触发受限关键帧恢复，后续间隔2秒，一次停顿最多3次；恢复后重置次数，最终沿用30秒失败边界。没有强杀线程、无无限重建。尚需故障注入与用户停帧复测，不宣称冻结根因已经修复。

实际命令：out/remoteplay/audit-20260911/build-product.cmd；日志logs/ps5-p0-progress-build.log（31步成功）、ps5-p0-progress-tests-build.log（2步）。scripts/run-short-test.ps1运行veyra_remoteplay_boundary_tests，TimeoutSeconds30，logs/ps5-p0-progress-boundary.stdout.log，exit0；新增用例验证独立快照的真实解码耗时和一秒后旧样本不显示为实时。

尚未完成：P0视频阶段UI/完整恢复矩阵，P1文件提前增强和FG恢复，P2面板，P3硬解。不能将此节点称为整体完成。

## P1：文件有界提前增强（第一轮实机通过）

复用唯一 GPU owner 的 LiveGpuScheduler，文件预览也使用最多 2 个保留 lease 的增强批次；启动仅在有界预取就绪后打开音频锚点，稳态保持音频主时钟，按每帧真实 PTS 呈现。暂停、seek、变更继续取消旧批次，不放宽过期容忍。修复 paused seek 被背压分支阻断。音画偏差改用最后实际呈现帧；性能不足提示改为最近一秒，不使用累计计数常亮。

真实原版 NR SHA256 符合 AGENTS。产品构建 build-product.cmd 成功（logs/ps5-p1-lookahead-build2.log）。使用 logs/audio-jitter-20260911/4k30fps.mp4（8秒）和实际 RTX5070/NVIDIA runtime，--native --nr --fg-multiplier 2 --no-sr：25秒 smoke 退出0，240源帧、238生成，稳态每秒约60次提交、expiredGenerated=0、播放1.000x。logs/ps5-p1-file4k30-runtime.log。与修前用户4K30样本不是相同媒体，不宣称全部媒体60fps保证。

15秒 --smoke-controls 同参数回归退出0：暂停→暂停seek到1秒→恢复完成，controlsStep=3，稳态提交约60fps，过期0，声音领先最终0.40ms、绝对偏差P95 0.88ms；logs/ps5-p1-controls-runtime.log。尚需长媒体、持续欠速、恢复、单帧EOF与XeSS回归，UI改动正在进行，硬解未实现。

## P1追加：恢复预算、暂停跳转与清理

加入FgRecoveryBudget：原帧耗时、FG追加GPU耗时、恢复预热分开；最近有效成本按墙钟过期并限制64样本，settings revision切换清空，普通时域重置不抹掉吞吐预测。欠速后250ms有界探测，连续两次可达截止时间的机会覆盖预热/有效生成，每次仍检查期限；不强制呈现过期帧。旧同步文件播放分支已删除，只有图片保留单次处理路径。取消原有陈旧混合预测字段。

动态负载回归通过：生产Engine/实际NR+DLSS2X，测试专用owner工作延迟70ms（明确不是测量/伪造GPU耗时），约12呈现fps，56个源预览机会跳过，音频没有新增停机；移除延迟且不改settings revision，恢复至46呈现fps且19生成呈现fps时通过阈值，后续仍继续恢复。logs/ps5-dynamic-recovery.stdout.log，65秒watchdog，实际6.8秒exit0。FgRecoveryBudget确定性9项新增检查通过，logs/ps5-fg-budget-unit.log。

文件欠速完整回归第一次有2项失败：4K输入的SR不执行（源/目标同4K，错误测试夹具），以及暂停seek显示位置未及时发布（产品问题）。改用1080p30→4K保证实际SR，并在Present成功时直接发布位置后，logs/ps5-final-file-overload2.stdout.log全部通过：NR/SR/FG实际执行、音频addedWaits=0、延迟有界、重建/播放seek/暂停seek/恢复/关闭通过。第一次失败保留于logs/ps5-final-file-overload.stdout.log，不能删除失败记录。XeSS原生4K文件音频连续性logs/ps5-final-xess-continuity.stdout.log退出0。

## P2/P3：界面和真实硬解

LiveStatusPanel首屏区分实际呈现、请求目标、软件平均/P95；默认阶段均值，详细产出/过期/等待/计数折叠。PS5输入取独立接收快照，不再用采集卡空计数。独立接收/解码/呈现年龄显示卡在哪一步，暂停和设置重建单独显示。TelemetryWindow也接PS5数据。

PS5连接页新增自动（优先硬解）/CPU软件/D3D12VA硬件，重连生效，本地INI仅存解码选择、不写凭据。自动硬解失败在下一关键帧切CPU；明确选硬解失败报错，用户可改自动/软件，不伪装硬解。使用Engine同一D3D12设备，实际AV_PIX_FMT_D3D12输出进入现有图；每个硬件输入AVFrame由图双槽保留到消费fence完成，避免FFmpeg提前复用。正常路径无CPU像素下载。

Native source H264/H265真实硬解、PTS重排通过；同片段软硬YUV逐像素对照max_error=0，颜色range/matrix/transfer与PTS一致，仅诊断测试允许readback。故意让FFmpeg格式选择失败，自动模式下一关键帧转软件与指定硬解报错分支都通过（预期日志-1094995529）。logs/ps5-p3-fallback-final.stdout.log，45秒上限，实际<1秒exit0。开发中颜色比较首次构建因Rational无operator==失败，改为to100ns比较，logs/ps5-p3-color-build.log保留；首次fallback夹具缺少独立config导致拒绝，补齐真实协议消息顺序后通过，logs/ps5-p3-fallback.stdout.log保留。

生产图导入硬件纹理专项：VEYRA_TEST_FILE_HW_DECODE=1仅测试开关，4K30原生NR+DLSS2X文件，20秒smoke退出0，logs/ps5-p3-hardware-graph-runtime.log，稳态约60呈现/秒、过期0。此结果是本机解码+图验证，不是PS5网络实机硬解验收。旧probe中“descriptor后硬解约8fps”是历史诊断，不能覆盖本次实测。

当前还有最后统一gate、UI快照、source gap/EOF等回归与最终文档存档。用户已追加授权：完成后正常关机，明天由用户实测PS5；不强杀游戏，不push/release。PS5原始冻结未重现，受限恢复与阶段诊断不能宣称彻底根因已确认。

## 2026-09-12 最终本机交付节点

上述待办已经执行：最终 product 构建通过（logs/ps5-final-product-guard-build.log），Remote Play OFF 构建通过；Native CTest 69/69，文件源23项、调度/呈现/UI/边界回归通过。统一 delivery gate 45.29秒通过，证据 logs/delivery/82d949d48503403495e10de477ce70d8/result.json。最终 EXE SHA256 7AA2452E05E6B423DB1C7A4D3D66B9C9D2BFE41262C337B8D70010EBE301E61A。

45秒4K30原生NR+2X实际长测：1284原帧呈现、1247生成帧呈现、过期5，末段59fps；并非零丢帧或全硬件60fps承诺。400ms源间断恢复后240帧原帧全部呈现、EOF无取消；单帧EOF无挂起；3X、动态欠速恢复、XeSS音频连续性均通过。所有单次测试均小于300秒。软硬H264/H265色值比较最大差0，强制硬解无设备拒绝和自动回退最终回归通过。

UI最终使用真实窗口DC抓取并检查非黑图，已人工查看overview/advanced快照；20次模式切换及20次全屏通过（logs/ps5-final-ui-visible.log）。此前隐藏子窗口PrintWindow黑图仅是无效测试方式，不作为产品通过证据。

新增 docs/PS5_REPAIR_ACCEPTANCE_2026-09-12.md 汇总测试入口、日志与验收边界，中英文README更新开发分支状态。桌面“Veyra PS5 测试版”指向 out/remoteplay/product-repair/veyra.exe，移除smoke/禁用增强启动参数。代码节点3405717。未push、未发布，未提交SDK/DLL/模型/测试媒体。

下一步唯一任务：用户明天实际连接PS5验收新增硬解、负载恢复与偶发停帧。原冻结未复现，原始根因不能断言；本轮没有真实PS5/采集卡复测，不把本地码流测试冒充网络验收。用户授权收尾后正常关机，不使用强制关闭参数。
