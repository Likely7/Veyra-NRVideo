# Veyra Repair v2 — 2026-09-08 本机修复交付

本次确认的三个缺陷及源帧计数疑点已修复，最终独立联合18项软件检查通过；新上下文只读 Reviewer 对这次修复范围给出通过。保持项目总状态 `needs_review`，旧Phase字段不变，不宣称实卡/公开首发/完整性能验收完成。

程序：`out/build/x64-release/veyra.exe`，也可双击 `Veyra.cmd`。
SHA256：`1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266`。

## 本次修复结果

1. CFR导出：不再从前两帧直接反推精确帧率，也不盲信avg/r_frame_rate。最多120帧只保存PTS元数据，以量化相位交集验证候选；整数tick帧率严格检查格点。优先一致的标准标称率，其次一致标准候选，再验证非标准标称率。元数据预查后从头打开文件，保留负起始PTS，输出中继续逐帧验证。短量化片无法证明唯一原始精确帧率，候选选择在日志中明确记录。
2. 设置失败：事务前保存完整previousDesc，失败恢复全部字段，包括enableNvofStandalone。NR开关失败注入证明回滚后NR与NVOF继续工作。注入发生在应用层，不是假装触发真实驱动故障。
3. B5诊断：NVOF初始化/caps/释放、DLSSG/SR Create/Evaluate/释放等失败记录Error与原始返回码；成功仍为Info。Logger支持NVOF status/st及NGX/SEH。合成错误码单测验证报告内容，未主动制造硬件故障。
4. 源帧身份：参数重算保留cachedPacket.sequence，不增加sourceFrames；图处理次数单独增加。实际日志有source=31 / totalRead=31 / graphProcessed=32 / cached=true / nvofStandalone=true。
5. 测试上限按用户最新澄清改为**每次调用最多300秒**，联合入口对子检查传递本次剩余时间；历史累计耗时仅报告，不清零，不再当成停止阈值。

## 构建与测试

实际最终构建：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
```
exit0，3.9694163秒，证据 `logs/repair-v2/r0-20260907/build-review-final.log` / `.json`。本次修复的其他构建分别保存在build-review-fixes、build-cfr-candidate、build-cfr-rounding的log/json，构建耗时独立记录。

实际最终联合：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/repair-v2.ps1 -Root . -MaxRuntimeSeconds 300 -Case all
```
exit0。证据 `logs/repair-v2/joint-a077b89fd39348e4a49f4195c9d4e416/result.json`。本次联合测试进程耗时 **71.0278864秒**；本次恢复会话新增测试进程合计 **85.8584581秒**；全部修复历史累计 **346.3985650秒**（含失败，非当前上限）。各result.json包含精确命令、应用/工具哈希、真实退出码、stdout/stderr路径。

| Case | 入口退出码 | 进程耗时秒 | logs/repair-v2 下的 run-id |
|---|---:|---:|---|
| contracts | 0 | 0.055 | `d834f505bd2347bb94500b7dedfaa94f` |
| presets | 0 | 0.071 | `04659cc35aa843b380ac0ae46e304f08` |
| shaders | 0 | 0.291 | `593b8038e0114098a6baa9e53c4e8da7` |
| parameters | 0 | 2.390 | `fd39d80d44d64b99be5793cc36a58ced` |
| fg2nr | 0 | 2.848 | `24402784eaa24b0c85e4e39b52fe9996` |
| sr4k | 0 | 5.035 | `9011917db686414eba3e3db4d0b27892` |
| mfg3 | 0 | 3.463 | `5dc7b0521fd14c42a80040f3402a44fd` |
| mfg4 | 0 | 4.117 | `11f8b464b7184cacbde7fc6257bc0ebf` |
| player3 | 0 | 6.756 | `afc9905627fe487792aef79d6b2f4469` |
| settings | 0 | 8.785 | `ec2c8902c9e54d519a09898449a31f26` |
| uicases | 0 | 8.781 | `1c3b0630795e4cba9bcc584c50c470be` |
| rollback | 0 | 8.785 | `d6391221195d4cbaa4d000daa5b2798d` |
| rollbackflow | 0 | 8.785 | `daa66f6c43ec472bb99bf33deace0295` |
| exportaudio | 0 | 2.751 | `c5432f50e744466f8ac171805b6d5b1d` |
| exporthevc | 0 | 2.675 | `5f87a5e8439b482d98eb48d3a144053d` |
| exportmkv | 0 | 2.761 | `bcf6947898c94f3dadb8614cb27cc4e4` |
| exportvfr | 0 | 0.443 | `f872813a9eaf4dffbfb77f485cc67b32` |
| cancel | 0 | 2.237 | `eb33b950acec4454949700aef85cb066` |

VFR子程序预期exit1，拒绝检查通过；cancel子程序预期exit3，入口验证其为正常取消；没有把非零退出码伪装成程序成功。

中间失败保留：38654a0a8a184b57815d2ba75ff7d58c 的MKV导出程序exit0，但错误输出119960/499被包时间戳检查抓到；017154fb30b443a28cd7c222577ef776 的小数帧率量化边界单测失败。随后针对实际错误修复；全部旧失败日志保留。此前测试记录及历史边界见REPAIR_PROGRESS。

## 真实输出及功能边界

- 2X：12源帧、11有效中间帧；3X：12源帧、22有效中间帧；4X：12源帧、33有效中间帧。合成纹理内容/位置/非线性混合检查通过；NGX真实Create/Evaluate成功码0x1、SEH0，NVOF status0（各run原始stdout日志）。仅像素验证probe允许读回合成图。
- SR4K：1920×1080 source/flow，3840×2160 base/FG/output，1920×1080 NR。12源帧、11有效生成。4K输入实时档与原生NR额外证据沿用本修复早先67ef0bf4657949acb1407d6abbe17445、bd6002fad6b64601a0a27596f7b322f9，非旧Phase gate。
- 最终H264+音轨、HEVC、量化MKV导出均为24源帧+69 generated+3 Hold=96编码帧；240/1fps，包PTS逐一检查i/240，H264音频case保留1条AAC。输出样本为1080p；本次三个缺陷修复后没有重跑4K视频导出，不把它写成本次新实测。
- 最终3X播放含暂停/seek控制测试：204源帧、400有效生成，lateness P95 1.66ms。最终4X UI测试含比较模式抑制生成帧提交：385源帧、1152生成，处理统计约60.05fps。实际提交日志带PTS/host/fence，物理扫描/光子延迟未测，不能把生成数当成屏幕显示数。
- **性能仍有限制**：设置测试在播放中切到SR4K并保留4X，最终统计39.75源fps、lateness约2170ms。此case验证事务与源不重开，未证明该组合4K60实时。NR1080回填解决了原生NR4K负载耦合，不会消除4K SR/FG及资源重建成本；不能把功能PASS说成所有组合实时性能PASS。

GPU query实测，以下为median/p95毫秒，非CPU：1080 NR+4X color0.043/0.049、NVOF队列区间1.006/1.118、NR5.958/6.367、residual0.066/0.067、FG子帧1=1.229/1.421、子帧2=0.570/0.748、子帧3=0.566/0.737、FG batch2.414/2.705、blit0.010/0.050。SR4K+NR1080+FG2合成短测：SR2.735/2.826、NR5.909/6.034、FG2.561/2.701。不同阶段可能有包含/重叠，不能把这些统计简单相加称端到端延迟。

## 参数与B1–B5

| 项目 | 结果与限制 |
|---|---|
| B1 同帧比较 | GPU source/base references与源ID/epoch/revision绑定；按住、切换、分屏及reference模式内部UI检查通过。生成帧不冒充同一时刻原图。 |
| B2 性能 | GPU query与CPU阶段分开；最新值、提交数/频率及未知项明确。物理扫描和光子延迟未测。 |
| B3 预设 | version1、白名单、原子写入/坏文件保护/CRUD/default检查通过；Desired/Applied、整套事务及冻结导出设置已接产品。 |
| B4 光流 | SDK FAST20/MEDIUM10/SLOW5，实际应用日志；性能/质量档先前repair-v2实测通过，当前联合使用平衡档。30/50/60是内容识别目标，非重采样。 |
| B5 诊断 | bounded64、去重、错误码、脱敏预览/复制；本次补齐失败级别；没有主动制造真实GPU移除故障。 |

参数实验逐项Applied revision1–9及原始setter日志见fd39d80d44d64b99be5793cc36a58ced。合成样本强度、明暗、结构、风格、自动遮罩、总变化强度均观测到像素变化；肤质/UI修正虽然设置提交，但像素不变，**效果未证实**。模型键与typed setter沿用现有本地实验接口，无新造官方预设。残差darken/brighten/color/luminance为Veyra独立GPU后处理，不修改DLL。

## 修改文件

本次三个问题/计数修复涉及：
- include/veyra/engine/CfrTimeline.h；include/veyra/media/FFmpegDemuxer.h、src/media/FFmpegDemuxer.cpp；include/veyra/source/IFrameSource.h、src/source/MediaFileSource.cpp；src/engine/VideoExportJob.cpp。
- src/engine/EngineController.cpp；include/veyra/pipeline/EnhanceGraph.h、src/pipeline/EnhanceGraph.cpp；apps/veyra/main.cpp。
- src/ngx/NvOfSession.cpp、DlssFgBackend.cpp、DlssSrBackend.cpp；src/base/Log.cpp。
- tests/unit/RepairContractTests.cpp；CMakeLists.txt；scripts/acceptance/repair-v2.ps1。
- 本报告、DELIVERY_STATUS、USER_GUIDE、WORKLOG、REPAIR_PROGRESS、loop/STATE/EVIDENCE/JOURNAL。

前续R1–R8完整实现模块与变更见REPAIR_PROGRESS。`logs/repair-v2/final-worktree-status.txt`保存整个未提交工作树清单，包含用户已有改动，不能把全部清单都归因于本次修复。`r0-20260907/before.patch`保留初始dirty资产。

## 独立审查与下一动作

新上下文Reviewer `review_known_fixes`只读审查最终代码、18项联合证据与应用EXE哈希，结论：“本次限定修复范围通过，无剩余阻塞发现。”它未修改文件、构建、运行测试或访问采集卡。此前只读Reviewer已复审其他R1–R8工作，本次结论不扩大为完整原规格或旧Phase重新放行。

7个protected-before控制面哈希均未变（final-protected-check.json）；NR root/staged与addon身份符合固定值，未加载addon。git diff --check exit0。未提交、push、发布、上传或打包。

已知未执行：本修复版本实卡验收、实际扫描/光子延迟、多显示器物理DPI、长时间稳定、广泛画质比较。专有runtime分发仍阻塞。唯一下一动作：用户用当前EXE做实卡与实际体验验收；若要要求SR4K+4X达到60源fps，需另以该具体组合的性能瓶颈作为任务验收，不把本次事务功能短测冒充性能达标。
