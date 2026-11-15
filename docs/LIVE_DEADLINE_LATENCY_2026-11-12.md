# 实时送帧与采集延迟续接结果

分支 `codex/live-deadline-latency-20261112`，基线 `1cd2ff7`，最终应用 `2.1.5-latency-test`，EXE SHA256 `4b0e2b7b82250ad22747cd71014c06a1938804a7094ccdf8bcf04a854a212a20`。本地测试交付，未合 main、未 push/Release，不关机。

用户要求沿 Claude 的未完成排查继续做完。依据是 `E:/项目/Veyra/reports/claude-low-latency-audit-20261112/FINDINGS.md` 及原始 run1；方案见 [施工与审查判据](LIVE_DEADLINE_LATENCY_PLAN_2026-11-12.md)。本轮完整回执、原始日志和复算脚本分别在 `E:/项目/Veyra/{reports,logs}/codex-live-deadline-20261112`。

## 最终改动

- 仅已经就绪、正在等呈现期限的实时原生帧启用精确唤醒。可等待定时器先睡眠，最多最后 1 ms 自旋；采集到帧、GPU 完成、停止/暂停/seek 都能退出。GPU 未就绪和显示容量轮询不启用自旋。保留单 owner、两批队列和现有源时钟/相位，不增加前视或整体显示延迟。
- SDR 实时图内补帧在 upload/color/flow/SR/NR/FG 的检查点，可由同一 owner 送出旧租约中已经到期的帧。呈现必须使用独立 command ring；不进入 XeSS、HDR 输出、文件、图片、导出或 reset。不在回调中重建图、应用设置或收集图查询；允许解析旧帧状态并登记其消费 fence。
- 物理采集无补帧允许在图提交后即录制/提交呈现，由现有呈现队列的 GPU-side Wait 保障 producer/consumer 顺序；完成观察和资源租约继续保留。没有像素回读或逐 pass CPU 等待。
- ready-driven 帧不再把固定初始锚点打印成持续增长的迟到时间。日志增加 `deadlinePaced`、`duringProcess`，未知 ready=0 不参与延迟复算。界面 FPS、1% low 的计算方法未修改。

## 本机结果

RTX 5070、Ryzen 7 9700X（16 逻辑线程）、2560×1440/320 Hz，T911 3840×2160 60 NV12，SDR；NR 用一层 1080p，基础超分关闭。测量窗口在稳定等待后开始，窗口和全屏分别记录。源时钟、NR 设置和倍率相同；各例使用独立数据目录，音频关闭。“旧版”指上一轮去 Boost 的测试程序 `787cf3e8…`，不是用公开 2.1.5 冒充同源对照。

| 同条件对照 | 旧版 | 最终版 |
|---|---:|---:|
| DLSS 2X 提交率 | 约 120 FPS | 约 120 FPS |
| DLSS 2X 逐帧返回间隔 P99 换算 1% low（窗口/全屏） | 105.39 / 106.66 | 115.19 / 115.76 |
| DLSS 2X 原帧迟到中位数（窗口/全屏） | 0.954 / 0.922 ms | 0.095 / 0.091 ms |
| VFG 2X 逐帧返回间隔 P99 换算 1% low（窗口/全屏） | 107.08 / 109.10 | 114.67 / 115.36 |
| VFG 2X 原帧迟到中位数（窗口/全屏） | 0.304 / 0.304 ms | 0.001 / 0.001 ms |
| 无补帧：回调到 Present 返回中位数（窗口/全屏） | 1.548 / 1.564 ms | 1.067 / 1.081 ms |

其余逐帧结果见本任务 `FINAL.json`、`quiet-*.json`。DLSS 4X＋60→30、FSR 3.1 2X＋垂直同步、XeSS 2X＋可变刷新、VFG 2X＋普通 Reflex/可变刷新均完成窗口/全屏短测。XeSS 生成呈现由提供方负责，逐帧应用日志约 60 次调用不能冒充其生成后的屏幕刷新率，界面 1% low 继续显示未知。

DLSS 同条件 CPU 采样约增加 0.06–0.13 个逻辑核，折合本机 16 线程总量约 0.4–0.8 个百分点。这个代价来自精度与检查点；不保证别的 CPU 相同。

这些是软件提交和 CPU 观察数据，不是手柄到屏幕、采集卡硬件或光学端到端延迟。原始 run1 的全屏 DXGI 估计等待已约 1.9 ms，不能沿用“全屏始终合成”的未分析结论。当前 compositionMode=-1；PresentMon 2.3.1 因权限不足启动失败，没有改系统权限，也没有添加未经证实的“独立上屏”状态标签。4070 Ti SUPER/160 Hz 原现场掉到 30 的驱动原因、其他显卡及 HDR 实卡收益不在本机通过范围。

## 失败、纠偏与未采用的实验

1. 最初夹具将每帧 stdout 再同步写入 `console.log`，同时还写正式引擎日志。`Logger::write` 在全局锁内对 stdout 每条 fflush。连续批测出现 100–300 ms CPU 停顿、源丢弃和显著低帧；这些差的样本完整保留，控制流程返回 PASS 不当作性能通过。
2. 改夹具为 stdout→NUL，只保留正式引擎日志与 stderr；没有改产品 logger，也没有删诊断事件。同一最终 EXE 的新旧九组对照、18 段测量全部通过实际帧率/长迟到/错误检查，另有 VFG 两段各 20 秒稳定测量。重复输出的 I/O 干扰与观察现象相符；线程栈监测三轮没有抓到原停顿，不能声称已用调用栈证明它是唯一原因。不要再用复制每帧 stdout 的批测窗口给产品作帧率验收。
3. 第二版试过“精确唤醒后立即直接 advance scheduler”，EXE `e330f08f…`。其所在批次有长停顿；它也曾在单独复测中正常，因此没有证明该捷径是停顿原因。最终仍撤掉，保留原统一控制/完成观察顺序；不拿该实验的 116 读数当最终交付成绩。
4. 首轮最终 DLSS 测试在第三段被正常关闭，不能算完整通过。后续完整生命周期和无重复输出对照已补齐。
5. AMD 合成测试首次因私有运行目录缺少两份测试 identity DLL 返回 2，补齐到任务临时目录后 128/0、D3D12 Debug 0 错误；没有把假 provider 放入可交付 app。文件生命周期第一次错误传了 Off，但夹具要求 On，故断言失败；改正确参数后全部通过。依赖前缀只有 FFmpeg DLL，没有 CLI；合成测试输入由本机已安装 FFmpeg CLI 生成，产品仍使用已核验的 patched DLL。

## 构建、回归与交付

使用 `scripts/acceptance/build-recording-handoff.py --task codex-live-deadline-20261112 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.5-latency-test --jobs 2`。首次 `--reconfigure` 六目标 103.4 s；最终产品/呈现/合同三目标 29.2 s；AMD 图目标 8.7 s。各回执在同任务 logs/build-*。

最终 scheduler 156、时序 172、预览 57、合同 301、等待/事件 9 项通过；等待微测旧 P99 0.988 ms、新 P99 0.010 ms（只证明该次微测）。AMD 合成图 128/0、Debug 0；文件 DLSS 原生生命周期及 VFG 持续播放通过。相关输出见 `QA-final.json`、`remaining-QA.json`、`QUIET-MATRIX.json`。测试 max 300 s、构建并发 2。

`IDENTITY.json` 核验 128 个运行组件/便携 DLL 与已有清单 SHA256/大小一致，旧供体 EXE 仍为 `787cf3e8…`，QML 临时探针已移除。测试 app 沿用只读硬链接依赖，仅替换本任务 EXE、Main 和本地测试元数据。许可和默认组件保持。

入口：`E:/项目/Veyra/tests/codex-live-deadline-20261112/启动送帧与采集延迟测试.cmd`。独立 manual-profile，不覆盖正式应用或原用户设置。构建继续复用 `E:/项目/Veyra/build/claude-screen-recording-20261022`。清理/占用结果在同任务 `CLEANUP.json`。系统自动审批以 `blocked by policy` 拒绝删除 6 个本轮复制的测试工具（22,372,352 字节），未给出更具体原因；本轮没有执行该清理，也未尝试替代删除方式。原始日志、私有配置、临时缓存和工具均保留在本任务目录。依赖约 1.06 GB 是共享硬链接的路径逻辑大小，不是本轮新增卷实占或回收空间。启动器已实际启动，独立配置无自动采集，随后观察到正常退出；界面截图被遮挡未用作通过证据。

本轮为自审和实测，没有独立 Reviewer 验收声明。
