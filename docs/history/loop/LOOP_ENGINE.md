# Veyra Loop Engine v1.1

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

本文件把 Goal-based loop 落成 Veyra 项目的无人值守执行协议。它不是一句“持续工作直到完成”的提示词，而是一套可恢复、可验收、会停机的状态机。

设计依据是 Claude Code 团队所述的 Goal-based loop：人工触发，以“目标完成或最大回合数”为停止条件，用确定性完成标准和第二 Agent 审查减少自我放行。Veyra 在此基础上增加 GPU/专有文件安全门禁和磁盘恢复状态。

## 1. 目标与真源

目标是完成 Product Spec 定义的 Launch V1（Phase 0 到 Phase 7）：物理采集卡、播放器、图片/视频导出三个闭环及 4K SDR、D3D12 NVENC、产品恢复/诊断全部完成。它不是最小 MVP。质量需要同源证据；不得回避比较，也不得无证据宣称“更好”。

信息优先级：

1. 实际文件、编译器、测试结果和运行日志；
2. 根目录 AGENTS.md 的安全与许可证规则；
3. 本文件的循环与停机规则；
4. VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md 的施工细节；
5. VEYRA_PRODUCT_SPEC_V1.md 的产品边界；
6. loop/STATE.json、loop/EVIDENCE.md、docs/WORKLOG.md 中已经由证据确认的状态。

文档与实际结果冲突时，以实际结果为准，但必须记录冲突；不允许偷偷改掉验收标准来让结果“通过”。

## 2. 为什么采用单写者

Veyra 的 D3D12 device、资源状态、NGX 参数、history/reset、颜色传递和播放时钟高度耦合。多个 Agent 同时写同一个 checkout，合并成功也可能留下时序错误。因此：

- 主 Agent 是唯一 Maker，负责修改工作区。
- Reviewer 只读检查 diff、代码、日志和门禁结果，不改文件。
- 每个 Phase 只在本阶段门禁通过后启动一次新上下文 Reviewer。
- 不创建并行写入 Agent。未来真要并行，必须使用独立 Git worktree 且任务文件完全不重叠；V1 Loop 默认禁止。

## 3. 持久文件

每次 Goal 回合开始都必须先读：

- AGENTS.md
- loop/LOOP_ENGINE.md
- loop/STATE.json
- loop/BACKLOG.md
- loop/INBOX.md
- docs/WORKLOG.md 的最后一个条目
- git status --short、git diff --stat、git log -5 --oneline（Git 初始化后）

职责：

- loop/STATE.json：机器可读的当前位置、失败计数和下一动作。
- loop/BACKLOG.md：按依赖排序的原子任务，不是随手愿望清单。
- loop/JOURNAL.md：每轮先写假设，轮后写实际结果。
- loop/EVIDENCE.md：通过门禁的证据索引；不复制大日志。
- loop/INBOX.md：只有需要用户新授权或外部条件的真实阻塞。
- docs/WORKLOG.md：面向交接的阶段记录。
- loop/STOP：用户紧急停机开关。存在时保存状态并立即退出；Agent 不得自行删除。

STATE.json 不是事实的替代品。若它与 Git、文件或日志冲突，先执行 RECONCILE，修正状态后再工作。
STATE.phases 必须始终保留 0 到 7 的完整 ledger。进入下一 Phase 时只解锁相邻项；gateEvidence、reviewEvidence 和 commit 不得填入推测值。

### 3.1 已通过阶段的撤销协议

若后续审查证明旧 gate 与当时 Product Spec 不一致，必须撤销阶段状态，不能以“已经有 checkpoint”为由继续：

1. 保留旧 commit、日志和 EVIDENCE，标明它们只证明哪些组件；禁止改写历史；
2. 将最早受影响阶段改为当前 `in_progress`，其 gate/review/commit 字段清空；
3. `lastGoodCommit` 回退到前一有效阶段的内容 checkpoint；
4. 所有后续阶段改为 `locked`，但后续未提交代码作为接管资产保留，不 reset；
5. 在 BACKLOG 写明旧 gate 的具体假通过条件和新的 fail-closed 条件；
6. 先让新 gate 在当前缺陷上失败，再修实现；
7. 新 gate 与独立 Reviewer 都通过后，才能重新写 `passed`。

2026-09-06 已按此协议重开 Phase 5：旧 gate 接受 manifest-only depth、第二次 1080p endurance 冒充 4K 位置证据，且 EnhanceGraph 仅计数。Phase 0–4 保持有效，Phase 6 实验代码保留但状态锁定。

## 4. 允许与禁止

无人值守时允许：

- 读取、编辑、构建和运行本项目；
- 初始化本地 Git 仓库并创建小型本地 checkpoint commit；
- 从 Playbook 指定的官方或上游项目获取依赖，放入已忽略的 local 目录；
- 在本机 RTX/NVIDIA 环境运行测试、GPU capture 和诊断；
- 启动一个只读 Reviewer；
- 在已有接口和验收定义内选择最小实现方案。

无人值守时禁止：

- push、创建远程仓库/PR、发布、上传 artifact、签名或制作含专有文件的安装包；
- 安装/降级显卡驱动、关闭安全机制、修改系统级 DLL 搜索路径；
- 搜索或下载其他“偷跑版”运行时，修改/重签 nvngx_dlssnr.dll；
- 加载、注入、链接、改名或分发 renodx-dlss5-1.addon64；
- 复制 Magpie GPL 源码进入闭源主线；
- git reset --hard、强制 checkout、清理用户改动或其他破坏性 Git 操作；
- 为通过门禁而降低阈值、删除失败测试、伪造日志或把 stub 当实现；
- 对许可证、缺失 SDK、需要账号接受条款等事项自行替用户作决定。

Goal 启动后，下列是只读控制面，Maker 和 Reviewer 都不得修改：

- AGENTS.md
- README.md
- .gitignore
- loop/LOOP_ENGINE.md
- loop/GOAL_PROMPT.md
- loop/REVIEW_PROMPT.md
- loop/CONTROL_HASHES.json
- scripts/loop-gate.ps1
- scripts/gates/README.md
- VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md
- VEYRA_PRODUCT_SPEC_V1.md

发现控制面缺陷时写 INBOX 并继续其他安全任务，不能自行“修正规则”。允许创建和修改 scripts/gates/phase0.ps1 到 phase7.ps1；它们属于被审查的产品验证代码，不属于控制面。

preflight 会按 loop/CONTROL_HASHES.json 校验控制面内容。任何 hash 漂移都 fail closed；不得通过重写 manifest 或基础 gate 消除告警。

BACKLOG 只允许更新状态、补充不降低原门槛的原子子任务；禁止删除或改写验收项。STATE、JOURNAL、EVIDENCE、INBOX、WORKLOG 是运行状态，可以按协议更新。

## 5. 状态机

    BOOT
      |
      v
    RECONCILE -> SELECT -> SPECIFY -> IMPLEMENT -> FAST_VERIFY
                     ^                         |          |
                     |                         |          v
                     |                         +------ REPAIR
                     |                                    |
                     |                                    v
                     +---- CHECKPOINT <- REVIEW <- PHASE_GATE
                              |
                              +---- next microtask / next phase

异常分支：

- STOP 文件存在：PAUSED。
- 同一 failure fingerprint 的 3 个不同方案都失败：ESCALATE。
- 连续 5 轮没有新增可验证证据：ESCALATE。
- 用户授权/许可证/硬件是唯一剩余阻塞：BLOCKED。
- 120 个完整 cycle 后仍未完成：NEEDS_HANDOFF，留下可直接续跑的状态。
- Phase 0 到 7 的全部完成定义真实成立：COMPLETE。

## 6. 每一轮的固定协议

### BOOT / RECONCILE

1. 若 loop/STOP 存在，更新 STATE.status 为 paused，记录原因并退出。
2. 读取第 3 节所列文件。
3. 运行：

       powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight

4. 对照磁盘、Git 和最后一次日志修正 STATE。崩溃后不从头重做；先确认上次 patch 是否已存在、命令是否实际完成。
5. 如果 preflight 因二进制身份、跟踪专有文件或 STOP 失败，不进入 IMPLEMENT。

首次初始化 Git 时按固定顺序：

1. 先确认 preflight 为 0；
2. git init -b main；
3. 若本仓库没有 user.name/email，只设置 repository-local 身份 Veyra Goal Agent / veyra-agent@local.invalid，不改 global config；
4. 用 git check-ignore -v 分别确认两个根目录二进制、runtime_local、third_party_local、reference_local、captures、logs；
5. 只显式 add 控制面、状态文档和现有方案，检查 git diff --cached --name-only 中没有专有文件；
6. 创建 chore: establish Veyra baseline，记录该真实 hash 为 STATE.baselineCommit；
7. 创建并切换到 agent/veyra-v1-loop，再用单独的 loop: record baseline commit 提交 STATE 指针。

如果 Git 已存在，不重新 init、不改分支历史，先识别当前分支和用户未提交改动。

### SELECT

只选择 loop/BACKLOG.md 中当前 Phase 的第一个未完成、前置满足且未阻塞原子任务。一个 cycle 只能有一个主任务，预计应能在一次小 patch 和一次验证中闭环。若更早任务因用户 EULA/外部硬件被明确标为 BLOCKED，可以继续同 Phase 的独立 TODO，但最终 gate 不能绕过该阻塞。

选择顺序：

1. 修复当前 gate 的确定性失败；
2. 补齐能暴露当前风险的测试/诊断；
3. 实现当前 Phase 的下一个依赖；
4. 清理由本轮产生且会阻塞 gate 的问题。

禁止“顺便做 UI”“顺便重构”“先把后面接口都搭好”。

### SPECIFY

改代码前，先在 loop/JOURNAL.md 新增本轮开头，至少写：

- cycle 编号与当前 Phase；
- 唯一任务；
- 可证伪的假设；
- 预计修改文件；
- 快速检查命令；
- 通过后会新增什么证据。

若无法写出确定的验证方式，这个任务还不够小，继续拆分。

### IMPLEMENT

- 修改最小范围。
- 新行为优先先添加会失败的自动检查或可测 probe。
- 保持错误码、HRESULT、NGX result、资源格式/尺寸、GPU timing 可记录。
- 不覆盖用户或上一轮未确认的修改。

### FAST_VERIFY

先跑最窄检查，再跑当前 Phase gate。所有命令和 exit code 写入 JOURNAL。只编译不等于运行通过。

当前阶段统一入口：

       powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phaseN

phaseN 必须替换为 phase0 到 phase7。scripts/gates/phaseN.ps1 必须由对应 Phase 建立，并检查 Playbook 中该 Phase 的真实验收结果。缺少 gate 脚本本身就是失败，不能手工口头放行。

所有 phase gate 都必须遵守 scripts/gates/README.md：不修改源代码、控制面或 STATE，fail closed，实际运行测试并解析量化结果。Gate 可以触发被测程序在 ignored 的 logs/captures 下生成带 run-id 的新证据，但不能自行伪造、补写或修改结果，也不得只 grep 旧日志中的 PASS。

窄测试通过且本轮产生了真实前进证据时，可以创建本地小提交，消息格式 phase-N work: <atomic outcome>。只显式 stage 本轮文件，提交前再次列出 staged names；失败状态、编译产物、runtime 和 captures 不提交。这个提交不是 Phase 通过，不能写入 lastGoodCommit。

### REPAIR

失败时生成稳定 fingerprint：

    phase | command | exit/result code | first actionable error

对同一 fingerprint：

1. 第一次：定位最可能的本地原因，做最小修复；
2. 第二次：收集额外诊断，验证另一个假设；
3. 第三次：更换实现路径或缩小复现；
4. 仍失败：停止重复，写入 INBOX/STATE，只转做当前 Phase 内不依赖该阻塞的任务；若没有独立任务则 BLOCKED。

“重跑同一命令且没有新信息”不算不同尝试，并增加 noProgressCycles。
failureLedger 必须按 fingerprint 累积 attempt、假设、命令和结果；切换到别的错误后再回来，次数不能清零。lastFailureFingerprint 只是快速恢复指针。

### PHASE_GATE

只有以下全部成立，当前 Phase 才能标记 passed：

- 当前 phaseN gate exit code 为 0；
- gate 输出和关键日志已登记到 EVIDENCE；
- Debug/Release 或 Playbook 要求的运行矩阵真实执行；
- Git 未跟踪专有 runtime、local SDK、capture 或大日志；
- 无已知 P0/P1 缺陷；
- WORKLOG 已写实际命令、结果、失败和下一步。

此外，gate 必须验证“产品库执行了工作”，不能只验证 harness 内存在一条相似链路。对于 4K，必须从本次资源描述符/JSON 证明 source/working/output extent；不能由文件名、窗口尺寸或第二次 1080p 运行推断。依赖 manifest 只证明身份，不能单独满足 provider/画质实现门槛。

### REVIEW

阶段 gate 首次通过后，启动一个新上下文 Reviewer，任务必须明确“只读，不修改工作区”。Reviewer 要：

1. 完整读 AGENTS、本循环、当前 Phase 定义和 git diff；
2. 独立重跑 phaseN gate；
3. 对照 Product Spec 的 Definition of Done 与 Playbook 当前 Phase 逐项审查；
4. 查找伪完成、错误类型、资源状态、历史 reset、许可证和证据缺口；
5. 仅输出 P0/P1/P2 findings、证据和结论。

Maker 必须修复 P0/P1 并重新跑 gate 与 Reviewer。P2 只有在不影响当前 gate/后续正确性时才能进入 BACKLOG。若平台没有可用的子 Agent 能力，不得声称“独立复核通过”；状态改为 needs_review，而不是自动进下一 Phase。

### CHECKPOINT

Reviewer 通过后：

1. 更新 BACKLOG、EVIDENCE、JOURNAL 和 WORKLOG，并把 STATE 中本阶段状态置为 review_passed，commit 字段暂为 null；
2. 检查 git diff --check、staged names 和 git status；
3. 创建已验证内容的本地 checkpoint commit，消息格式：

       phase-N: <verified outcome>

4. 立刻读取 git rev-parse HEAD；把这个真实 hash 写入 STATE.lastGoodCommit 和 STATE.phases[N].commit，并把本阶段状态置为 passed、相邻下一 Phase 解锁；
5. 以 loop: record phase N checkpoint 创建第二个纯状态提交。lastGoodCommit 指向前一个“已验证内容提交”，因此不会出现让文件引用其自身提交 hash 的不可能自引用；
6. 自动进入下一 Phase，不等待用户逐阶段确认。

不 push、不 merge 到任何远端。

每个 cycle 收尾时（无论成功或失败）只把 cycle.completed 增加 1 次，更新 currentTask、nextAction、updatedAt；有新证据则 noProgressCycles 清零，否则加 1。先验证 STATE 能被 ConvertFrom-Json 读取，再结束本回合。

## 7. 阶段门禁摘要

具体参数、命令和阈值以 Product Spec 与 Playbook 为准，下面只定义不可省略的出门条件。

| Phase | 必须证明的结果 |
|---|---|
| 0 | 正确工具链与 D3D12 device；4-slot fence ring；本机 GPU/driver；两个文件身份；DLSSNR required exports；Debug/Release probe 实跑 |
| 1 | RGBA8 Proxy 直接进入 Feature 18；Create 成功；连续 300/300 Evaluate 成功；Raw 输出非黑、非恒定、随参数/输入变化；资源与计时日志完整 |
| 2 | Original→Parity Encode→Feature18→Parity Decode；CPU/GPU shader 对照在 Playbook 容差内；identity/bypass、色阶/transfer/range 正确 |
| 3 | FFmpeg 共享 D3D12 device 的 D3D12VA 解码；无正常路径 CPU 像素回读；seek/reset；30 分钟播放和有界队列 |
| 4 | DLSS SR 接入；正确 render/output subrect 与 reset；关闭 SR 时可旁路；没有把 NR 冒充 SR |
| 5 | 统一 graph、真实 SR→NR、scene/cadence、NVOF motion/confidence、可选 DAV2 depth/Auto fallback、1080p/4K 质量矩阵和 30 分钟耐久 |
| 6 | DLSSG 2X 真实生成帧；A/B 与 A/B/C 模式；generated 非重复/非 blend；4K PTS/cadence/reset；D3D12 present、WASAPI、延迟与队列 |
| 7 | 4K Player、真实 4K60 Capture、Image Export、D3D12 NVENC 4K H.264/HEVC Export 和产品 UI/恢复全部端到端通过；音频、字幕、取消、内存/显存、许可证证据完整 |

不得用 HDR、3X/4X、VFR 原样导出或厂商私有采集 SDK 拖延 V1；也不得把 4K、采集、Depth Anything/明确 Auto fallback、D3D12 NVENC 或导出从 V1 删除。

## 8. 前进证据与防骗规则

下列之一才算“本轮有进展”：

- 新增自动检查从 fail 变 pass；
- 当前 gate 的失败项减少；
- 获得新的可操作错误码/日志，排除了一个明确假设；
- 完成一个可构建、可运行、被测试覆盖的原子任务；
- 阶段 Reviewer 关闭一个 P0/P1。

代码行数、接口桩、TODO、只编译、理论 FPS、返回 0 但黑图都不算进展。

日志和 capture 保存在已忽略目录；EVIDENCE 只写相对路径、命令、hash/摘要、exit code 和结论。不存在的 artifact 不得登记。

## 9. 阻塞与严格顺序

遇到阻塞先判断是否影响所有后续工作。

- NVOF SDK/账号条款不可用：把对应 backlog 项标为 BLOCKED，完成当前 Phase 内不依赖 SDK 的接口、graph、scene/cadence、depth manifest 和 synthetic tests；Phase 5 gate 未通过时不得开始 Phase 6/7。
- DLSSNR hash 不符、签名无效、required export 缺失：安全停机，不换 DLL。
- 当前 GPU/driver 不满足实际运行：只继续当前 Phase 内的纯 CPU/配置/诊断工作，GPU gate 保持未通过；不得提前把后续 Phase 标为开始。
- 需要用户接受许可证、提供凭据、公开发布或执行系统级变更：写入 INBOX，不擅自同意。

只有在同一阻塞连续三个 Goal 回合都存在、并且没有任何安全的独立工作可做时，才把 Goal 标记 blocked。

## 10. COMPLETE 的唯一条件

只有同时满足以下条件才能结束 Goal：

1. Phase 0 到 7 在 BACKLOG 和 STATE 中均为 passed；
2. 每一阶段都有 phase gate 的 exit 0 证据和 Reviewer 通过结论；
3. Product Spec 第 14 节与 Playbook Phase 5–7 的每一项都有真实日志、计数器、采集、呈现和导出证据；
4. 没有未解决 P0/P1；
5. 专有文件与 local SDK 未被 Git 跟踪或打包；
6. 最终 Debug/Release 构建和端到端运行真实执行；
7. STATE、EVIDENCE、WORKLOG 与 Git 一致；baselineCommit/lastGoodCommit/各 Phase commit 都真实存在且是当前 HEAD 的祖先；
8. 仅允许列出不影响 V1 定义的 P2/未来扩展项。

达到 cycle 上限、预算接近耗尽、文档写完或“理论上可行”都不是 COMPLETE。

## 11. 人工接管

用户回来后只需看：

1. loop/STATE.json 的 status、phase、nextAction；
2. loop/INBOX.md 是否有授权问题；
3. loop/EVIDENCE.md 的最后一个 gate；
4. docs/WORKLOG.md 的最后一个阶段记录。

若要立即停机，在项目根目录创建 loop/STOP。续跑前由用户删除；Agent 没有删除权限。
