# Veyra Gate Evidence

## 2026-09-10 SR targets / AMD NR research, not a gate

Report: `docs/SR_TARGETS_AMD_NR_PLAN_2026-09-10.md`; extends the XeSS/AMD OF plan. Pinned GitHub source/README/license/release records confirm three different AMD NR approaches; no new backend was executed. Local read-only checks confirm the original NR hash/signature and unchanged UI-repair EXE. CIM shows RTX5070 and 9700X integrated AMD graphics, not the RX9070XT used by upstream tests. Research texts are ignored under `logs/amd-nr-research-20260910/`. No build, 2K/8K SR Create/Evaluate, AMD inference, capture or phase acceptance; implementation starts at planned S0. Documentation checks are recorded separately from GPU evidence.

## 2026-09-10 Native control paint repair

Scoped build/UI checks PASS: native control21, popup14, actual RTX settings, resize24/move24/menu4, FPS3 layouts. Report with exact commands/failure evidence: `docs/UI_PAINT_REPAIR_2026-09-10.md`. Initial fixed1s settings wait failed during feature teardown; bounded actual-log wait passes. No full delivery rerun, physical capture, long desktop recording or independent Reviewer; no phase advancement. User reproduction confirmation remains pending.

## 2026-09-10 FPS / capture60-to30

Release build exit0; CPU41, worker11, preset roundtrip PASS; actual RTX engine replay half-rate19 PASS with ~30 completed FPS and return to ~60. UI daily/professional/small visible bounded live FPS and pause-zero PASS. Original delivery23 PASS45.317s: `logs/delivery/910fe667d2574babaa3b02a7aa9bb3e2/result.json`. Full commands, hash and failure records: `docs/HANDOFF_2026-09-10.md`. No new physical capture or independent Reviewer verdict. Phase7 remains in_progress; known FRUC reset pixels unresolved; no claim of scanout FPS or end-to-end latency. Runtime hash/signature unchanged, local Git only.

## 2026-09-08 optimization Goal startup — preflight FAIL

Executed `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`: exit1, 2.604 seconds, sole failure control:README.md. Other controls and both binary identities passed. README matches user-authorized f79ef95; manifest expects earlier README. Full output is in the task tool record; logs/optimization-goal-20260908/control-rebaseline-proposal.json is explicitly an observation/proposal, not fabricated gate output. Proposal is unexecuted; see cycle49/INBOX. No product build/Create/Evaluate this cycle; previous Phase7 records do not approve the new Goal's changes.

## Current capture fix candidate — 2026-09-07 (not independently approved)

EXE4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9. Actual real-card source FPS~49.4–49.8 atMJPEG1080p50,0capture drops; final NR195/NVOF193/FG193. Logs `logs/capture-fix-20260907/`. Visible file/image/export gate `logs/delivery/01b72df768524af9ab0aa8d2e3dbb59e/result.json`,47.309s,21 checks. Full evidence/caveats `docs/CAPTURE_LATENCY_FIX_2026-09-07.md`. Reviewer attempt ended without verdict, needs_review; old PASS below only applies to its old binary. Do not mark physical latency/audio/FG display cadence verified.

## Historical delivered binary / independent PASS — 2026-09-07

Current EXE SHA256 70DD23C44337004FC734FBAE8BB6139E185C52370AE97CCE6C3432A3C6F30DE1. New-context Reviewer preflight/phase5 (consolidated delivery) exit0/0, PASS, no reviewer source mutations. logs/delivery/9be0614da5d642e394a35c71d80f6207/result.json,40.31s. Initial3 P1 fixed and independently reproduced correction: image4K dimensions, cancel callback lifetime43encodes exit3/partial only, audio fail-closed code review. Review persisted docs/REVIEW_F6.md. Local contract only; awaiting_user_capture_test and distribution_blocked remain.

## F6 consolidated local delivery — 2026-09-07

Build x64-release exit0. `scripts/gates/delivery.ps1 -Root .` exit0, run d28879b01b5c44dd86cad33d6f386d90, 31.59s, 16 checks. Full current evidence in logs/delivery/{run}/result.json and per-command stdout/stderr/JSON/media. Actual 1080 NR60 + NVOF motion + GBV0, native4K NR12, 4K-input realtime FG source264/generated263/absolute-latenessP95 14.88ms, paused seek/resume/JPEG, PNG application run, native4K H264 and HEVC23 frames each + audio fully decoded by ffprobe. EXE900DD752D749036EC482B3B9557AD23D12C60D58A58E37B44C1C960D5DC1B747. New read-only reviewer pending. No capture hardware or endurance PASS.

Raw GPU segmentation logs/f6-profile-split.* proves NR22–23ms versus decode/output0.28ms. User explicitly accepted default realtime processing profile with optional native4K. New controls/doc hash rebaseline records this subsequent user authorization, not a hidden lowering of native4K quality/output correctness.

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `../docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。

这里只登记真实存在的门禁证据。格式固定：

## Phase 3 / phase3 / 2026-09-03T01:20:00+08:00

- Commit: 验证内容 checkpoint 待登记于 STATE.phases[3].commit
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase3`
- Exit code: 0（外层 70；内部 41 checks，run-id a122c9f792e14ceeb117d07a0e08ff91）
- Build configuration: x64-debug + x64-release 均真实构建；FFmpeg 9.0.1 (vcpkg 30ef65ca 基线, C:\veyra-deps)
- Hardware/runtime identity: RTX 5070 / D3D12VA shared Veyra device / AV_PIX_FMT_D3D12 / Feature 18 via signed snippet
- Logs/captures: logs/phase3/a122c9f792e14ceeb117d07a0e08ff91/（software+d3d12va+seek-storm+debug）；logs/phase3/endurance-30min.{json,log}（60 循环×900 帧）
- Quantitative result: **software 300 帧 PTS 全单调零回读；d3d12va 300 帧 nrEvaluateCount=300/300 + shaderDispatches=300 + AV_PIX_FMT_D3D12 + sharedVeyraDevice=true + gpuReadbackCount=0；seek-storm 10/10 无 stale；debug infoqueue active 0 errors；endurance 60 loops=54000 帧 nrEvaluate=54000/54000 workingSetGrowth=94.4MB<256 commitGrowth=-46.5MB<256**
- Reviewer: 首轮 FAIL（5×P1+8×P2）→ 修复（GPU queue wait/AVFrame lifetime waitIdle/实测 maxInFlight/NR per frame 54000/30min endurance+memory）→ 复核 **PASS**（独立 run 768dd095…全过；endurance JSON↔log 交叉一致；8+1 P2 均维持非阻塞）
- Open P0/P1: none
- Conclusion: Playbook §16 Phase 3 全部门槛机器验证通过且经独立 Reviewer 复核；Phase 3 关账，进入 Phase 4

## Phase 2 / phase2 / 2026-09-02T23:05:00+08:00

- Commit: 验证内容 checkpoint 待 Reviewer 后登记；gate 运行于 220d6ba+f477948 之后的工作树
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase2`
- Exit code: 0（外层 70；内部 phase2.ps1 35/35，run-id 32fb351bb11b49d2b718027bbfdb33f4，5.0s）
- Build configuration: x64-debug + x64-release 均真实构建；CPU 黄金测试双配置 12/12 0 failures；Release parity run 真实执行 16 帧 Evaluate
- Hardware/runtime identity: RTX 5070 / Feature 18 via signed snippet（Init_Ext/Create 0x1）；debug run 附带 ID3D12InfoQueue 零错误
- Logs/captures: logs/phase2/85da38334fd0488b816bccb14635f782/（修复后 gate run）；logs/phase2/debug-parity-run.{log,json}（持久化 debug parity run）；captures/phase2/<runId>/（七文件）
- Quantitative result: **GPU vs CPU encode maxCodeDelta=1（≤1）maxAlphaDelta=0**；**GPU vs CPU decode：beyondOneUlpCount=0（每通道 ≤max(0.002, 1×FP16存储ulp)）、maxAbsError=0.00390625（恰 1 ulp@[4,8)）、nanInf=0**；debug parity run infoqueue active=true stored=0 errors=0（持久化）；中性 baseline 1.0/1.0/1.0 + addon SHA-256 实测匹配（addon 从未加载）；rawDlssnr≠final；stageLuma（JSON）：proxy/raw/final 机器统计替代手抄值
- Reviewer: not run yet（gate 首次通过后立即安排）
- Open P0/P1: none known
- Conclusion: Playbook §16 Phase 2 全部门槛（CPU golden、≤1 code、≤0.002、四阶段真实捕获、中性 baseline+addon hash 记录、raw/final 可区分）机器验证通过

调试记录：5 个缺陷由 debug layer infoQueue 定位——SRV MipLevels=0 触发 RemoveDevice；UAV range 双重偏移越界；NGX 后共享资源 DATA_STATIC bind 冲突（DESCRIPTORS_VOLATILE+前置 barrier）；decode 容差含 FP16 量化（期望量化后比较）。CPU 黄金数学零改动。

容差裁定记录（Reviewer 首轮 P1/P2 修复，2026-09-02）：①P1——"debug parity run infoQueue 零错误"原先无持久化产物；已改为无条件 drain+计数入 JSON 并真实持久化 debug run（logs/phase2/debug-parity-run.log：stored=0 errors=0）。②量化器改 RNE（与 GPU 存储一致）后实测揭示：高光放大区（UpgradeToneMap ratio≈4.26×）fp32 与 double 的固有中间差（~0.05%）会跨越 FP16 桶边界（例证已留档：cpu=4.00763→RNE 4.0078125，GPU fp32≈4.0055→存 4.00390625；5038/2M 像素呈 ±1 ulp 分布，全部恰 1 ulp）。裁定：可表示区（<4.0）与原 0.002 界逐字等同；≥4.0 区因绝对 0.002 对正确 fp32 管线数学不可达，裁定为恰 1 存储 ulp。经独立 Reviewer 终审接受（PASS）。

## Phase 1 / phase1 / 2026-09-02T22:20:00+08:00

- Commit: 验证内容 checkpoint 在 Reviewer 通过后登记于 STATE.phases[1].commit；gate 运行时工作树为 a8feb21 + 用户安装 Graphics Tools 的环境变化
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase1`
- Exit code: 0（外层 70 checks；内部 phase1.ps1 55/55，run-id 7a535572145b4d73b25da192adf847ee，9.1s）
- Build configuration: x64-debug（带 D3D12 debug layer，实跑 10 帧零 state error）+ x64-release（300 帧）均经 build.ps1 真实构建
- Hardware/runtime identity: RTX 5070 / 0x10DE / driver 32.0.16.1656 / FL 12_2；staged nvngx_dlssnr.dll 165840496 / E16B…FC8E / Valid / NVIDIA；NGX SDK 310.7 官方 GitHub clone
- Logs/captures: logs/phase1/7a535572145b4d73b25da192adf847ee/（harness-release.{log,json}、harness-debug.{log,json}、captures/）
- Quantitative result: **Release 300/300 Evaluate 成功 0 失败**；output meanLuma=0.49425 min=0.05098 max=0.94397 stddev≈0.327 allZero=false constant=false；baseline=1BD84180… style1=B5294CB6… intensity05=8A621726…（三 hash 互异）；GPU timestamps nonZero=true avgMs=6.32；teardown release=0x1 snippetShutdown=0x1；deviceRemoved=false；**Debug run debugLayer=true 且日志 0 条 state/descriptor 错误**
- Reviewer: 首轮 FAIL（1×P1：no-state-errors 为空转 grep，debug layer 消息无捕获通道；5×P2）→ 修复（ID3D12InfoQueue 真实接入 + gate 两项可失败断言 + 字面量/containment/SEH/nanCount/debug30帧）后 **PASS**（2026-09-02；独立 run-id 0ede6908f036462f99ced2ccfe48f169 双配置 fresh build；release 300/300、debug 30/30 infoqueue active 0 error；三 hash 与此前三个 run 完全一致；runId/exeSha256 与独立 Get-FileHash 一致；控制面 1fb7afa..HEAD 零改动；Reviewer 工作树零变异）
- Open P0/P1: none（P2 残留三条已记录 JOURNAL Cycle 017：teardown 段 infoqueue drain、containment 后缀 vs 前缀、200 条检索上限——均不阻塞，留待后续 Phase 顺带处理）
- Conclusion: Playbook §16 Phase 1 全部门槛（Init_Ext/Create 成功、300/300、非黑非恒定、参数可变 hash、GPU 计时、干净释放、debug layer 无 state error【经 ID3D12InfoQueue 真实断言】、capture 存在）机器验证通过且经独立 Reviewer 复核；Phase 1 关账，进入 Phase 2

迭代记录：2026-09-02T21:56 首次运行 54/55（唯一红项 debug-layer-enabled，本机未装 Graphics Tools）；用户安装 Windows 图形工具后本 run 55/55。前期过程证据见 JOURNAL Cycle 016 与 WORKLOG Phase 1 条目。## Phase N / Gate name / timestamp

- Commit:
- Command:
- Exit code:
- Build configuration:
- Hardware/runtime identity:
- Logs/captures:
- Quantitative result:
- Reviewer:
- Open P0/P1:
- Conclusion:

在命令未执行、artifact 不存在或 Reviewer 未运行时，字段必须明确写 not run / not available，禁止使用 assumed、should pass 或理论结果代替。

## Bootstrap / preflight / 2026-09-01T17:16:32+08:00

- Commit: not available（本地 Git 尚未初始化）
- Command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立
- Hardware/runtime identity: nvngx_dlssnr.dll size/hash/signature/signer 通过；renodx add-on size/hash/NotSigned 身份通过
- Logs/captures: 命令标准输出；无 Phase capture
- Quantitative result: 45/45 baseline checks 通过，0 个失败；包含 8 个受保护控制面文件 SHA-256
- Reviewer: not run；bootstrap 不是 Phase gate
- Open P0/P1: none for Loop bootstrap
- Conclusion: Loop preflight 可用；Phase 0 仍未通过

反向检查：同一入口以 -Gate phase0 运行，exit 1，准确报告 Git 未初始化和 scripts/gates/phase0.ps1 缺失。

## Audit / preflight / 2026-09-01T21:48:25+08:00

- Commit: not available（本地 Git 尚未初始化）
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立
- Hardware/runtime identity: 两个本地文件的 size/SHA-256/signature 校验通过；本次文档审查未冒充 P0.1 的 GPU/driver probe
- Logs/captures: 命令标准输出；无 Phase capture
- Quantitative result: 54/54 baseline checks；九个受保护控制文件 hash、STATE sequence/evidence/bounds、ignore rules 和 binary identity 全部通过；source-snapshot 内存单测为 17 files / 0 false changes / README mutation detected
- Reviewer: not run；这是交接控制面审查，不是 Phase gate
- Open P0/P1: none for the control-plane audit；应用 Phase 0 尚未开始
- Conclusion: 经过清理后的 Loop preflight 可用，仍没有任何 Phase 被标为完成

反向证据：`phase0` exit 1（Git 与 phase0 gate 缺失）；`phase1` exit 1（并命中 requested=1/current=0）；`cycle.completed=80,status=ready` exit 1（state-loop-bounds）；提前解锁 Phase 1 exit 1（state-phase-sequence）。两次临时 STATE 修改均已恢复，随后再次执行正向 preflight。

## Phase 0 / phase0 / 2026-09-02T21:09:09+08:00

- Commit: gate 运行于 639fd48d 工作树之上（含 gate 修复，未提交时运行）；验证内容 checkpoint 在 Reviewer 通过后登记于 STATE.phases[0].commit
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate phase0`
- Exit code: 0（外层 70 checks；内部 phase0.ps1 61 checks 全过，run-id a5fd6348b3084b44857b1f1ffc96a449，总 308.3s）
- Build configuration: x64-debug（VEYRA_ENABLE_D3D12_DEBUG=ON）与 x64-release（OFF）均通过 scripts/build.ps1 真实 configure+build；probe exe sha256 debug=E75D22D04A9DCAADC31451FD6CA14A90CC86B5D1C2C736016BD2A47FE9EA683F，release=0C1AB463E94588998D8BD1E4E11CAC5DBE6C44A9B97CD05308D6146401C9ED2C（JSON 自报与 gate 独立计算一致）
- Hardware/runtime identity: NVIDIA GeForce RTX 5070 / vendor 0x10DE / LUID 0x00000000:0x000000005D5F2320 / dedicatedVideoMiB 11943 / driver 32.0.16.1656（注册表 nvlddmkm.sys）/ featureLevel 12_2 / OS 10.0.26200；staged nvngx_dlssnr.dll 165840496 / E16B…FC8E / 310.8.0.0 / Valid / NVIDIA Corporation；System32 nvofapi64.dll 32.0.16.1656
- Logs/captures: logs/phase0/a5fd6348b3084b44857b1f1ffc96a449/（probe-debug.log/json、probe-release.log/json）
- Quantitative result: exports 5/5；Debug 窗口 3s/303 帧；**Release 窗口 300s/30002 帧，deviceRemoved=false，reason=0x00000000**；git ignore 7/7 探针通过；git-sensitive-files none tracked；D3D12 debug layer 在本机不可用（0x887A002D，INBOX 已记录，非 Phase 0 门禁项）
- Reviewer: PASS（2026-09-02，新上下文只读子 Agent；独立重跑 preflight exit 0 / phase0 exit 0，自建 run-id a55d5fb41b164abc88fc2760f0b635ec，300s/30003 帧；BASE_COMMIT 2086282 经 cat-file 验证；控制面九文件 diff 为零；runId/exeSha256 防陈旧机制验证有效；4 条 P2 加固建议、无 P0/P1；工作树指纹前后一致 e69de29b）
- Open P0/P1: none
- Conclusion: Playbook §16 Phase 0 全部门槛机器验证通过且经独立 Reviewer 复核；Phase 0 关闭，进入 Phase 1

迭代记录：第 1 次运行 exit 1（runtime:fileversion 区域逗号格式）；第 2 次 exit 1（-Clean:$false 的 SwitchParameter 字符串转换）；第 3 次 exit 1（不存在目录的 ignore 匹配）；第 4 次 exit 0。三次修复均未降低任何阈值。

## Goal Cycle 001 / preflight / 2026-09-02T20:23:27+08:00

- Commit: not available（本地 Git 尚未初始化，属 P0.2 范围）
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: not applicable；应用工程尚未建立（P0.4 范围）
- Hardware/runtime identity: nvngx_dlssnr.dll size/SHA256/Valid NVIDIA 签名通过；renodx add-on size/SHA256/NotSigned 通过；`nvidia-smi` 实测 NVIDIA GeForce RTX 5070 / 616.56 / 12227 MiB / compute 12.0；System32 nvofapi64.dll 32.0.16.1656；Windows 10.0.26200.9168
- Logs/captures: 命令标准输出（记录于 loop/JOURNAL.md Cycle 001）；无 Phase capture
- Quantitative result: 54/54 checks 通过，0 失败；工具链实测 MSVC 14.44.35207（cl 19.44.35226）、CMake 3.31.6-msvc6、Ninja 1.12.1、DXC 1.8-1.8.2502.11、Git 2.53.0.windows.2，全部与 Playbook §1.4 记录一致
- Reviewer: not run；preflight 不是 Phase gate
- Open P0/P1: none
- Conclusion: P0.1 验收成立——Goal Agent 亲自采集的 preflight + 工具链/GPU/driver 证据取代交接基线；Phase 0 仍未通过（P0.2–P0.9 未完成）

## Fast-track control rebaseline / preflight / 2026-09-03T15:59:14+08:00

- Commit: not yet checkpointed; working tree based on `7b85752`
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build configuration: x64-release configure/build also executed, exit 0 (`ninja: no work to do`)
- Hardware/runtime identity: root `nvngx_dlssnr.dll` size/hash/Valid NVIDIA signature all rechecked by preflight; RenoDX add-on size/hash/NotSigned identity rechecked; no addon loaded
- Logs/captures: command output only; this is control-plane evidence, not a Phase 5 runtime gate
- Quantitative result: 68/68 preflight checks; protected control set is exactly 10 files; final manifest hash `332DB6AA1D5F109F8D6CA729985F2110FAB5AD57C086B155D5BBFDE0015D7A75`; STATE ledger 0–7 valid; sensitive Git paths none tracked
- Adversarial consistency pass: fixed the sole SR/Guidance graph-order contradiction; Product Spec, Playbook and competitor audit now all require `ingress -> optional SR(Zero) -> post-SR Guidance -> Feature 18 -> FG`
- Local DLSSG source identity recorded: SDK 310.7.0 `nvngx_dlssg.dll`, 7,519,856 bytes, SHA256 `135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F`, valid NVIDIA signature; not yet staged or executed
- Reviewer: not run; P5.0 is a user-authorized control rebaseline, not the Phase 5 completion gate
- Open P0/P1: old `scripts/gates/phase5.ps1` is obsolete and must be replaced before Phase 5 can pass; recorded as next backlog task
- Conclusion: Fast-track control plane is internally consistent and fail-closed; no new Feature 18/NVOF/DAV2/DLSSG/Capture/Export success is claimed

## Launch V1.2 4K control rebaseline / preflight / 2026-09-03T16:53:03+08:00

- Commit: not checkpointed; working tree based on `7b85752`
- Command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\loop-gate.ps1 -Gate preflight`
- Exit code: 0
- Build: x64-release configure/build exit 0 (`ninja: no work to do`)
- Quantitative result: 70/70 preflight checks; protected control set exactly 10 files; manifest SHA256 `77A9414588B0EF09156CB97EDF3618AF36D63DF12C5E2E3F8E15743292B96921`; cycle limit 24/120; four external/distribution blockers recorded
- New invariants: native 4K Player/Capture/Export; A/B and A/B/C windows bounded at 2/3; lookaheadFrames is not fabricated latency; D3D12 NVENC H.264+HEVC; release-candidate/distribution-blocked consistency checks
- Runtime scope: control-plane only. Native-4K Feature 18/NVOF/DAV2/DLSSG/Capture/NVENC were not executed and are not marked passed
- Reviewer: not run; P5.0b is a user-authorized scope/control rebaseline, not Phase 5 completion
- Conclusion: Launch V1.2 control plane is fail-closed and ready for P5.1

## Phase 6 — DLSSG 2X 与实时 engine (2026-09-04, session partial)

### fg-cap (manual-cap1)
- FG.Available=true (Get(ull)=0x1 Get(i)=0x1), FeatureInitResult=1, NeedsUpdatedDriver=false,
  MultiFrameCountMax=5, MinDriver 520.0; GPU=RTX 5070 driver 32.0.16.1656; HwSchMode 缺省=系统默认。
- logs/phase6-manual/fg-cap.{log,json}

### fg-test 真假证明 (regression-fg / manual-fg3)
- translation: realFrames=60 usableGenerated=59 dup=0 disableFlag=0; minBlendResidual=1.058
  (baseline 1.106, 阈值 0.664) ; maxTrueResidual=0.171 (阈值 0.553); maxMidpointErrorPx=1.27;
  directionCorrect=true; ptsMonotonic=true; generatedMeanLuma=60.2。
- cut: usable=58 (58..58), crossCutGenerated=false, resetIssued=true。
- mvecConventionWinner=pixels-scaled-1-over-w; createResult=0x1; NeverProvided=0xB8。
- 自校准说明: blend 判据不是固定阈值,而是 CPU 渲染精确中间帧做基线,要求 |gen-blend|>0.6×baseline
  且 |gen-truth|<0.5×baseline —— blend 冒充(残差≈0)与重复帧(hash 相等)都会 FAIL。
- logs/phase6-manual/fg-regression.{log,json}, captures 在 fg-truth runs。

### audio-test (audio4)
- eventMode=true 48000Hz buffer=1056; framesPlanned=192000 framesWritten=192000;
  underrunsSteady=0; maxAbsDriftMs=0.015 (LSQ slope n=349, offset range [-10.26,-10.22]ms 为
  IAudioClock 量化); pauseFlushWorks=true。exit=0。
- logs/phase6-manual/audio4.{log,json}

### player probe 场景 (pp-run11 1080p / pp-4k1 4K)
- 1080p: exit=0; playPause/seek(10)/resize=true; maxAvDriftMs=32.0; nrToggle=true fgToggle=true;
  presents=1306 fg=1156 nr=953 sr=953(1080p→4K); normalPathReadback=0;
  mvecSource=zero-motion-fallback(NVOF 帧内被注入层阻断,见 WORKLOG 系统发现)。
- 4K: exit=0; 全 toggle true(SR 1:1 bypass); nr=1080 fg=1068 drift=32.0 maxInFlight=7。
- logs/phase6-manual/pp-run11.{log,json}, pp-4k1.{log,json}

### 耐久(正式 5min×2) — 运行中,完成后补记

### 耐久(2026-09-04 补记)
- 15s 冒烟: 4K30 internal=59.07Hz(fg 505), 4K60 internal=111.87Hz(fg 778) — 引擎时间线正确。
- 5min×2 正式(endurance-full): FG 生成健康(13290+12998, ~44/s), NR 26299, drift 38ms, readback=0,
  但 presents 降到 10.5/7.2Hz 且 WS 增长 4.8GB → FAIL(诚实记录)。
- 泄漏定位矩阵(全部 GRAPH_OFF+PRESENT_OFF 纯路径, pace 采样):
  4K+音频 +150KB/帧; 4K 无音频 +150KB/帧(音频排除); 1080p +16KB/帧(与帧字节成比例);
  解码器每 400 帧整体重建无效; NR/FG/GPU 图/呈现全部排除(decode-only 亦泄漏)。
  → 泄漏在 FFmpeg sw 解码路径的进程内分配,疑与注入层的分配器钩子有关(或该 vcpkg 构建行为)。
- present 上限: 每次被注入层假报 device-removed 的 Present ~17ms → 显示链路上限 ~59Hz;
  4K60 的 120Hz 内部时间线经呈现链路不可达(引擎侧 44+44=128/s 事件已达标)。
- D3D12VA: 打开+池预热(视图前)成功,但帧内解码被拖到 ~8fps → 回退软解(VEYRA_HW_DECODE=1 可复测)。
- logs/phase6-manual/{endurance-full,diag-*,recycle-test}.{log,json}

### Phase 6 组件结论
- 已验证(green): P6.0 gate fail-closed; P6.1 FG capability/create/eval/release; P6.2 FG 真假
  (自校准 blend 基线); P6.4 WASAPI; P6.3 PresentSink(flip-discard+图形呈现+窗口缩放回退);
  P6.5 引擎(有界队列/迟到丢弃/seek 重同步); P6.6 双场景 player probe exit=0。
- 阻塞(red, 系统级): 5min 4K 耐久的内存增长与呈现速率上限; NVOF 帧内引导(DLSSG 以零引导降级,
  JSON mvecSource 显式标注)。

## Phase 6 session 2 证据补录 (2026-09-04)

### AVPacket 泄漏根除(src/media/FFmpegDemuxer.cpp av_packet_unref 显式化)
- FFmpeg-only 40s(VEYRA_GRAPH_OFF+PRESENT_OFF+NR/FG/AUDIO off;每 10s pace 采样):
  4K+音频 536/536/536MB;4K 无音频 537/538MB;1080p 523MB —— 全部平稳(logs/phase6-manual/leak-{avaudio,decode,1080}.log)
- 对照:修复前 4K 5 分钟 WS 增长 4835MB(endurance-full.json)。

### phase6 gate 自检
- gate:self-control-chars :: clean(脚本含 CR/LF/TAB 外控制字符即 FAIL)。
- 修复行:178/179(clip 路径 U+000C)与 182(ffmpeg bin 路径 U+000B/U+0008/'installedd'),字节级修复。

### Present 失败判别矩阵(同一二进制,600 次/轮)
| 配置 | 结果 |
|---|---|
| 裸(窗口+交换链+清屏) | ok=600 |
| +NGX core / +snippet / +capability / +FG create | ok=600 ×4 |
| +SRV 直写可见堆 | ok=0(全 DEVICE_REMOVED,INVALID_CALL,无 DRED) |
| +SRV 直写非可见堆 | ok=600 |
| +UAV / +CBV 直写可见堆 | ok=600 ×2 |
| +SRV 经 staging 复制 | ok=600(复测×2) |
| 同语义探针不同地址(stage13,mask=0..64) | ok=0(复测×2;vsync=1 同败) |
| 引擎(无视图) | 681 presents 0 失败 |
| 引擎+仅 UAV / 仅 raw-SRV | 440/439 次后败于 resize+1s |
| 引擎+任何纹理 SRV | 第 1 次即败 |
| 进程模块扫描 | 仅 NvTelemetry×2 为外来模块 |
日志: logs/phase6-manual/{bare-s*,mtx-s*,r9-*,r13*,b13-*,kn-*,vg-*,bis*,nofeat-*,nopso-*,small-sink}.*

### 状态
- 等待用户 A/B(关闭捕获/覆盖软件后重跑 stage13 探针)→ 决定根因为覆盖软件钩子或驱动缺陷。

## Phase 6 session 3 证据 (2026-09-04)

### descriptor_present_probe(独立、无 NGX/FFmpeg/NVOF/WASAPI;诊断全开)
7 案例 × 3 轮全 PASS(exit 0,presentSucceeded=600,presentFailed=0,removedReason=S_OK,
errMessages=0,corruptionMessages=0):
A bareClear / B nonVisibleSrv / **C directVisibleSrv** / D copiedVisibleSrv / E boundHeapNoDraw /
F sampledSrvDraw / G uavThenSample。logs/phase6-manual/dpp/r{1,2,3}c{A..G}.{log,json}

### 根因实锤
- nr_harness/parity_compare.cpp:543 既有注释 "MipLevels = 1; // 0 is invalid; the debug layer removes
  the device" —— 旧 makeSrv 恰好 MipLevels=0(非法)→ 设备移除在 Present 上显现。全部旧矩阵由此闭环。

### 修复后播放器
- 1080p(fixed-1080):Present FAILED=0;NVOF 305/0;FG 305;NR 331;SR 353;mvecSource=nvof;WS +206MB;
  seek 10/10;resize OK;drift 262ms(超标, pacing 待修)。
- 4K(fixed-4k):Present FAILED=0;NVOF 288/0;FG 288;全部 toggle OK;drift 262ms(同上)。

### 作废更正
- 上一 session 的 stage9/stage13 判别矩阵、"staging workaround 有效"、"驱动 616.56 缺陷/注入层"结论
  全部作废(嵌套括号导致 stage9 未执行,假阳性)。

## Phase 6 session 4 证据 (2026-09-04, 用户指令 s5)

### P0.1 音频验收(A 短测)
- p4b-1080.json: audioUnderruns=0, audioOverruns=0, audioBufferedMsEnd=1007.3,
  audioClockPtsMsEnd=55382.7(60s 片尾一致), audioSeekCount 含 10 seek。
- 旧 decodeUntil 绝对时间比较 bug(16974 overrun)已根除。

### P0.2 drift 分位数(无截断)
- p0-1080.json: driftMinMs=-1049.0 p50=853.2 p95=3776.6 p99=4402.9 max=4502.5,
  latenessSampleCount=565(全部 present 前记录)。
- p4b-1080(NVOF grid-4 后): p50=821.4 p95=2844.6。droppedLatePresents=0。

### P0.4 NVOF S10.5/grid-4/方向
- nvof-dots.json + log: 随机点 +8px(grid-4 SHORT2): dx p50=-32.06(raw=-1026)dy=0;
  像素单位=raw/(32*gridSize)(实测 4 倍因子);方向 current->previous(负)。
- caps: nvOFGetCaps 调用成功但 grids 列表返回空、min=max=32(如实记录,待查参数)。

### P0.5 逐 pass GPU(串行 fence+QPC,VEYRA_GPU_TS=1)
- ts-1080.log gpu-pass-ms: upload 0.01 | yuv 0.00 | sr 0.00 | encode 0.00 |
  nvof_call 0.18 | nr 2.34/2.75 | decode_blit 24.68/26.01 | fg 2.37/2.70 (p50/p95, ms)

### P0.3/P0.6
- PresentItem 资源绑定/epoch/fence 在源码(present 用 item.textureSlot;seek++resetEpoch);
- ResizeBuffers:queue-idle fence + 引用释放 + 硬失败(无 DWM 回退)。

## Phase 6 session 8 证据 (s9, 2026-09-05)

### fault-injection(nvof-fault-inject.json,exit 0)
7/7 REJECTED-CLEAN(null-device/inputA/inputB/flowOut/costOut/inFence/outFence 各一例,
均 return false + InvalidArgument + initialized=false,无崩溃,无 NVOF 副作用)。

### InfoQueue 三阶段(iso11-full-L1)
startup: stored-cleared=1 | runtime: stored=3178 retrieved=3178 failures=0 err=0 corr=0
warn=0 info=3178 cap=unlimited saturated=0 | (teardown 阶段因交换链崩溃未达)。
旧缺陷对照:容量 1024 时 failures==stored(两段式检索在 GBV 下全败)——已修。
### message-ID 样本
全部 3178 条为 INFO 级(调试层常规跟踪);ERROR/CORRUPTION=0。代表性 ID 直方图在
JSON diagHistogram(按需读取)。

## s10 — NVOF/GPU/InfoQueue teardown ownership — 2026-09-05T01:45:00+08:00

**任务**:修复并验证 NVOF/GPU/InfoQueue teardown 所有权(用户 session-8 指令 s10-I~V)。

**结果**:全部自然 return;矩阵 8/8;三根因修复(非抑制)。

### 释放顺序(实现)
- in-scope:lastNvofSignal 保存 → stub JSON(INCOMPLETE)→ 停生产者/音频 → ring-wait-idle →
  nvof-out-fence-drain(HRESULT+waitResult 检查,硬失败)→ ring-wait-idle-2 → queue-final-drain
  → NR/FG/SR release → nvof-unregister(纹理存活)→ release-nvof-resources(4 纹理,DLL 加载中)
  → nvof-shutdown(destroy+FreeLibrary)→ nvof-event-close → ngx-params-destroy → iat-shim-restore
  → ngx-core-shutdown → staged 资源显式释放(rtvHeap/passes/guidance/frame/working/upload)。
- scope end:资源自然析构(device 存活)。
- post-scope:sink-shutdown(交换链先于队列;backbuffers→swapchain→window→factory)→
  ring-shutdown → teardown scan → ReportLiveDeviceObjects → final scan → InfoQueue.Reset →
  context-shutdown → demuxer/decoder → final JSON(processCompleted=true 仅自然 return 前)。

### 根因证据
1. NVOF 纹理 FreeLibrary 后 Release SEGV:t10-L0-r2(17:11)"before release-nvof-resources"
   后崩溃;修复后同组标记通过。所有权规则已写入 NvOfSession.h。
2. 0x87D:WER event 1000(exception 0x0000087D,KERNELBASE.dll);PowerShell Start-Process
   实测 TRUE_EXIT=2173(0x87D);SEH 捕获后 InfoQueue 立即扫出触发消息:
   id=921 ID3D12Resource final-release with GPU operations in-flight on Command Queue。
   根因=最后 Present 在最终 fence signal 之后提交,waitIdle 不覆盖。drainQueue 修复后
   L1/L2 三阶段 0 ERROR(修复非抑制;与 NVOF 无关,s9 三方交互结论撤回)。
3. NF 空句柄:NR evaluate seh=0xC0000005(nrHandle==nullptr)→ break 跳过 in-scope teardown
   → return 时析构顺序颠倒 SEGV。toggle 按 nrHandle 门控后 NF 自然 12。

### 矩阵(全部自然 return,禁 ExitProcess)
| run | exit | nvof exec/fail | diag rt/td/fin err |
|---|---|---|---|
| L0-r1/r2 | 12/12 | 450/0 | off |
| L1-r1/r2 | 12/12 | 450-451/0 | 0/0/0 |
| L2-r1/r2 | 12/12 | 427-430/0 | 1764-1772(runtime GBV id=938)/0/0 |
| NF-r1/r2 | 12/12 | 0/0 | off |
- fence drain 每轮 expected==completed==lastSignal waitResult=0;mvecSource=nvof。
- 新 blocker:L2 GBV id=938 GPU_BASED_VALIDATION_DESCRIPTOR_UNINITIALIZED(引擎运行期,
  非 teardown;样本在 JSON diagErrorSamples;verdict 正确 FAIL)。

**原始日志**:logs/phase6-manual/t10/{L0,L1,L2,NF}-{r1,r2}.{log,json};L1-r3.json 为修复前
INCOMPLETE stub 崩溃样本。

## Control rebaseline audit / 2026-09-06T13:50:17+08:00

- Type: read-only implementation/status audit plus user-authorized control-document rebaseline; **not a Phase gate**.
- Pre-change preflight: 70/70, exit 0.
- Current Release build: exit 0, Ninja no work to do.
- Valid retained evidence: Phase 0–4 checkpoints; historical Feature 18/SR/parity/NVOF/DLSSG/WASAPI/Present component artifacts.
- Invalidated conclusion: historical Phase 5 product-level pass. Exact reasons: `src/core/EnhanceGraph.cpp` is metrics-only; old phase5 gate accepts a depth manifest without a provider; its second “4K” endurance run uses the same 1080p source/path.
- Current Phase 6 truth: t10 L0/L1/L2/NF matrices naturally tear down, but every scenario verdict is FAIL. Example `logs/phase6-manual/t10/L0-r2.json`: driftP95=2828.229ms versus 50ms threshold, NVOF execute/fail=450/0, FG=450, readback=0. L2 has 1764–1772 runtime GBV id=938 errors.
- Product artifact check: no `apps/veyra`, CaptureCardSource, ImageExportSink, VideoExportSink or working DAV2 provider; `tools/player_probe/main.cpp` is 3641 lines.
- Worktree check: 31 porcelain entries before the control update and 44 after it; uncommitted Phase 6 code preserved; no reset/clean/checkout performed.
- Dependency correction: `third_party_local/nvidia/Optical_Flow_SDK_5.0.7` exists; Video Codec SDK 13.1 remains absent.
- State action: Phase 5 reopened; Phase 6/7 locked; lastGoodCommit restored to Phase 4 content checkpoint `8cdf1ea2f9b0bb1331f9219a2305cef5efd3c8d8`.
- No new RTX runtime test, phase gate, Reviewer or checkpoint was claimed in this audit.
- Post-rebaseline preflight: 70/70, exit 0; final control manifest hash `45A54E674D32A8055B338C0AE9B77F4A6440D7BC08D4BCF00D27CE8E01D556BF`; phase sequence/state/evidence/Git-pointer checks all passed.

## Repair v2 handoff — 2026-09-08
Current evidence and unresolved review findings: docs/REPAIR_PROGRESS_2026-09-08.md. Latest joint: logs/repair-v2/joint-bb22bfa233ee455ca030ba0eccdf5a86/result.json (15 software cases exit0; final static review NOT passed). Budget ledger: logs/repair-v2/runtime-budget.json, 260.5401069 seconds spent. User requested stop and new-session continuation; no new Phase pass.

## Repair v2 final known-fix delivery 2026-09-08
Joint joint-a077b89fd39348e4a49f4195c9d4e416: 18/18 software checks, 71.0278864s. EXE 1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266. Read-only review_known_fixes PASS in scoped fixes; global needs_review and old Phase fields unchanged. Per-invocation <=300 seconds; cumulative 346.3985650s preserved. Full evidence, failures and performance limits: docs/REPAIR_V2_DELIVERY_2026-09-08.md. No capture or publication.

## 2026-09-08 UI0–UI9 final software record

Baseline ddc515d. Current EXE 2DE33230CD3A6556BC8E1399DD93BB8959B8EF37B2BACEF23D1486590AA4D3C6. Final app build logs/ui-dual-mode/build-upload-direct.log exit0; source-only integration test build build-threaded-test.log exit0. UI logs/ui-dual-mode/de337338b71e43618c4beaa488d54afb/result.json: 15 subprocesses exit0/109.583s; switch40 P95 4.2ms/max13.591, 446 submissions/epoch1/maxgap20.3844ms/no Create/reset/open; GDI/handle stable. Repair logs/repair-v2/joint-91436d0b0f4145c2b6fe6794ff146c68/result.json:18 exit0/70.7119369s subprocess sum, lifetime ledger487.6690411 preserved. Current-CFR supplement logs/ui-dual-mode/4k-f764b1a5ef894f46b050cbbf728103ec/result.json:23 checks/14.178107s PASS, actual 1-vs-4-thread 4K H264/HEVC B-frame first read and post-EOS seek0 reread, effective YUV pixel Adler32 and PTS equal, native4K 12+11+1=24frames/120fps/0.2s/AAC/decode/cancel.

Protected gate logs/delivery/39c5c9d100234c92beaf7eb5d5cab1c0/result.json remains FAIL at legacy23 assertion, 34.736374s. Earlier runs de8ddbf1e89c41d78543802c299f3a3a and 64e5c18cc8834dfba5f991549d726477 failed player lateness at45.95/55.68fps. Historical1080 flow/FG/output gate cannot prove current full4K performance. Bounded four-thread file decode, fenced direct YUV upload and .2ms FG readiness polling preserve full4K base/flow/FG/output, NR1080; current59.55fps/P951.77ms, NR248/NVOF247/FG247, Create/Evaluate0x1 Success. Never weakened protected assertions or lowered extents. Root/staged NR and addon hashes match; seven protected hashes unchanged; deleted user test video remains absent. No capture or publication.

Visual checks via native UI: same EXE daily-current.jpg/professional-current.jpg in logs/ui-dual-mode/visual-candidate, transparent bilingual subtitles, complete four-card Pro repaint. Other visual states from prior same-UI candidate include capture setup only, export, diagnostics, 800x600 drawer, 2560x1440 fullscreen, and edit focus. Earlier subtitle/layout/mode-latency failures retained and repaired. Readonly review_dual_mode reviewed code and evidence; exact final conclusion in docs/REVIEW_UI_DUAL_MODE_2026-09-08.md. Historical Phase fields are not UI proof. User authorized normal shutdown after local task completion; final checkpoint and shutdown dispatch recorded separately.

Next: user visual/capture audio/latency acceptance. Report docs/UI_DUAL_MODE_DELIVERY_2026-09-08.md.


## 2026-09-08 UI repair v3 final software record
Baseline b6b251d; build16 exit0, EXE B1892C51E8AFC27D7223E271D48D93107C34CAA96BC4014DFF6F45FDE0D7BF74. Native logs/ui-repair-v3/3fb66a3472264eb4abc1fc61917839a8/result.json:11 commands PASS/81.0014012s. UI logs/ui-dual-mode/13e2d933fe66477797a71b32612af303/result.json:15 commands PASS/141.1874955s. Repair logs/repair-v2/joint-f3c40457ea02481c87dfce0739f51c74/result.json:18 cases PASS/70.4781915s subprocess sum; cumulative558.1472326s retained. Current4K logs/ui-dual-mode/4k-719b7f3002fa42b88abbad8ba0262137/result.json:23 checks PASS/13.7508573s. Every invocation <=300s. Actual RTX Feature18 Create/Evaluate0x1 and actual SR PNG/video dimensions/decode recorded. Switch40 P958.191ms/max9.963ms;442 submissions/epoch1/maxgap18.1116ms;GDI12 andhandles721 stable. Independent readonly review_ui_v3 PASS scoped fixes; see docs/REVIEW_UI_REPAIR_V3_2026-09-08.md.
True DWM red/blue desktop proof in desktop-red-final.jpg/desktop-blue-final.jpg (build14 material main path unchanged); actual finalbuild16 Daily/Pro/scroll/diagnostic/fullscreen-hidden screenshots in logs/ui-repair-v3/acrylic-*-final.jpg; Ctrl+A/0.75 input/copy/paste verified. Video and subtitles remain opaque/sharp, empty video black. Removed simulated application artwork. Native reject bfc72 FAIL preserved; corrected observer begins500ms after backend rollback; final log independently shows checkboxfalse->true with draft always preserved. Earlier export cold-start timeout and old-gradient performance failures preserved in report.
Protected delivery04388 FAIL legacy23 assertion; protected7 unchanged and fixed root/stagedNR/addon match. Old global Phase7 needs_review retained; no capture, public package/push or shutdown. Next: user visual/capture acceptance. Full report docs/UI_REPAIR_V3_DELIVERY_2026-09-08.md.


## 2026-09-08 UI controls v4 (interactive; not a new Goal)

User requested direct bottom controls, a free consistent icon set, and glass app selectors. Baseline97ccd19; single writer retained. Native toolbar + Lucide ISC/MIT subset + accessible list/combo popup implemented. Fixed DeferWindowPos hide/show merge, SR effective-state sync, popup synchronous cancel and focus/notification lifetimes. True desktop acrylic and opaque video retained.

Final executable7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514: controls/14popup cases PASS1.312s, switch40 PASS15.008s, nativeNR/SR/NaN/bottom controls PASS25.858s. Previous build differs by12DIP compactSR width: fullUI PASS141.503s; image/video/SR/export PASS80.914s. Evidence is version-qualified in docs/UI_CONTROLS_V4_DELIVERY_2026-09-08.md, not combined into a fake Phase7 pass. Actual1280/720 and popup keyboard screenshots retained locally. Independent readonly review scope recorded in docs/REVIEW_UI_CONTROLS_V4_2026-09-08.md. Protected hashes unchanged. No capture stream, runtime change, upload, packaging or shutdown. Global needs_review unchanged. Next: user UI and original capture acceptance.

## 2026-09-08 optimization blocked audit 2

Previous Goal turn classified progress (identified preflight failure and exact authorized-README origin). This automatic continuation is no progress, not a verified process wait: README still F226D0A7 versus expected 781FAFD8; manifest still 26559334; no STOP and no explicit authorization received. Proposal unchanged, no product or control edits. Same blocker for second consecutive Goal turn; goal remains active under the three-turn blocked threshold. No build/GPU/gate rerun performed.

## 2026-09-08 optimization blocked audit 3 — Goal blocked

Previous and current continuation classified no progress, not verified process waits. Read-only revalidation confirms unchanged README F226D0A7 / manifest expectation 781FAFD8, manifest hash 26559334; no explicit authorization for the exact two-hash synchronization. Same blocker for three consecutive Goal turns including startup. Under the explicit control-plane stop rule there is no permitted independent implementation remaining. Mark Goal blocked, retain full Q0–Q8 scope and unapplied proposal; no product/control edits, build or GPU test. Resume requires explicit authorization recorded in INBOX, then exact synchronization and fresh preflight. Not complete.

## 2026-09-08 Q1a RGB / odd dimensions and static NR isolation

Phase 7 optimization remains in progress. Q1b large-image tiling, Q2 zoom and Q3-Q7 quality work are not passed. Real capture remains unexecuted.

Changes: ResolutionPlan/EnhanceGraph accept odd extents up to actual single-texture 16384; WIC arbitrary 8192 guard removed with checked UINT byte bounds; RGB upload and RgbToLinear avoid 4:2:0 conversion; PNG negotiated BGRA packing fixed; ScaleBlit exact 1:1 load avoids long-image floating-point interpolation error. Static images skip NVOF and FG capability/Create/warmup, reject FG enable, normalize image settings to 1X. Product per-export integrity checks unchanged.

Build command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release (exit0; logs/optimization-goal-20260908/build-static-final.log).
Image command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File logs/optimization-goal-20260908/run-image-tests.ps1 (latest image-c17dd1e5813b494e9d25344ebd0c93e1, exit0,4.4185s; EXE 60EEF44E1E36B97182D0EE53A09A782444D7AA01864CD1B617D5677703CF9008).
Five NR-off cases 1x1,257x513,97x9001,4097x257,257x4097 have max RGB error0 and exact PNG readback. 257x513 and97x9001 real NR: Feature18 Create0x1 Success,handle non-null,SEH0,Evaluate1,nonblack output. FG capability/Create absent; direct enable and 2X apply rejected. This proves execution/dimensions, not NR quality equivalence.

Historical failures retained: image-ece8... missing shader dependency (fixed CMake); image-fe18... long1:1 error6 (fixed ScaleBlit). phase7-rgb failed old 23-frame assertion: actual 12source+11generated+1hold=24,120Hz,0.2sec,audio preserved. Developer delivery gate now checks all those identities/duration/rate for H264/HEVC; it does not change export behavior or add product scans.

Gate command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7. Q1a run4c37adc29e6e406c9f7e398fd2a286cd passed; independent reviewer run3af42697442443a6b79e3aab2e78c04b passed41.461s. Static-FG fix runb3c3df19f8b94d3eae6bcfde6aa68679 exit0 preceded final controller normalization/test assertions, so do not claim identical final executable coverage. Independent review_rgb_foundation scoped PASS with P2 static-FG finding subsequently fixed; this subsequent fix awaits review.

Next: Q1b bounded tiles using the same EnhanceGraph, full-size image export and viewport rendering. Inputs beyond single-texture limit still fail explicitly; memory/model/codec limits are real and arbitrary input support is not complete. No runtime replacement, push, artifact upload, packaging or shutdown.

## 2026-09-08 Q1b tiles and Q2 preview candidate (cycles 53–54)

Previous turn classified progress, not wait/no-progress. Fresh preflight passed; STOP absent. Q1a static FG isolation integrated into Q1b. Added TiledImageProcessor using shared EnhanceGraph (1280 core,128 context halo,64 overlap feather; spatial tiles reset independently). Real full-sized CPU result retained for image save. EngineControllerImage presents bounded viewport sampled from full result, reprocesses on settings only, supports original comparison, cancel and rollback. Normal presentation uses PreviewView UVs; professional wheel anchors cursor, middle pan/right reset; new media/daily reset fit. Product video export integrity unchanged.

Release build: scripts/build.ps1 -Root . -Preset x64-release, exit0; logs/optimization-goal-20260908/build-q1b-final.log. Current app SHA256 0409DACD716950F3B674D0B105AAC9972B1B85A8AAC8362EECAA27314166F0E6. Shader identities recorded separately; EXE hash alone does not prove shader identity.

Tests: image-55da6ef7d941450cbbb8d934852191a0, exit0,6.400s (run-image-tests.ps1,275s watchdog). NR-off single texture five sizes exact; 17001x17 and17x17001 tiled14 each, maxError0 including independent half-transparent white->188/transparent->black fixtures; cancellation after first tile returns no output. Real NR97x17001:14 tiles,14 Evaluates, full dimensions,PNG exact readback (pixel quality/equivalence not asserted). Earlier single257x513 and97x9001 real NR remain covered. UiContractTests pass includes pointer anchoring/inverse wheel/pan math.

Application --smoke-zoom --smoke-seconds 9/10 tests: zoom-large-5ae8f78c7c5442ca982871c5638d7c82 (17x17001 NRoff) saved same PNG SHA3E3331C3FF73E636F4F37467F8F0175EE2F1158772E24A5EB0AC1F1A69F525C1 despite zoom; zoom-nr-7918200c1f554761b4af22d7ffd68db4 normal257x513 NR1 unchanged; final app zoom-large-nr-0b7a2fdd8cef4cc1855094d3d3cedbe8 NR14 unchanged,97x17001 full save. Session/revision/output extent preserved; zoom/pan/reset/daily flags pass. App processes bounded25s. Runtime output/dimensions covered; screenshot pixel comparison and real pointer hover routing still need stronger evidence.

Final phase7: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/bb35fca4c4e64b269caa7a5eb4243a2d/result.json 41.140s, same app0409..., all software checks pass. Prior run0ff376...41.203s is older app1B943... . Software gate is not full Goal approval. Q1 video model/codec edge limits and tiled visual seams/quality still need review; Q3–Q7 remain. No capture hardware claim, publishing or shutdown.

### Cycle55 reviewer P2 recovery fix — 2026-09-08

review_tiles_zoom independently passed software gates (3637158bbc434cb3aa6af32f9d62935c40.906s; image-bb9100790cf54d5c85237c8afc1da9d0 6.465s) but scoped verdict FAIL for unhandled save allocation exception and presenter-before-drain unwind. Finding accepted and repaired, not dismissed by green gates.

All-exit cleanup guard now drains shared queue before presenter/graph destruction. Large-image save and settings exceptions retain last successful result; ordinary image saves also preserve playback. WIC channel packing is bounded to row bands instead of another full-size image; existing decode-back extent verification remains unchanged. Partial file is CREATE_NEW-owned and removed after WIC closes on failure, or disarmed after successful rename. No new integrity scans or video-export changes.

Fault injection is opt-in VEYRA_TEST_LARGE_IMAGE_SAVE_THROW=1, once after actual WIC WritePixels (partial exists), not actual system memory exhaustion. App save-recovery-15610c7429934b15803b3e0a5cbc6751 9s smoke/25s watchdog: exception caught; session/output retained; same-path retry success; no partial remains; saved SHA equals source3E3331...525C1. Zoom session/revision/output assertions still pass.

Build scripts/build.ps1 -Root . -Preset x64-release exit0 (build-save-recovery-final.log/build-save-tests.log). Final app SHA5E7BC723B647903851C4DD0265F6CDC1A16BBDCE9AEEF07D6E213808F49BE95E. Image integration image-98845305ab6045a79311ef503ecfee9d exit0,6.515s: prior cases, cancellation, new PNG/JPEG multiple-band1024x3073 solid RGB fixtures pass. Final phase7 via scripts/loop-gate.ps1 exit0; logs/delivery/04815e94852a435a83840eaa45ba0ac1/result.json41.246s matches final app. Reviewer recheck pending.

Read-only Q3 diagnosis: CaptureCardSource packet duration is still default known0; downstream clamp selects83333 ticks incorrectly. Source colorInfo defaults BT709/SD601 but graph reads unresolved AVFrame fields and uses different transfer/matrix defaults. Capture RGB32 also leaves transfer/matrix unspecified and alpha is not guaranteed meaningful. Next Q3 must resolve once, carry actual sample/nominal duration, and retain explicit fallback logging; no Q3 code changes yet.

Independent recheck PASS for implemented Q1a/Q1b/Q2 and cycle55 save recovery; see docs/REVIEW_IMAGES_PREVIEW_2026-09-08.md. Current Phase7/Goal remains in_progress. Prepare local checkpoint only; next close remaining input/preview evidence, then Q3–Q7.

## 2026-09-08 cycles56–61: input/preview evidence, duration and color

Prior turn progress checkpoint3b2806b; clean startup and preflight passed, no STOP. Phase7 optimization remains active.

Two-dimensional2561x2561 image tests:3x3 tiles,NRoff maxError0 including intersections;NRon9 real Evaluates. Final combined image matrix image-97a039da3acf457b91046ba4e93831e1 exit0,9.532s. Earlier image4a730...9.300s before color changes. This verifies coverage/execution; natural-image neural seam/context equivalence still not asserted.

Actual VideoPresenter GPU framebuffer geometry: preview-pixels-96eb8b4f9f9b44668dcfb3cb7a935ede,exit0,<1s/25s watchdog. Fit/zoom/pan/reference/reset pixel assertions passed; fit.png/zoom.png viewed. readPresentedFrameForTest is explicit diagnostics only and has no production callers. Normal playback/video export do not read pixels back. Root-window queued hover/focus test --smoke-hover: hover-0579d93ffdf14b3db425da4da6dfc553 exit0,9s,actual WindowFromPoint route with focus on ModeSwitch button;zoom/pan/reset/session/revision pass. Initial hover-fe036...failed because smoke checked in same timer before posted message could run;log shows actual route immediately afterwards. Fixed smoke's150ms post-message wait, not product logic.

Actual H264 YUV444 video import: video-dimensions-4790082c088444288795325b778e400a,257x513 and1280x2561 each6frames,NR6/NVOF5,app exit0,6s smoke/25s watchdog. Old even and2160-height import guards no longer block these inputs. Does not promise unlimited model/codec dimensions or extend export codec contract.

Q3 duration: CaptureTiming derives duration from complete IMediaSample GetTime;missing stop uses negotiated nominal;unknown stays unknown. FramePacket default duration is unknown. Scheduler rejects zero/invalid duration as measured interval and uses explicit nominal fallback;clamps before integer conversion. Unit14 checks pass:30/60fps,knownzero,missing/invalidsample,huge duration. Initial build-duration.log failed Windows max macro;parenthesized numeric_limits(max) fixed. No physical capture test in this run.

Q3 color: shared ColorMetadata resolver combines declared AVFrame fields with source fallback (per-frame explicit wins,assumed values adapt to decoded format),SD601/HD709,YUV709/RGBsRGB defaults with assumed logs. Source metadata and graph swscale/shader share it;player and video export pass packet colors. Captured RGB32 source reports same resolved metadata. Explicit BT2020 conversion rejected instead of misinterpreted as709;HDR policy unchanged. GPU YUV neutral128 gives142(BT709),130(sRGB),189(linear);SD601red254,0,0 within golden tolerance. File parser defaults now use resolver. Capture RGB still traverses NV12 and remains a Q3 optimization task.

Commands: scripts/build.ps1 -Root . -Preset x64-release exit0 (build-color-capture.log); veyra_live_timing_tests.exe14 PASS (duration-unit.log); run-image-tests.ps1 (275s bound) and explicit25s-bounded PreviewGeometry/app tests. Latest phase7 via scripts/loop-gate.ps1 -Gate phase7 exit0; logs/delivery/f3fbdc84e9c54e718e03a94066df28bc/result.json41.453s appCDD4A16CC66A462A907EFA9EB82C10D22AA58A7B057BAEFDFAF4FB0EC055DC4F. Earlier duration gate e2fee...41.489s was previous app9A5C... . Latest small metadata-format changes covered by gate; GPU color goldens precede those small changes and await independent rerun. No new export integrity checks, publish, runtime change or shutdown. Independent review pending; Q4–Q7 open.

Cycles56-61 independent review_color_timing PASS: docs/REVIEW_COLOR_TIMING_2026-09-08.md. Full Goal still open.

Cycle62 RGB capture: image-1660346e66504f8bac949030915d7398 11.706s PASS, exact saturated color / opaque BGR0, cut1 NR4 NVOF2. phase7 8211c4bf969647d099f62cd3cd11d96a PASS; app410EE473... Review pending; real capture unexecuted.

Cycle62 independent scoped PASS: docs/REVIEW_CAPTURE_RGB_2026-09-08.md; full Goal not approved.

Cycle63 SR fixed image87b550aa16.462s and motion60c12206415.8s PASS; diagnostic failurebd4905c3 root-caused/repaired actual texture sizes. Official linear flags0x41; same-size/height-only validated. WORKLOG details; review pending.

Cycle63 reviewer scoped PASS: docs/REVIEW_SR_CONTRACT_2026-09-08.md. FullQ4 visual reconstruction and Q5-Q7 remain.

Cycles64-65 protection GPU imagea812c02d19.450s PASS, presets migration PASS, phase7b9de57f741.751s PASS, finalactualbuttons329fc51e UI9s PASS. App098F6950... NR-only, feather quality/SR/FG/natural andQ6Q7 open. WORKLOG details.

Protection P2 fixed; actualUIcd4088559s PASS dirty draft+master-off same revision indicators. App0654C61F... independent recheckpending.

Protection final independent scopedPASS: docs/REVIEW_NR_PROTECTION_2026-09-08.md; app0654C61F... phase7a178db7f41.477s. P2closed withdirty/master-off regressions; Q5b/Q6/Q7 stillopen.

## Cycle 66 - protection boundary diagnostics (2026-09-08)

Scoped test-only change in tests/integration/ImageDimensionTests.cpp: feather 2/32 pixels, disjoint regions, and partial 2561x2561 mask spanning central horizontal/vertical tile seams. No product code, export integrity or runtime changed.

Commands: scripts/build.ps1 -Root "$PWD" -Preset x64-release; scripts/loop-gate.ps1 -Gate preflight; logs/optimization-goal-20260908/run-image-tests.ps1 (275s watchdog); scripts/loop-gate.ps1 -Gate phase7. Build log build-protection-boundaries-final.log. Initial runs image-eeba5f9f5a934480ab91d3307fbdc74a (17.620s) and image-97deff3da67c4a3e94c731e561377964 (17.654s) failed a new test assertion: unsigned 192-x subtraction misclassified exterior pixels. Diagnostic per-mode evidence isolated the assertion; changed to 192.f-x, no product adjustment.

Maker image-262e170a7cef4014b6287f6c4c022f22 passed 21.828s. Independent review_protection_boundaries PASS with no introduced P0/P1/P2: preflight71; phase7 logs/delivery/d8846bbfe5d54b41b7b14327e8897975/result.json 41.695s/75checks; image-e8c7b9377cd94f68bd50fb189bd8b49c 21.916s. Actual Feature18 Create0x1 non-null SEH0; seven protection evaluations with original/baseline error0. Feather2/32 has 1581/15725 mixed channels, envelope error0. Partial nine-tile/nine-NR interior/exterior error0, envelope error1. Baseline comes from same run, dimensions verified.

Image-test SHA256 052F7DE99BB622DDF05F977D5431C8257F26330119CF7760072126C3DC655481. App remains 0654C61F11D9FC50436B964F385E4028E6458C4B9D186B6DCD5B0796C383B4F5. Reviewer tracked fingerprint f80219622037f7671d65046e14313267f9bb85ce unchanged.

Limits: these assertions prove endpoints and bounded nontrivial blending, not exact smoothstep or perceptual smoothness. Central seams covered, not every seam with a protected region. Moving HUD/temporal behavior, natural quality, SR/FG protection and private resource effectiveness remain unverified. Phase7 and full Goal remain in_progress. Next atomic task: moving-background/static-HUD protection matrix, then isolated optional NR resource contract; Q6 motion confidence/FG and Q7 candidate ROI remain pending.

## Cycles 67–68 - temporal protection and private-resource decision

Previous Goal turn was progress (3d73860 boundary evidence). Cycle67 normal temporal graph synthetic moving background/static HUD/caption replacements: final logs/optimization-goal-20260908/image-0f6adfcbc36b4eafb785d67f7a15e926/result.json26.004s PASS; 12NR/11NVOF/11motion frames per sequence, reset1/cut0, protected/outsideerror0. Positive controls changed1800777background and467773HUD channels;28350captionchannels genuinely changed to current input at frames4/8. Four actual captures saved per sequence; frame4 protected viewed. This is synthetic hard-mask proof, not natural footage or moving-mask/feather temporal quality.

Cycle68 adds an unset-by-default diagnostic NR parameter callback (EnhanceGraph.h/.cpp) and explicit optional-resource probe in ImageDimensionTests.cpp. No normal-path pixel readbacks, export checks or UI resource binding. Full private-resource expected contract FAIL is retained, not converted to PASS: R8 12.736s andRGBA8 12.912s exit1; Create/Evaluate successful/debug0, but ControlMask-one differs57 and UIAlpha-unprotected differs2 in final8-bit output. Raw protected output matches proxy while parity final differs8 from original. Diagnostic-state bug corrected; signedI32 andRGBA8 hypotheses did not fix mismatch. Full trail and primary citations: docs/NR_OPTIONAL_RESOURCE_FINDINGS_2026-09-08.md. Stop repeated hypotheses without new evidence; retain exact post-NR protection, proceed Q6. Final image executableF574508156F15E51B1E9FB932BBA3F96589E7B8A4B3DCA85DE71D046A3B6F6B0. Build via scripts/build.ps1 -Root "$PWD" -Preset x64-release, log build-nr-optional-format.log. Tests use run-image-tests.ps1 with275secondwatchdog; optional flags separately retain exit1. Independent review pending.

## Cycles67–68 independent review and checkpoint

review_nr_temporal_optional final scoped PASS; no introduced P0/P1/P2. Preflight71, phase7 logs/delivery/2b2498a58135471e981779c989390d31/result.json41.739s/75 checks; normal image6508c7ccffcc45c6a46ca158e14fe0d7 exit0/26.855s. Optional R8 image-d5f8d7387035495f95d22bc04d705857 exit1/12.753s andRGBA image-d997436007bd4a3c9214adbd550f0dd8 exit1/12.526s independently reproduce expectation mismatch with debug0. Optional failure is retained; no private resource integration approved. Reviewer actual frame4 protected PNG viewed; callback product-default-disabled and resource lifetime/state transitions verified. Diff fingerprint9bdf08fe0712eb335c4e1f027b450b2a3de0cfd8 unchanged. AppB48EB58A70FCB1B25014AD27688B7A3DA90058DD2BD8A0ED4AC383E57E2D0F1C, imageEXEF574508156F15E51B1E9FB932BBA3F96589E7B8A4B3DCA85DE71D046A3B6F6B0.

Existing P2 diagnostic gap found: tests/integration/RepairShaderTests.cpp still uses8 residual constants, actual shader requires24. Current graph image matrix uses24 correctly, but that old standalone harness cannot prove the current contract. Next atomic task fixes and validates this harness, then Q6 adds bounds/photometric motion checks and measuresFG; Q7 and natural comparisons remain open. FullGoal/Phase7in_progress; no practical capture/long-term/distribution acceptance claimed.

## Cycles69–70 - residual harness and fused motion validation

Cycle69 fixes existing P2 RepairShaderTests root constant layout8->24; actual GPU downsample and residual identity at strengths0/1/2 PASS, log shader-cycle69.log (60s watchdog); build-cycle69.log; gate-cycle69.log75PASS. Prior turn classified progress d2921d2.

Cycle70 NvofDensify now uses existing previous/current source-space encoded RGB with raw current->previous vectors. Cost>=32 rejects as before. When validation enabled, out-of-bounds reprojection clears confidence/motion; bilinear previous-luma vs current-luma mismatch smoothsteps trust from error.03 to.15, attenuating motion and existing cost confidence. This is a bounded heuristic, not a probability or safe history-exclusion guarantee. It fuses into existing dispatch (4SRV+2UAV); graph transitions A/B toSRV thenCOMMON after NVOF fence synchronization. No new textures, models, CPU readbacks or source-frame waiting. Default enabled; --legacy-motion is quality-probe-only A/B with distinct configHash and motionValidation0/3 JSON.

Standalone GPU matrix (before product GPU integration): known+2,+2.5,-2.5pixel displacement, high-cost cells, both-side out-of-frame, strong mismatch and partial mismatch. Final motion-shader-final.log: legacy128rejected/896retained; validated416rejected/408retained/200attenuated; errors0. Source geometry analytically known, not inferred fromNR outputs. Build-motion-validation/final/fractional.log exit0.

Actual SR60/NVOF59/NR60 plusGBV: motion-final-158d0996302841c68344adf749bda65f vs motion-final-legacy-af959ca76cb740a19ccada074c5d6a13, failures0. GPU command-list P50 3.7572vs3.8628ms, P95 24.8223vs24.5440ms, duration5.3vs5.5s; these noisy mixed-command measurements do not prove speedup or isolated shader cost. VRAM headroom7882MiB both. Normal image+temporal protection matrix image-e306179d8fa74490a54d50745f69db3a27.397s PASS; protected/outsideerror0 and freshcaption28350 retained. Each invocation<=275s. Export integrity unchanged. Final gate/review pending. Q6 still needs occlusion/scene/FG/natural corpus and A/B bidirectional ROI; Q7 remains pending.

## Cycles69–70 independent review

review_motion_validation scopedPASS, no introducedP0/P1/P2. Independent preflight71; phase7 logs/delivery/240031138e804d7f84a42a57a2069759/result.json41.996s/75PASS. Initial reviewer environment missingWindowsPowerShellmodulepath/Get-FileHash fixed; first failure retained, not product failure. Motion/repaired residual GPU tests PASS, logs review-motion_validation.out.log and review-repair_shader.out.log. Image29e690f3e6ba4492aab270cbbdefc06825.692s PASS, current protected/outsideerror0/captionfresh28350. Quality review-motion-2041c730446f48f9a4a5f846d13e8074 and legacy045036253b284931b5d5f3a7b4f61d98 bothSR60/NR60/NVOF59/GBV0/failures0,5.4/5.5s; Create0x1Success/SEH0. No speedup claim. All paths under logs/optimization-goal-20260908 unless explicit delivery.

AppD14DFB94A933E1BE85B9972CADB628BC58E320C26D53B4887F2102BFA2E48D46; quality21A265B0645BD8FA5556E527776155FD26AB7F65DF2EBAC9E523CF653D0988F1; motiontestC5F3AE03B74EB56B106C070FD275095811DE0CE18599411B564C1F2B85C25428. Reviewer trackedfingerprinte3db68bd6d230d63b0752d9f5d6fa84be6837252 unchanged. Product export integrity remains unchanged.

Limits: encoded-luma.03/.15 heuristic cannot detect isoluminant mismatch or repetitive wrong matches; shortened/zero motion does not prove safe model history exclusion. Unit matrix currently horizontalpositive/negative/fractional only; next atomic task extends nonzerovertical/top-bottom/cost31-32/intermediateconfidence, then actual occlusion/cut/FG comparisons and A/B bidirectional cost/benefit. Natural footage/Q7/physical acceptance remain pending. Phase7/Goalin_progress; scopedpass only.

## Cycles71-73 scoped scene repair candidate
See docs/SCENE_MOTION_CORPUS_2026-09-08.md. Motion vertical/cost GPU checks and scene14 checks PASS. Fixed all3 authored cuts missed by old .3 SAD threshold; actual shared graph logs150/300/301/302/450,600NR/594NVOF/debug0. Four other600frameclips zero new boundaries. Actual1080p2X export fg-scene-b1e15c6c88b64b00aee2b6af5f8c4990 exit0:600source/594generated/6hold/1200output; full diagnosticdecode1200,5boundaryholds matchpreviousYmean<=.049/255. Buildcycle73exit0. No exportintegrity change, no naturalquality conclusion. Frozen gate/review pending.

## Cycles71-73 independent scoped PASS
review_scene_boundaries no introducedP0/P1/P2; docs/REVIEW_SCENE_BOUNDARIES_2026-09-08.md. Independentpreflight71/phase7delivery55725c1cd58846c59342d5e62456277341.568s75PASS. Freshreview-scene-fc558d881d0b404fbe51e5f6f2ebd6e0:scene14/motionbothaxesdebug0;RTXNR600NVOF594/Create0x1SEH0;actualFG600source594generated6holds1200output andindependentfull1200decode. BoundarypreviousYerror<=.048694/255. Trackedfingerprint4343fa50af201ad1393b6bec4228e3c18fb4447dunchanged. Q6/Q7/natural/physical/long-term/distribution not passed.

## Cycle74 bidirectional candidate evidence
Shared optional BOTH session and realGPU diagnostic; defaults unchanged. docs/BIDIRECTIONAL_FLOW_EXPERIMENT_2026-09-08.md. Finalbidir-matrix-e47ef77e21b44baaa4eace596d6080cd360p/1080p/4Kall exit0/debug0, known+8/-8EPE.0442px.1080pnovelwrongaccept6330->12, correctbackground101392unchanged;4K25992->44/correct432992unchanged. Fixed-pair warmedthroughput1080p.836->1.510ms,4K2.974->5.571ms; notlatency/P95/naturalquality. Default remainsforward; no graph/exportintegrity/newmodel changes. Frozen gate/review pending.

## Cycle74 independent final scopedPASS
review_bidirectional_candidate: initialP2diagnosticfootprintalignmentFAIL repaired; independent640x129all4casesPASS/debug0 (novel191->73,good1411unchanged). Initial1080/4KindependentmatrixPASS remainsvalid. Finalphase7aea3d88148bf4e6b9c793cd99f95f7fb75PASSexit0; docs/REVIEW_BIDIRECTIONAL_FLOW_2026-09-08.md. Reviewerfinalfingerprint2d3745d722cd9931e12d9ec6200d14d4d66f9924unchanged. Optionalsharedcapabilityonly; defaultforward, no graph/shader/exportintegrity/model changes. Retaincandidatependingnatural/changingframeNR/FGROI; fullGoalopen.

## Cycle75 NR depth controlled response candidate
Newdiagnostictarget only. docs/NR_DEPTH_RESPONSE_2026-09-08.md; finaldepth-response-b8490565d1374d1c9a28a93ecc12b54dexit0/39.416s/debug0. Default/replacement/residentdepth0,1,gradient,checker:rawNRandfinalRGBchanged0;singleframe andall12temporalframes. Intensity0positiveandMVscale0/-1changemillionsofRGBchannels; explicit.5/repeatbaselineexact;NR12/NVOF11/motion11/reset1. Resident originaltexturecontentsmutated afterframe0 toexclude simplepointercacheexplanation. No basisfordefaultNRdepthmodel; SR/FGdepth nottested. Frozen gate/review pending.

## Cycle75 independent scopedPASS
review_nr_depth_response nointroducedP0/P1/P2. Independentpreflight71/phase7e67dc13783b24976a68fd1118c43a18841.694s75PASS. Actualreview-depth-9678e01163c949b999231759f1fe6fc6exit0/40.440s:all24casesreproduce,depthzerochange/intensityandMVpositive,NR12NVOF11motion11reset1/debug0. Raw/residentcopy/pointer/per-frame comparisonreviewed; no universaldepthignoredclaim. docs/REVIEW_NR_DEPTH_RESPONSE_2026-09-08.md. Trackedfingerprintca90e967b7f2c588749b2eaf8dc516fed4b7eaadunchanged; exe0A0CA21053F6CDE55CB023A6AD3D71A24EEEE1C91BA6B91E2E57B894A4D8859A. Do notadddefaultNRdepthmodelwithoutnewbenefitevidence. SR/FGdepth/naturalreconstructionremainopen.

## Cycle76 candidate final matrix
18 cases actual RTX PASS, srfg-final-b3590fc9f2b1478ab4e205897cb2ef2f exit0 31.988s/debug0; SR12 or FG11/NVOF11/reset1; all generated PTS checked. SR depth RGB unchanged, FG ordinal near-depth MAE .237438->.237343, insufficient default model justification. docs/SR_FG_DEPTH_RESPONSE_2026-09-08.md. Build final exit0; frozen gate/review pending. Export integrity unchanged.

## Cycle76 independent final PASS
Initial P2 incomplete-FG-sample assertion repaired; independent18cases review-srfg-final-e1efc4f1b8124f8f9bea0aa7423b8345 exit0/32.210s, actual Create/Evaluate0x1 SEH0/debug0. Finalpreflight71 and phase7 bf527e55e4914c8aa93451ec78121f18 41.408s/75PASS. docs/REVIEW_SR_FG_DEPTH_RESPONSE_2026-09-08.md. Defaultdepthmodel remains absent; no verified cost/quality case for adding it. Next: known4K reconstruction against spatial baseline, then natural/changing-frame quality. RTX Video SDK absent from project and filename-targeted Downloads search. Export integrity unchanged; fullGoal open.

## Cycle77 reconstruction candidate
Actual1080->4K sharedgraph two24frame scenes: final0-b240e19c658c43d2bc06b06c4f4442ea17.030s andfinal1-42eb27fb292f4be0814b1e6d2c42de0819.956s exit0, NR/FGoff SR24/NVOF23/reset1/debug0, SRrepeat exact. SR lowers spatialMAE but increases temporalerrorandmaxerror; docs/SR_RECONSTRUCTION_2026-09-08.md. No naturalquality or defaultreplacement claim. Buildcycle77finalexit0; frozen gate/reviewpending.

## Cycle77 independent final PASS
review_sr_reconstruction: full24frameSR/spatial metrics reproduced, SRrepeat exact; twoactualRTXinvocations17.861/22.122s, debug0; docs/REVIEW_SR_RECONSTRUCTION_2026-09-08.md. Independentpreflight71/phase7 268ee4b316bf4604bedafcd9b747b09c41.718s75PASS. DocumentationP2 initializationFGwarmup distinction fixedandreadonlyrereadclosed. No default/product/export change. Next isolate exactmotion vsestimated SR guidance, and natural material; recent-app source exists (ffprobe H2641920x1080,30000/1001,limitedBT709,56.689s), not native4K reference. FullGoalopen.

## Cycle78 SR motion response candidate
Actual two8mode 4K matrices final0-978ea39fa951411f9a3f9d82f2239c4148.652s andfinal1-f755274a89804bd19d4da245b3a712b255.509s exit0; resource width/format/disabled negativecontrols reject; SR24/NVOF23/reset1/debug0 andestimated/exact repeats identical. Exactmotion improveserrors, zero/inverseworsen spatial; defaultfilter slightlyworse thanlegacy onbothsyntheticcases. docs/SR_MOTION_RESPONSE_2026-09-08.md. NewSR-only borrowedprobe unsetinproduct; no defaults/export change. Frozen gate/reviewpending.

## Cycle79 — professional move/resize callback stack repair (2026-09-08)
User WER dump 33456: C000041D at module RVA BB0F7, same-object linker MAP identifies __chkstk; AppShell WndProc reserves0x36A28 bytes, nested layout/control/modal callbacks exhaust stack. Move 32768-wchar per-operation path buffers to vector heap storage; retain path capacity and independent modal lifetime. Fixed compiler reservation0x6A48 bytes. No STACK linker increase or exception swallowing.
Build: scripts/build.ps1 -Root <project> -Preset x64-release, exit0 (logs/optimization-goal-20260908/build-cycle79.log). Native bounded regression scripts/acceptance/ui-window-resize.py: empty18.265s (resize-1788881542706216600), playback18.578s (resize-1788881522216906600), each24moves/24sizes/4selectors, clean shutdown. Playback600NR/599NVOF, failed=false, P951.63ms. These are Win32 scripted operations, not physical mouse acceptance. Early old-binary probe did not reliably reproduce crash; old exit0 without smoke marker is not accepted evidence; final script requires actual completed smoke.
Preflight71PASS; phase7FAIL player-sync in logs/delivery/966863bd15a54bd58849984c73b5105d/result.json. Prior independentCycle78 alsoFAIL54.38ms, preceding this UI edit. No overall gate pass or latency fix claimed. Review pending; no checkpoint marking product passed.

Cycle79 final independent scopedPASS: review_window_stack found no remainingP0/P1/P2 in product fix or regression. Final empty regression logs/optimization-goal-20260908/resize-1788881908641248600/result.json:18.234s,24moves,24sizes,4selectoropens,cleanSmoke=true,exit0. Prior same-binary playback resize-1788881714808561400:18.594s,600NR/599NVOF,failed=false. SHA25686E1EE322C86B539FC7C280F10726AC721F05FF322D5C0CA778EE1F9608C5D45. External25s supervisor+5s cleanup; forced timeout inspected not fault-injected. User mouse acceptance remains; full Phase7 not passed, no product-completion checkpoint. Latest gate concurrent usercapture means no clean latency conclusion.

Cycle80 capture whitelist removed and labels identify native subtype. ActualUSB SS(noSSPlus), firmware UVC2x12 frame entries no1440p. 4K18 YUY2/MJPEG source testsPASS andapp107NR106NVOF,callback18.02fps,0drops. Details/commands/failures docs/CAPTURE_CARD_AUDIT_2026-09-08.md. User resumedcapture0:22:0 after update; no further exclusivehardware tests while in use. Independent scopedreview pending.

Cycle80 independent scopedPASS: metadata list + offline rawUSBdescriptor independently verified, code and actualsource/NRlogs agree, noP0/P1/P2. Preflight71PASS. FullPhase7notretested while user4Kcaptureactive; priorlatenessissueunresolved. Finalexe2587E014D349402254C23C8B82108539B4C586E2C4C1D2C558BC636EB4AB34EB. Audit report contains complete evidence.

## Cycle81 — video SDK trial 2026-09-09
RTX5070 FRUC standalone natural-pan test generated 3 distinct intermediates; Create/Register/Process/Unregister/Destroy=0. Synthetic tests repeated frames and correctly failed. Evidence and build commands: docs/RTX_VIDEO_FRUC_TRIAL_2026-09-09.md. Each invocation external25s watchdog, final exit0. Player PID20272 remained running, no performance conclusion. RTX Video SDK requires official login and was not executed. No product changes or phase advancement; existing Phase7 timing failure remains. Next: SDK acquisition and isolated comparison before integration.

## Cycle82 — RTX Video D3D12 trial
User SDK received and safely extracted ignored. Actual RTX5070 Init/Create/Evaluate0x1 Available1; 1080p->4K all5quality outputs nonblack/different. Warm static8sample GPU medians q1=1.374ms q2=1.774ms q4=5.857ms; not end-to-end/capture comparison. Debug errors0 with retained warnings. Build/readback/timing/logs/limits in docs/RTX_VIDEO_FRUC_TRIAL_2026-09-09.md. Preflight71PASS; per-process25s watchdog. No product code/default changes, Phase7 timing failure unchanged. Next optional shared backend and same-source A/B, not automatic default replacement.

## Cycle83 independent final scoped PASS
Independent review_video_sr: no remaining introduced P0/P1/P2; original backend-switch P1 and draft-loss P2 closed. Independent low->medium->DLSS->low:3 applied transitions, VSR Create/Evaluate/Release3/401/3 all0x1/SEH0; DLSS Create1. Draft0.314159 retained across all3 changes, never secretly applied. Preset v2 protection/feather and defaultquality0 retained; v3quality2 roundtrip and invalid5 rejection pass.
Preflight71PASS; phase7 delivery a76caae2b03b4a96aaed4e4e1ab4eb71 45.295s/75PASS. Initial reviewer environment PSModulePath failure retained; fixed module search and reran. Evidence logs/video-sdk-trial-20260909/review-ui-switch.stdout.log, review-app-switch.log, review-preset.log. Tracked fingerprint98da728f82273048c095e9a12b2fe63c3114def9 unchanged during review; no tracked proprietary assets.
Final exeSHA37574BF6ACCA4E78A26EF10BE0D3AD0CDD90470D8A2353C6D551ACC798006A5B. User can choose Professional/Enhancement/RTX Video SR low or medium, enable SR with realtimeNR; test FGoff then2X. Physicalcard not enumerated this turn, onlyOBS; physical capture/naturalquality/longstability/unresolved historical intermittenttiming remain unaccepted. No overallGoal completion or distribution approval; no new integratedGBV claim.

## Cycle84 physical capture diagnosis
Same userPID22300 USB3Video1920x1080YUY2/60 examined without reopening. NativeNR+highestVSR is bottleneck: NR22.911ms vs realtime6.100ms; same-source realtime/low restores59.51fps vs native/highabout30fps. Temporary q4/realtime,q1/realtime,q1/realtime/FGoff comparisons each13s; originalquality4/nativeNR/FG2 restored. No code/binary change. NRdial is not total latency; rolling1200sample P95 mixes prior settings, so noFGofflatency conclusion frommixedwindow. Report docs/PHYSICAL_CAPTURE_DIAGNOSIS_2026-09-09.md; evidence logs/video-sdk-trial-20260909/physical-*. FullGoal open. Next revise metric windows/UI clarity and user chooses realtime/low forlive60fps.


## Cycle85 — settings page submission isolation: independent PASS
User anomaly log revision29 NR3840x2160 median22.135ms -> revision30 NR1920x1080 median6.154ms; RTX Video SR quality1/output4K unchanged. Historical exact clicks not logged. SettingsWindow FG apply previously submitted hidden enhancement fields; now FG and enhancement reads/submissions and dirty drafts are isolated. No GPU stage order change.
Changed product file: apps/veyra/SettingsWindow.cpp. Regression: scripts/acceptance/ui-settings-page-scope.py. Build command: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <project> -Preset x64-release; PASS, logs/video-sdk-trial-20260909/build-cycle85.log. Maker and independent command: python scripts/acceptance/ui-settings-page-scope.py; PASS (independent stdout logs/video-sdk-trial-20260909/review85-ui.stdout.log; app logs/settings-page-scope-72281e89bf974ce3a6f143945181cf2e.log). Independent actual revision4 intensity1/multiplier2 keeps nativeNR; revision5 applies realtimeNR with fg=1 and435 valid generated frames.
Independent commands: powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate preflight (71 PASS), same command -Gate phase7 (75 PASS,42.678s), logs/delivery/c2bd451c2bb844448b60e78a61714558/result.json. Reviewer review_video_sr: no new P0/P1/P2; no tracked writes. Frozen code fingerprint8f3ebcf6d5f68c02592f52a0194674b7b3f776ae. EXE SHA25688AE23C12616355858AE85A6F8D11E0AC441872A5117665578DF626CAA48306A. Actual RTX execution performed; proprietary assets remain untracked.
Correction superseding Cycle84 restoration claim: nativeNR and VSRquality4 restored, but originalFG2 was left OFF (revision22 multiplier1). Hidden legacy checkbox was stale and was an invalid restoration source. User notified; future comparisons must use applied settings.
Phase7 overall optimization remains in_progress. Scope PASS does not resolve historical intermittent timing or prove physical display latency. NR dial measures NR stage only; rolling windows still mix settings. Next task: configuration-specific timing windows and unambiguous metric display, followed by user real-card acceptance. User app was closed normally for rebuild; reopen updated app for real-card retest.


Cycle86 safety stop: build passed (logs/video-sdk-trial-20260909/build-cycle86.log), but preflight reports file:loop/GOAL_PROMPT.md and control:loop/GOAL_PROMPT.md missing. This deletion was present before Cycle86 edits. Per AGENTS control-plane rule, stopped before GPU regression, independent review and checkpoint; no manifest/gate rebaseline and no restoration of user deletion. UI changes remain unverified candidate. Ask user to authorize restoring the exact tracked GOAL_PROMPT.md from HEAD, then resume verification/documentation/local checkpoint.


## Cycle86 最终独立验收与交接

只读review_video_sr限定范围PASS，无新增P0/P1/P2。preflight69 PASS，phase7 73 PASS，实际命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/loop-gate.ps1 -Gate preflight` / `-Gate phase7`；delivery `logs/delivery/9f7468b334bc457db4b8bf125d4f5ddb/result.json`。减少的2项仅为用户明确删除的GOAL_PROMPT文件存在/hash检查，剩余10个控制文件身份保持。

实际RTX独立UI回归通过（`python scripts/acceptance/ui-settings-page-scope.py`）；额外回归确认master-off保存.55/style1/FG2、重新开启真实应用；焦点内无效文本保留，失焦恢复；注入style2失败后回滚style1且按钮状态同步；滚轮确实滚页但不调参；还原默认生效。证据 `logs/video-sdk-trial-20260909/review86-ui.stdout.log`、`review86-extra-final.stdout.log`、`logs/settings-page-scope-79d029d4883e42fea08d53eecfcc65c6.log`。初次补测1秒等待不足，改为最长5秒观察真实回滚后通过，未用固定短等待误报产品失败。

EXE SHA256: `3BD470D51AD43F7C461F23AB6F9F9B7234DC6F33D84C9DDEB35A463663264E2C`。构建日志 `logs/video-sdk-trial-20260909/build-cycle86.log`；独立review前后跟踪指纹 `9d45fe7ff6b19a67b3a42db2215928b1aecf5b4f`不变，之后仅更新交接记录。单次测试均不足300秒。Cycle86修复可交本机实测；整体Phase7优化仍in_progress，既有统计窗口/间歇时序/实卡体验/长期稳定风险未宣称消失。下一任务见HANDOFF优先处理配置相关统计窗口。

本地Git checkpoint包括Cycles78–86已核对源码、使用指南、交接报告与授权的GOAL_PROMPT删除；不包含runtime/SDK/个人媒体，不push。实际提交ID以本次 `git log -1` 为准，避免在commit内容中制造自指hash。


## Cycle87–88 FRUC/high-FPS candidate

User requested1000fps support andFRUC2/3/4. Source timing admission120→1000, exact timestamp validation retained. OptionalFRUC behind sharedgraph, UI/preset/export propagated, DLSS retained. Same-process multiinstance required per-instanceCUDAcontext; destroy/recreate repeatedlyRegister4/warpSEH, so stopped that approach and isolated runtime in hidden ownedworker with GPUsharedtextures/fence. No normal pixelreadback. Fullreset freshworker is correct in triple-reset tests but has startupcost/capture-drop feedbackrisk. Maker pixel/PTS2/3/4PASS; playbackknownpan有效5/10/15子帧与实际播放计数、NVENC24/36/48frame输出、1000fps2000frames/999.34processedfps及UI切换PASS. Specificcommands/logs/failures in docs/FRUC_INTEGRATION_2026-09-09.md. 最终lazy创建候选等待独立review/preflight/phase7；整体Phase7未完成，不push。


## 独立检查与用户反馈补充

独立review_fruc已报告CPU16+27项/preset、实际2/3/4pixel/PTS/连续reset、UI全后端切换和自有worker进程故障测试通过，无新增已确认P0/P1/P2消息。preflight69 PASS；其实际phase7日志 `logs/video-sdk-trial-20260909/review-fruc-gate-phase7.log` 为73 PASS，delivery `logs/delivery/1c195ef6d3684879b220fc8c13439e83/result.json`。Reviewer随后额度耗尽，未返回最终整体结论；因此记录为independent_checks_passed / final_verdict_unavailable，不伪造最终Reviewer PASS。用户随后明确“我已经测试没问题”，记录用户本次体验通过，不外推其未说明的配置与长期稳定性。程序SHA256 BD76BA1F3D66DF30286E450AD1742E773F48989B5815C175BDC0A297ABF4E94D；workerSHA256 B6F267C3B1C73DE0CB99C4BAD4B5DA9355C42DF7E7F37F21C9891B02B80412A3。


## 用户新请求：GitHub性能归因

已固定7个仓库head，只读源码/官方文档+既有GPU日志；报告docs/GITHUB_PERFORMANCE_AUDIT_2026-09-09.md。确认原生4KNR硬预算与软件同步/日志/重置/统计问题并存，不提供未测占比。未运行竞品、未改runtime/产品代码、未占实卡。下一条任务：配置revision隔离统计，再同源逐项A/B定位等待与计算；不先建议换卡。

## 2026-09-09 竞品对照改进方案（仅文档）

用户暂停间歇卡顿现场追查，要求先对照已有竞品写改进方案。新增 docs/COMPETITOR_IMPROVEMENT_PLAN_2026-09-09.md，按固定 GitHub 审计、当前 controller/FRUC 源码、旧质量方案及已完成专项整理。先统计/日志/有界调度与 reset，再做 CUDA FRUC、硬解和拷贝消减实验；深度/缓存不加入默认路径。明确每项证据、动作、验收与回退，保留导出完整性检查和单次 300 秒约定。最新自然日志未包含用户所述卡顿的处理证据，不给周期性卡顿下确定归因。同步 HANDOFF 索引和 STATE 文档条目，未改产品代码/依赖/默认设置或保护门禁。未运行新的构建/GPU/实卡测试，未推进 Phase 7 或补造 Reviewer 结论。
## 2026-09-09 基础画质审计 — 已执行范围

新交接 docs/BASELINE_QUALITY_PERFORMANCE_REPAIR_2026-09-09.md；本地证据 logs/obs-magpie-rootcause-20260909/audit-manifest.json。真实 GPU 合成 RGB/BGR0 无增强旁路：python logs/obs-magpie-rootcause-20260909/build_probe.py，最终 build/run exit 0，两项 maxError8=0；只覆盖已编译共享图，不包含实卡转换与 VideoPresenter/DWM。未新执行 NR/SR/FG、同条件竞品性能、delivery 或独立 Reviewer。原生 YUY2 强制转 RGB32 的元数据缺口和显示缩放为优先排查方向；色带根因未闭环，产品未修。

Magpie 本地日志显示多数 1440p 处理，不能由 GPU60% 推断同4K速度。用户授权必要兼容 OSS 模块复用，归因/许可证和运行时分离保持。远端 v0.0.1 已公开经 gh 查询确认，本轮未变更。Phase7 仍 in_progress；下一条 R1 同帧四段定位。

## 2026-09-09 原生 YUY2 与实时呈现修复交付

用户撤回实卡 A/B（“不用测试这个，可以确定就是YUY2的转换”），本轮未打开采集设备。实际实现见 docs/BASELINE_REPAIR_IMPLEMENTATION_2026-09-09.md：原生 YUY2/NV12 DirectShow 接收、一次 GPU YUY2→linearFP16、颜色元数据/stride/方向校正，同尺寸 blit/copy 消减、两个批次的异步采集呈现、revision 隔离统计与批量文件日志。保存/暂停/重配置/退出仍按 lease/fence 排空。不能说已同帧实测证明全部色带只有一个根因。

Release 构建 final-build4.log exit0；CPU契约26项、worker11项；YUY2 601/709×full/limited、原生4K及RGB范围 GPU 基准最大误差<=1/255，显示误差0。YUY2八帧NR含7次NVOF/7帧motion；实际Engine文件回放live worker的DLSS2X/4X/暂停恢复/设置/退出10项PASS，FRUC已知15fps平移素材同样10项PASS。没有占实卡。单项均外部timeout290秒。

delivery最终 logs/delivery/37bd4fedc5604642a2a2afeeca46c99e/result.json，23项PASS、42.569秒；NR/NVOF/GBV零错误、4K播放、4K图像、NVENC H264/HEVC音频和2X CFR、取消检查通过。第一次delivery所有功能检查通过但最后Get-FileHash模块未加载而exit1，保留2d62b4ad日志，仅包装导入平台Utility模块后重跑，未改原gate。早期编译头文件遗漏、temporal测试未开启NVOF配置的失败与修正也保留。

独立review_native_capture最终限定复核：所提动态格式/RGB范围/异步统计问题均关闭，审查范围无其他未修P0/P1/P2；它核对源码和真实日志，没有自行执行GPU或实卡，不记整体Phase7 PASS。

另发现旧路径迁移导致本机FRUC运行目录缺NvOFFRUC.dll及其直接依赖cudart64_110.dll。由已有本地SDK复制固定SHA/有效NVIDIA签名的两项到gitignored runtime_local，不修改文件内容。打包allowlist添加固定cudart依赖；仅语法检查，未执行打包或发布。实际FRUC平移回放通过；通用test_av素材返回repeated=true的失败保留，不当有效生成。已发布旧包未更新。

EXE SHA256: 61618AF18F7B985BD4C1FECA06EBE7A07C30EAA01B2CC0682A217E89EAD3C372。原版NR固定SHA一致。已有控制面漂移保留，不改AGENTS/README/CONTROL_HASHES、不声称preflight通过。整体Phase7仍in_progress；下一条任务是用户实测本机新YUY2路径，再按同尺寸同配置证据继续性能优化。FRUC互通/重建等待、GPU临界区、自然运动效果和长期稳定性仍有边界。未push、未重新发布Release。
