# 下一对话接管提示词

把下面整段作为新对话的第一条消息。只有用户实际发送它，才代表授权新 Agent 开始其中的代码修改。

```text
接管并完成当前 Veyra 修复版本。我的本条消息授权你在这个项目目录内修改应用代码、测试和未锁定交付文档，范围仅限下述功能；不授权发布、上传、push、制作安装包、下载或更换 proprietary runtime、修改驱动/OBS/远程软件、保存我的采集画面，也不授权修改任何保护哈希或降低门禁。

项目目录：C:\Users\123\Desktop\Veyra DLSS Video Player

先完整阅读：
1. AGENTS.md
2. docs/REPAIR_EXECUTION_PLAN_2026-09-07.md
3. docs/MAGPIE_FEATURE_BACKLOG_2026-09-07.md
4. docs/DELIVERY_STATUS.md
5. docs/CAPTURE_LATENCY_FIX_2026-09-07.md
6. docs/WORKLOG.md 当前顶部

真实状态：旧 Phase 5–7 的 passed/goal complete 不能作为这个版本的可靠发布证据。历史上 Phase5/6 被提前转到同一 short delivery gate，Phase7 又只是包装同一 gate；CONTROL_HASHES、loop-gate 预期 hash、review prompt 也曾被改过。后来软件的可见卡顿、SR4K 高负载和 FG 体感失败证明当时完成口径不足。保持 needs_review；不要复用旧共享 evidence 来宣布新功能通过。

严格禁止：
- 不改 loop/CONTROL_HASHES.json、scripts/loop-gate.ps1 中的期望 hash、scripts/gates/phase5.ps1、phase6.ps1、phase7.ps1、delivery.ps1、loop/REVIEW_PROMPT.md 来制造通过。
- 不用 rehash/rebaseline 消除 preflight 问题；发现控制面冲突只记录，不启动 Goal，不修改控制面，继续在普通授权任务中做应用实现；若规则真的阻止安全施工，准确汇报给我。
- 不覆盖现有 dirty tree，不恢复已删除的 validation/fixed_clips/test_h264_1080p.mp4，不清理不属于本轮的修改。
- 不复制 Magpie GPL 源码/shader/UI，不加载 renodx add-on，不 patch 或联网替换 nvngx_dlssnr.dll。
- 不把重复帧、线性混合、生成计数或成功 Present 冒充真实 DLSSG 输出；不把 CPU 时间冒充 GPU/显示/光子延迟。
- 测试总运行时间最多 300 秒，构建时间单独记录；不要占用采集卡，我最后亲自实卡验收。

本次必须完成，不留到以后：
1. SR4K：拆开 SR 输出尺寸和 NR 内部尺寸。默认实时档保留 4K 底图、约 1080p NR 变化回填；原生4K NR可选且明确标注性能；视频导出默认原生4K。
2. FG：先证实真实2X内容/纹理/PTS/呈现，再接SDK原生3X/4X；采集、播放、NVENC导出共用FrameBatch，不重复/嵌套2X伪造。
3. DLSS5参数：强度、局部明暗、局部结构；肤质/风格/自动遮罩/UI修正作为实验项；另做总变化、暗化、亮化、色彩、明度变化合成。参数帧边界实时生效，不重开视频。
4. 中文UI和完整全屏：按钮、F11、Alt+Enter、双击、Esc、多屏/DPI/Alt+Tab恢复；字幕和播放器UI在FG后合成。
5. B1：同sourceFrameId的原图/增强瞬时切换与分屏拖杆。生成帧没有同一时刻原图时明确标“真实帧对比”，不能错帧。
6. B2：GPU timestamp展示颜色/SR/NVOF/NR/残差/FG/blit；CPU调度分栏；源/有效生成/提交/可测显示FPS分开，未知为null。
7. B3：用户预设新建/复制/重命名/删除/默认；整套settingsRevision事务应用，原子保存，损坏回退；不保存设备、DLL或可执行命令。
8. B4：NVOF性能/平衡/质量请求，按本地SDK capability映射；SR/NR/FG共享一次估算，显示请求与实际，fallback有原因。
9. B5：最近错误、详情/建议、脱敏复制、打开本地日志目录；固定容量与重复合并，绝不自动上传或附带素材。
10. 图片、视频播放、视频导出、采集入口继续共用 FrameSource→EnhanceGraph→FrameSink；不能做三套实现。

按 docs/REPAIR_EXECUTION_PLAN_2026-09-07.md 的 R0→R9 推进。顺序不可倒置：
- R0 只读盘点、记录当前 EXE/runtime/dirty tree和旧证据边界。
- R1 建 FrameBatch、ResolutionPlan、Settings、Metrics、DiagnosticEvent 合同和失败测试。
- R2 修真正2X及稳定呈现时间线。
- R3 修 SR4K/残差回填并接B1 reference。
- R4 泛化3X/4X及Presenter/NVENC/CFR/音轨。
- R5 接模型/残差参数和B3预设事务。
- R6 接B4共享光流档位和30/50/60内容节奏。
- R7 接B1/B2/B5实际产品UI。
- R8 完成汉化、布局、全屏和交互。
- R9 新建 scripts/acceptance/repair-v2.ps1，在共享300秒预算内做联合短测；它不得包装或改写旧gate，也不得更新旧Phase状态。随后让一个新上下文Reviewer只读审查本轮diff和新证据。

实施纪律：
- 每完成一个闭环就同步到实际 veyra.exe，不把核心实现堆在 probe/main.cpp。
- 所有 NGX/NVOF/HRESULT/SEH、extent、format、frame/batch/subframe ID、fence、settings revision和GPU timestamp进日志。
- 正常路径无GPU→CPU全帧回读、无每pass CPU fence wait、无无界队列；采集ingress仍是容量1 latest mailbox，内部A/B历史有界。
- 资源设置变更采用新epoch、有界排空、原子切换；UI线程不能join长任务。参数失败保留上一个AppliedSettings。
- runtime只使用项目当前固定身份和绝对路径；不从Magpie、游戏或驱动缓存拿文件。
- 不要为了追求测试全绿修改验收标准。一个问题最多尝试三个能增加新证据的不同方案，随后如实记录。

完成标准：
- 不是“编译成功”或“Evaluate返回成功”，而是执行方案中各项内容/资源/时序/UI/导出验收有新证据。
- B1–B5逐项有实际验收，不得以面板存在代替底层工作。
- 300秒运行预算内未覆盖的项目明确写未执行，不伪造。
- 实卡性能、真实显示扫描/光子延迟和公开分发许可留给用户/后续，不能宣称已验收。
- 最终报告修改文件、构建命令、实际测试秒数/退出码/日志、Create/Evaluate错误码、已知风险、唯一下一动作和EXE SHA256；保持 needs_review，只有新Reviewer对本轮明确结论后才更新相应状态。

现在开始执行，不要先重复问我已经在这条消息里决定的产品选项。如果遇到会改变范围、需要外部系统修改、发布或控制面重写的事项才停下来问我。
```
