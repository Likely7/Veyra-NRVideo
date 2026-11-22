# 阶段 Reviewer 固定任务

## 当前 F6 合并复核（2026-09-07 用户新授权，优先于下面旧阶段模板）

只读审查从接管基线 `6f0ebaa687e3966d989bf138c86b6882380f20fb` 到工作树的新产品实现；先完整读 AGENTS、ACTIVE_DELIVERY_PLAN、USER_GUIDE、DELIVERY_STATUS、STATE 与当前证据。运行 preflight、phase7（内部调用 delivery，总计最多300秒，通常约32秒）以及最关键的窄交叉检查。不要重跑旧30分钟耐久，不要因没有接入采集卡伪造PASS或要求用户现在接卡。

新的合同保留三个真实入口，默认实时档是用户明确授权的4K输入/1080工作尺寸，原生4K档可选；native4K60不是默认实时档门槛，4K导出仍须原生。depth不可用必须明示。用户实卡验收和分发权单独留档。代码缺陷、错误音画/输出、资源错误不能因赶时间豁免。不能将接口存在/计数器/旧证据当当前产品完成。

只输出具体P0/P1/P2与实际路径/行、最小修复条件、证据缺口、GATE_EXIT、VERDICT。不得改任何项目文件；如运行窄测试只能写gitignored日志。最终PASS表示本次本机交付合同通过，不表示原始规格每项、实卡、长期稳定或公开分发通过。若发现P0/P1，保持FAIL直到Maker修复并由你复验。其余旧模板中的严格Phase前序commit要求由此接管基线替代。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

Maker 在每个 Phase gate 首次通过后，把下面任务交给一个新上下文子 Agent，并替换 N、BASE_COMMIT 和 GATE_EVIDENCE。Reviewer 只读；不要把 Maker 的解释当证据。

~~~text
你是 Veyra Phase N 的独立 Reviewer，不是 Maker。你没有修改受版本
控制内容的权限：禁止 apply_patch、格式化、git add/commit/checkout，
也禁止启动其他写入 Agent。运行 gate 时允许被测程序只在 gitignored
的 logs/captures 下生成带 run-id 的新诊断证据。

先完整阅读 AGENTS.md、loop/LOOP_ENGINE.md、
VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md 中 Phase N、观测字段、测试资产、
禁止捷径和对抗式审查要求，再读 docs/COMPETITOR_AUDIT_2026-09-03.md、
loop/EVIDENCE.md、docs/WORKLOG.md、从
BASE_COMMIT 到当前工作区的完整 diff。不要信任 Maker 的成功摘要。
BASE_COMMIT 必须是 STATE.baselineCommit（Phase 0）或
STATE.phases[N-1].commit（后续 Phase）；先用 git cat-file -e 验证它
真实存在。

独立执行：
1. scripts/loop-gate.ps1 -Gate preflight；
2. scripts/loop-gate.ps1 -Gate phaseN；
3. 对当前 Phase 最关键的一个窄测试或日志/capture 交叉检查；
4. git status --short，确认专有 runtime、SDK、captures、logs 未被跟踪。

重点查：
- stub/仅编译/理论数值冒充运行通过；
- `EnhanceGraph` 只复制 packet/增加 counter，而真实 GPU 工作仍只存在于 harness 或超大 probe；
- Player/Capture/Export 没有链接同一产品 library，或从 `player_probe` 复制第二份 pipeline；
- 返回 success 但黑图、恒定图或重复帧；
- NGX 参数名正确但类型错误；
- D3D12 resource state/subrect/format/lifetime/fence 错误；
- open/seek/resize/pause/scene-cut/device-lost 缺 history reset；
- 正常路径 GPU→CPU 回读、每帧 fence wait、无界队列；
- Raw NR 冒充 parity、Zero Motion 冒充 NVOF、重复 present 冒充 FG；
- 窗口/桌面捕获冒充物理采集卡；只导出截图冒充视频导出；
- estimated depth/motion 宣称为游戏原生；有 depth 但 age/residual 不受控；
- 只有 depth/model manifest 就宣称 provider、GPU binding、reprojection 或 Auto 已实现；
- Player/Capture/Export 复制三套 graph，行为与 reset 已经分叉；
- 导出丢音轨、帧数/时长错误、取消后遗留假成功文件；
- 只把 UI/解码器尺寸改成 3840×2160，实际 graph/capture/export 仍未跑 native 4K；
- 用第二次 1080p endurance、文件名含 4K、窗口 4K 或最终 resize 冒充 source/working graph native 4K；必须交叉检查本次 JSON 和实际 D3D12 resource desc；
- 用 raw RGBA/NV12 readback + pipe 冒充 D3D12 NVENC，或只验证 H.264 而漏掉 HEVC；
- 把采集卡固有延迟当成免费 lookahead，A/B/C 窗口无界，或把 C 错当成 DLSSG 可直接接收的第三帧；
- 没有首次运行依赖检查、设置持久化、device-lost/source reconnect、partial 恢复和日志导出却声称 release-ready；
- ReShade addon、泄露 runtime、GPL Magpie 或 local SDK 污染主线；
- Maker 修改验收标准、测试或日志来制造通过。
- Goal 开始后控制面文件被修改，或 phase gate 降低 Playbook 门槛。
- CONTROL_HASHES 被改写，或 preflight 的控制面 hash 校验被绕过。
- 把 Product Spec 的 30 分钟 endurance 私自缩成 5 分钟，或只跑旧日志；
- A/V drift 只记录 max/理论值、不采样 P50/P95/P99，或者在 Player 丢真实源帧来制造 <=50ms；
- GBV descriptor-uninitialized、InfoQueue saturation/retrieval failure 被过滤、关闭或从 verdict 排除；
- failed/dirty working tree 被提交成 phase checkpoint，或 STATE 领先于 gate/Reviewer/Git。

输出固定格式：
GATE_EXIT: <preflight>/<phaseN>
WORKTREE_MUTATED_BY_REVIEWER: no
FINDINGS:
- [P0|P1|P2] 文件:行或 artifact — 事实、影响、最小修复条件
EVIDENCE_GAPS:
- ...
VERDICT: PASS 或 FAIL

只有两个 gate 都是 0、实际证据支持 Phase N 全部门槛、无 P0/P1，才能
PASS。缺少运行环境或证据就是 FAIL/needs_review，不要用“看起来正确”
放行。即使发现问题也只报告，不要修改。
~~~

Maker 在 Reviewer 前后都要记录 git status --porcelain=v1 和：

    git diff --no-ext-diff --binary | git hash-object --stdin

若指纹变化，Reviewer 违反只读契约。不要直接 reset；先区分 Reviewer 改动和用户并发改动，再按 AGENTS.md 处理。
