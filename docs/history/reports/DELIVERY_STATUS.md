# 2026-09-08 UI v4：常用操作直出与统一玻璃选择器

日常底栏直接提供打开、采集、最近、总增强、SR、播放、音量、字幕、全屏及窗口控制；720px保留全部入口。采用固定来源的Lucide免费图标（完整ISC/Feather MIT通知），字幕和所有应用内下拉框使用同一Desktop Acrylic弹层。保留真实桌面透明与纯黑视频区域。

基线 `97ccd19`；最终EXE SHA256 `7EEA31511FFA649F3E6B0829F5D1D3A5D454010167A70137786D4430F71E9514`。最终布局/14场景弹层专项1.312秒PASS；40次切换15.008秒PASS；原生NR/SR/NaN草稿/底栏可见性25.858秒exit0，Create/Evaluate实际成功。紧邻的12DIP排版微调前，全UI15命令141.503秒PASS，图片/视频/导出专项11命令80.914秒PASS。报告明确区分EXE版本，不把旧证据冒充最终抓图。单次测试均小于300秒。

实际检查宽窗口、720px窗口、字幕/专业下拉、键盘确认取消。独立只读复核通过其限定代码范围；最后SR文字宽度微调另经主Agent验证。保护文件和runtime身份不变，整体仍Phase7/needs_review，实卡、长稳、分发和既有delivery门禁失败不由本轮UI验收代替。

详细文件、命令、真实日志、失败与截图：[UI v4交付报告](UI_CONTROLS_V4_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_CONTROLS_V4_2026-09-08.md)。下一条唯一任务：用户验收底栏/选择器，并继续原合同的实卡体验验收。

---

以下为历史记录：

# 2026-09-08 UI修复v3：真实桌面毛玻璃

已删除应用内彩色渐变，控制区采用Windows Desktop Acrylic，透出下方桌面或其他窗口的模糊颜色；视频区域不透明、空闲纯黑。同步修复窗口白边、视频全屏、专业NR/SR开关、数值草稿/回滚、滚动裁剪、日常底部布局与240ms展开动画。独立采集对话框仍为普通深色。

基线`b6b251d`，最终EXE SHA256 `B1892C51E8AFC27D7223E271D48D93107C34CAA96BC4014DFF6F45FDE0D7BF74`。build16成功；专项11命令PASS/81.001秒，UI全套15命令PASS/141.187秒，Repair18项PASS/子进程70.478秒，当前4K合同23项PASS/13.751秒。均为每次300秒以内，历史累计保留。40次切换P95 8.191ms/max9.963，442次提交epoch1，GDI12/handles721稳定。真实红蓝背窗、视频字幕、输入/粘贴、全屏及滚动已检查；独立只读复核通过本轮范围。

全项目仍Phase7/needs_review；本轮保护delivery gate保留FAIL（旧23帧断言，当前12源帧2X含尾部CFR占位输出24帧），不修改保护门禁或用补测冒充阶段通过。实卡、真实IME候选、多屏DPI、长稳及公开分发未验收。没有开启采集流或公开上传，没有再次执行历史的一次性关机请求。

完整文件、命令、RTX Create/Evaluate日志、失败和截图：[UI修复v3报告](UI_REPAIR_V3_DELIVERY_2026-09-08.md)；[只读复核](REVIEW_UI_REPAIR_V3_2026-09-08.md)；[操作指南](USER_GUIDE.md)。下一条唯一任务：用户通过`Veyra.cmd`验收当前UI和原合同中的实卡体验。

---

以下保留历史记录，旧的“当前版本/通过状态”不代表本轮：

# 2026-09-08 双模式UI本机软件交付

已实现批准的UI0–UI9：默认日常影院界面、专业四区工作台、共享视频宿主与无重开切换、完整参数/预设、真实音量和透明字幕、后台冻结设置导出、诊断/全屏/小窗口。最初普通Win32排布在用户反馈后重新实现。

基线本地存档`ddc515d`；当前EXE SHA256 `2DE33230CD3A6556BC8E1399DD93BB8959B8EF37B2BACEF23D1486590AA4D3C6`。UI全套15子检查109.583秒PASS；Repair v2 18项PASS，子进程计时合计70.712秒；4K补充23检查14.178秒PASS。真实4K输入/底图/光流/FG/输出、1080内部NR短测59.55源fps、落后P95 1.77ms。每次调用最多300秒，累计历史保留。

受保护delivery脚本此次34.736秒后在旧23帧断言FAIL；基线已有CFR tail hold，当前12源帧2X正确输出24帧/120fps/0.2秒。保护文件未改，补测不替代Phase gate。全项目仍needs_review；当前UI软件交付不等于实卡、多屏物理DPI、长稳、完整组合性能或公开发行通过。

详情、文件、命令、错误码、截图和复核范围：[双模式交付报告](UI_DUAL_MODE_DELIVERY_2026-09-08.md)。操作：[使用指南](USER_GUIDE.md)。下一条用户验收：从Veyra.cmd启动，在真实采集卡上检查新界面切换、声音和体感延迟。本轮未开启采集流、未更改外部应用或运行时、未上传发布。

---

以下保留历史版本记录，旧的“当前EXE/尚未施工/已通过”不代表本次状态：

# 当前本机状态 — 2026-09-08 Repair v2

## 双模式UI改版计划已确认，尚未施工

2026-09-08用户确认：默认日常模式，负责视频/采集；专业模式集中精细参数与导出，切换保留同一播放/采集会话。详细布局、工程缺口、UI0–UI9顺序及验收见 [双模式执行文档](../plans/UI_DUAL_MODE_EXECUTION_PLAN_2026-09-08.md)。本次只写文档，以下Repair v2应用版本和实测状态不变。后台导出与观看并存、音量控制等新增工程仍待实现。

已完成本次三个确认缺陷及源帧计数修复，18项联合软件检查通过，新上下文只读复审通过本次限定范围。全项目保持needs_review，实卡和完整性能/长期稳定尚未验收；不更新旧Phase，不公开发行。

当前EXE SHA256：`1DFE9A6A6963A73350B7677392516DFB23E6C208FABF4514388457183DDDA266`。详细文件、命令、耗时、错误码、参数实验、功能与性能限制见 [最新交付报告](REPAIR_V2_DELIVERY_2026-09-08.md)，操作见 [使用指南](USER_GUIDE.md)。

用户已澄清：单次测试最多300秒，累计耗时不设本轮硬上限。最终联合耗时71.028秒；历史累计346.399秒。SR4K+4X设置测试仍观察到39.75源fps与约2.17秒落后，不宣称这个组合4K60实时。

---

以下完整保留历史状态，仅供追溯，不代表当前EXE或最新结论：

# 本机交付状态 — 2026-09-07

## 最新反馈与待执行方案

用户仍报告 SR4K 开启后严重卡顿、补帧体感未达预期。下面已有的源帧吞吐短测不能证明这两项已修好，也不能证明 50→100 的实际显示效果。

本轮仅按用户要求编写方案，没有改代码、构建、运行测试或更新 EXE：

- [详细修复执行方案](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/REPAIR_EXECUTION_PLAN_2026-09-07.md>)：分辨率解耦、真实 2X、3X/4X、DLSS5 模型/变化量参数、中文、完整全屏与五分钟验证设计。
- [Magpie 功能与难度清单](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/MAGPIE_FEATURE_BACKLOG_2026-09-07.md>)：用户已选择 B1–B5；其余候选分开，按独立实现评估。
- [下一对话接管提示词](<C:/Users/123/Desktop/Veyra DLSS Video Player/docs/NEXT_AGENT_HANDOFF_2026-09-07.md>)：可直接粘贴，明确旧门禁证据边界和禁止改哈希/门禁。

这些新增功能处于计划状态，不代表当前已可使用。B1–B5 已选但尚未编码；现有 F11 基础切换仍需要完善，不是完整全屏验收证明。当前补丁仍为 `needs_review`，不创建新 checkpoint，也不把旧 Phase5–7 共享 gate 当作新版本发布证据。

## 上一次实卡修复的实际状态

当前状态：2026-09-07 实卡暴露的旧帧积压/错误 PTS 等待已修复，本机 EXE 已更新。增强全关和 NR 约49.8源fps，NR+FG约49.4–49.5源fps，短测0采集丢帧（当前卡为1080p50）。软件联合可见窗口回归21项/47.309秒通过。独立复核未完成，体感/音频/实际显示间隔仍待用户确认；不是公开发布。

当前 EXE SHA256 `4A9BA4B321DEEC17C5E3562AF03EF75A316856200A8708FA8E692475A05B59C9`；联合回归 `logs/delivery/01b72df768524af9ab0aa8d2e3dbb59e/result.json`；实卡证据与完整限制见 [CAPTURE_LATENCY_FIX_2026-09-07.md](CAPTURE_LATENCY_FIX_2026-09-07.md)。本补丁状态 `needs_review`，不继承旧 Reviewer PASS。

## 以下是修复前交付历史，不是当前实卡验收证明

最终构建EXE：`70DD23C44337004FC734FBAE8BB6139E185C52370AE97CCE6C3432A3C6F30DE1`；独立联合短测run `9be0614da5d642e394a35c71d80f6207`，40.31秒通过，包含新增4K图片保存与真实NVENC中途取消。下方31.59秒是初测历史，不是最终二进制身份。

## 已有实际软件

- `Veyra.cmd` → `out/build/x64-release/veyra.exe`，Win32 UI + 同一共享增强引擎。
- 本地 H.264/HEVC 播放，音频主时钟，暂停/定位/全屏/开关/最近文件，外部同名 SRT。
- WIC PNG/JPEG 图片输入和保存；D3D12 NVENC 原生 4K H.264/HEVC MP4 + 兼容音轨，FG 2X、取消/partial。
- DirectShow 视频设备/实际格式/HDMI 音频选择；当时没有实卡证据，且所谓容量1邮箱仍持有生产者sample，缺陷已在上述修复中纠正。

## 本轮实测

构建：`scripts/build.ps1 -Root . -Preset x64-release` exit0。

联合门禁：`scripts/gates/delivery.ps1 -Root .`，run `d28879b01b5c44dd86cad33d6f386d90`，31.59秒、16项通过。原始 JSON：`logs/delivery/d28879b01b5c44dd86cad33d6f386d90/result.json`。

- Native 1080：60 NR，真实 NVOF/非零 motion 接入，GBV 0错误，诊断 PNG 可见非黑测试图。
- Native 4K：12 NR，actual source/working/output =3840×2160。
- 4K输入实时档 FG2X：264源帧、263生成帧，短测 absolute lateness P95=14.88ms；不是端到端光子延迟。
- 暂停中 seek → resume → JPEG 保存，exit0。
- PNG 经过实际应用打开/增强/显示，exit0。
- H264 与 HEVC 各12源帧→23输出帧，原生4K；ffprobe `-count_frames`逐帧解码、均保留音轨。
- 本次 EXE SHA256：`900DD752D749036EC482B3B9557AD23D12C60D58A58E37B44C1C960D5DC1B747`。

## 重要修复与否定结论

修了 NV12 错误上传、chroma 尺寸、uint根常量按float传递、motion统计步长；将 NVOF 放到 NR 之前并实际绑定；加入场景切换清历史、命令时间戳、正确退出；修音频环与暂停定位，明确48k双声道浮点格式由WASAPI转换到设备格式。

原生4K GPU分段实测 `logs/f6-profile-split.*`：NR-only约22–23ms、parity decode/output约0.28ms。以前“decode shader24ms”根因结论错误。用户已接受默认实时档/原生4K可选；原生4K60不能凭改队列保证，导出不受实时预算限制。

## 未被证明 / 明确限制

实卡视频音频与按键体感延迟、长时稳定、全面seek/resize/device-loss压力、多音轨/音频转码、内嵌字幕、depth provider、VFR原样时间线、HDR、安装包及专有分发权均未证明或不提供。部分完整产品体验仍是显式错误后手动重开，不宣称无缝恢复。使用范围与操作见 USER_GUIDE。

下一步唯一任务：用户接实卡按USER_GUIDE验收并反馈。软件本机交付停止自动扩展，不重跑历史30分钟矩阵，不自动发布。
