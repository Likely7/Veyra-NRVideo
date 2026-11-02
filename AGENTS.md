# Veyra Agent 工作规则

> **2026-11-02 自动剔除重复帧修复（最新）**：用户要求合并到main并重新打包2.1.2。修复在分支claude/dedup-precise-20261102提交后快进合入main，活动树回到main；未push/Release。重打的2.1.2包放在test-packages/claude-build-2.1.2-dedup-20261102，结果见活动树docs/BUILD_2.1.2_TEST_2026-11-02.md末节。
> - 改动：整画面去重容忍约百万分之五的超界分量；删除图内64×36弃帧。
> - 运行目录tests/claude-sr-stage-20261028/app已换成修复后的EXE fb1e3ad7…；Codex的首个2.1.2包目录（含用户设置）不动。
> - 记录：活动树docs/DEDUP_PRECISE_2026-11-02.md；回执reports/claude-dedup-precise-20261102/FINAL.json。

> **2026-11-02 用户授权main整合及2.1.2本地测试包**：最近14文件修复已提交9e836ab并快进main（原8535791）；活动树现main。之前已完成Claude/Codex产品分支均是main祖先；Lecram两线和AverMedia诊断存档继续保留不并。2.1.2测试包已从main产品快照47b5e84构建并验证，PE2.1.2.0；交付test-packages/codex-build-2.1.2-20261102（两GPU包/ZIP/源码/独立配置启动脚本）。8组合同、QML132/0/1、三组GPU短测、ZIP及解压冷启动通过；记录活动树docs/BUILD_2.1.2_TEST_2026-11-02.md，后续仅文档提交。未授权GitHub推送/发布。准确结果见reports/codex-build-2.1.2-20261102/FINAL.json。

> **2026-11-01 极简音量交互修复（最新）**：当前claude/vsync-jitter-wait-20261102工作区同时保留之前补帧修改与本轮QML音量修复，未提交/合并/发布。音量两端/快速拖动、Return/Enter和空白提交、焦点清理及播放条移动手势隔离已验；QtQuick132/0/1，真实Windows极简操作通过。运行目录仍tests/claude-sr-stage-20261028/app，记录活动树docs/MINIMAL_VOLUME_FIX_2026-11-01.md，回执reports/codex-cinema-volume-20261101/FINAL.json。

> **2026-11-02 垂直同步等待跟随抖动（最新）**：用户问60 Hz垂直同步为何仍丢帧，选择“上限3 ms”。活动树现在分支`claude/vsync-jitter-wait-20261102`（基于main `8535791`，未提交、未merge、未push/Release）：显式垂直同步、输出不高于80 fps时，生成帧的容量等待随测得的回调抖动增加，上限3 ms；自动模式和高刷规则不变。`VEYRA_TEST_FIXED_CAPACITY_WAIT=1`恢复固定等待。记录`docs/VSYNC_JITTER_WAIT_2026-11-02.md`，分析`E:/项目/Veyra/reports/claude-vsync-slots-20261102/FINDINGS.md`。320 Hz回归通过。真实60 Hz回放注入1.9 ms抖动：DLSS生成帧提交缺口37.7→5.6/分，VFG中档5.6→2.8/分（短样本，非实际显示丢帧）。原上屏格点估算不能证明屏幕延迟，已撤销其验收结论。Codex于本地11-01完成续接、最小复验及配置/运行组件核验，回执reports/codex-vsync-handoff-20261101/FINAL.json；分支仍未提交、未合并。没有用真实采集卡和高抖动实卡验证。

> **2026-11-01 用户授权并入main（最新）**：Claude的补帧延迟返工及VFG自动调控已原样提交为`6e79a5e`，由main `13016a8`快进合入。活动树现在main；覆盖下面该任务“未合并”的旧状态。合并记录`docs/MAIN_INTEGRATION_2026-11-01.md`，回执`E:/项目/Veyra/reports/codex-main-integration-20261101/FINAL.json`。本轮相关构建、8组合同/设置、QtQuick123/0/1、VFG开关/DLSS6X/XeSS2X短测及列表/节点真实开关和重启保存通过；原配置/默认运行组件保持。没有重做60Hz性能A/B、实卡采集或5060Ti验收；未push/Release，正式版仍2.1.1。

> **2026-11-01 补帧延迟不增长返工（最新）**：用户不接受10-31修复的延迟增长。分支`claude/fg-no-latency-growth-20261101`（基于main 13016a8，未合并、未push/Release）撤掉自动模式队列、长等待和放宽的VFG预算，改平滑呈现锚点，VFG 2X赶不上刷新时这一对改用下一档模型。真实60 Hz实测见docs/FG_NO_LATENCY_GROWTH_2026-11-01.md，回执E:/项目/Veyra/reports/claude-fg-sidefx-20261101/FINAL.json。`VEYRA_TEST_LEGACY_LIVE_PACING=1`恢复旧规则。真实采集卡和5060 Ti未验证，5060 Ti速度是人为加延迟模拟的。

> **2026-10-31 本地main整合**：用户要求把各分支修复合入main并处理兼容。main现含英伟达低延迟三档、RX7000 NR、补帧接近刷新率修复、屏幕采集无补帧扣留修复、屏幕捕获帧率、采集载波Dolby/DTS、F7/F8、60→30取第一份。Lecram两线和AverMedia诊断存档未并入。活动树在main，下面各段“不合并main”到此结束；未push/Release，正式版仍2.1.1。回执E:/项目/Veyra/reports/claude-main-integration-20261031/FINAL.json。RX7000实卡、真实60 Hz屏/采集卡/5060 Ti、PS5载波音频未验证。

> **2026-10-31 补帧接近刷新率修复**：用户确认“开始修”。分支`claude/fg-60hz-pacing-20261031`（基于c6c28b9）按docs/FG_60HZ_PACING_2026-10-31.md改实时图内补帧：输出在刷新率0.65–1.02倍时自动模式走一帧队列，输出不超过刷新率时生成帧等到下一帧到期，VFG跳帧后前2次不计入成本。`VEYRA_TEST_LEGACY_NEAR_REFRESH=1`恢复旧规则。本机只验证320 Hz，未在真实60 Hz屏/采集卡/5060 Ti验证，不得当作现场已修好。不合并main、不push/Release。

> **2026-10-29 RX7000 NR接入**：用户授权在codex/amd-rdna3-nr-20261029（基线e7a9ae1）加入7000系NR与Small性能模型。按docs/AMD_RDNA3_NR_PLAN_2026-10-29.md隔离RedDuke网络worker，保留9000 lmxxf及现有NVIDIA路径；不接旧帧异步残差，不回读GPU图像，不改真实配置/驱动/现有SDK与运行组件。不合并main、不push/Release。本机无RX7000，合同测试不冒充实卡通过。

> **2026-10-28/29英伟达低延迟任务**：当前隔离分支`codex/nvidia-low-latency-20261028`，基线main4351bbd；用户授权完善Reflex并改为“英伟达低延迟：关/开/开＋增强”。直接NVAPI，源帧Sleep与生成帧带外队列标记分开；当前本机DLSS/VFG/FSR原生路径验证，XeSS保持提供方调度并显示不生效。真实证据、失败修正和硬件边界见`docs/NVIDIA_LOW_LATENCY_2026-10-28.md`与`reports/nvidia-low-latency-20261028/FINAL.json`。2026-10-29 Claude复查修正（非英伟达显卡低排队归属、状态逐段翻译、摘要、未补帧通知降级）与屏幕采集延迟对比已提交到本分支（未合并main），见执行记录末节及`reports/claude-nvidia-ll-review-20261029/FINAL.json`。不合并main、不push/Release，不改真实配置、驱动、SDK/模型/DLL；使用私有测试配置。

> **2026-10-28后续（用户选择）**：同分支新增“超分后缩回到”超采样选项：单次RTX视频超分到目标尺寸，再线性光精确缩回`srOutput`，NR/补帧/显示按缩回尺寸运行；双重开启时不用、保留选择。复用`srOutput`字段与`SrResolve`，导出协议14，超采样图同双重图走WDDM预算准入。读`docs/SR_SUPERSAMPLE_2026-10-28.md`及`reports/claude-sr-supersample-20261028/FINAL.json`。用户随后要求直接合并：双重超分、复查修正与超采样已合入本地main（未push/Release），活动树切回main，其余限制同下条。

> **2026-10-28最新用户纠偏（覆盖下面同日早期两条）**：当前分支`codex/dual-sr-20261028`，实施真正的“基础SR→降低第一次AI结果→第二次SR”；双重开关位于基础超分下，展开独立第二设置，默认关闭。普通8K保持原规则，撤回强制4K参数修正/拦截；新双重图按WDDM预算拒绝不足的分配，不偷偷缩小尺寸。当前双重仅RTX视频超分，两次真实Evaluate；本机SDK共享Feature，不宣称两个独立SDK实例。实际验证与限制读`docs/DUAL_SR_PLAN_2026-10-28.md`及`reports/dual-sr-20261028/FINAL.json`。不重跑原8K最终输出+NR+VFG危险组合，根因未证实；不改真实配置、不合并main、不push/Release。

> 2026-10-28后续：用户报告接手测试版在1080p先降/8K最终输出时整机卡住重启。当前修复分支`codex/sr-stage-freeze-20261028`，依据`docs/SR_STAGE_FREEZE_2026-10-28.md`；原存档同时开NR与VFG 2X，不得再无防护重跑原组合。当前将三段式SR的NR/FG最终输出限制到4K，保留8K内部超分；这不是底层驱动挂死已根治的证明。真实用户会话仅只读及保全，不直接修改；私有副本验证。无合并main/push/Release授权。

> 2026-10-28当前任务：用户授权接手Claude的单层RTX视频超分功能，继续使用`claude/sr-stage-20261028`与现有施工树。范围是先降/超分/再缩的单层链，按`docs/SR_STAGE_HANDOFF_2026-10-28.md`续接；不自动扩展多层，不合并main、不push、不Release。接手前未提交改动已归档，原生NR先行/较小输出边界与导出协议需以本轮回执为准。

## 现在从哪里开始

- 当前正式版 **2.1.1**，发行标签 `v2.1.1` / 源码标签 `v2.1.1-sources`，发行提交 `2fecacf`，已完整进入main。
- 当前施工树：`E:/项目/Veyra/worktrees/screen-recording-fix-20261022`。通用Git库仍在桌面旧工程`.git`，不可把桌面当普通副本删除。
- 先读 [当前状态](docs/CURRENT_STATUS.md)、[开发与验证](docs/DEVELOPMENT.md)、本任务执行记录及 [WORKLOG](docs/WORKLOG.md)。按任务需要阅读具体报告、运行库身份与许可证，不再要求每轮通读所有历史方案。
- 用户于2026-10-27明确授权执行项目整理1–4：清过时产物、收拢分支、归档文档、分职责整理桥接/QML并回归。本轮基线main `7bf02d4`，结构整理提交`d4b1034`已验证，后续从main继续，准确整合HEAD见本轮FINAL回执。**不push、不发布**，Lecram继续暂停。
- 完整历史授权与原规则在 [历史索引](docs/history/INDEX.md)；旧“未合并/未发布/冻结”按当时日期理解，不能覆盖用户后续明确授权。

## 工作纪律

1. 诚实报告实际结果，不迎合、不把推断或历史测试当本次验证。已验/未验、用户反馈/本机测试分别说明。
2. 开工检查`git status`、活动分支、工作树与当前进程。不覆盖未提交工作，不停止用户进程，不抢采集卡或真实主机连接。
3. 单Agent；构建并发最多2、单次构建≤900秒、每个测试≤300秒。不要恢复旧Loop、STOP、控制哈希排队；当前任务按最新用户指令及执行记录。
4. 不改真实用户配置、驱动、运行库、模型或凭据。测试使用任务专用data-dir/log/TEMP，TEMP/TMP只对子进程设置，不改系统环境。
5. 每次只处理可审查的职责边界；结构整理保持接口、配置格式、默认值、信号语义和处理行为。改动后做相关构建与回归，失败保留证据。
6. 不顺手修改UI统计33ms/状态250ms刷新、GPU默认优先级或引入持续刷新。菜单保留原交互，不用淡入淡出掩盖文字抖动。
7. 不发跨会话/邮件/群消息，不评论或关闭issue，不关机，除非当前用户明确要求。

## 产物、依赖与清理

- 全部新构建、下载、日志、测试、解压、临时和打包产物统一放`E:/项目/Veyra/{build,tests,logs,tmp,downloads,deps,test-packages,releases,verify,reports,archives,worktrees}`对应任务目录。源码、受控脚本/文档留Git；不要向桌面、C盘或系统Temp新增研发产物。
- 依赖实体`E:/项目/Veyra/deps/core`；Qt实体`E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64`。`C:/veyra-deps`和`deps/core/qt-veyra`是兼容Junction，保留入口，不在C盘重建依赖。清单`E:/项目/Veyra/deps/DEPENDENCY_ROOTS.json`。
- 当前可复用构建`E:/项目/Veyra/build/claude-screen-recording-20261022`；2.1.1正式归档`E:/项目/Veyra/releases/release-2.1.1-final-20261027`。打包供体与必要源码依赖按开发指南核对。
- FFmpeg保持实际patched版本（PS5 H.264 slices256、dav1d、压缩PGS所需zlib），并保留实际对应源码、补丁、构建记录及许可证；不可退回未打补丁底包。
- 正式归档保留2.0.7及以后已发布资产和对应源码。一个任务共享一份运行目录，以data-dir/log隔离用例；结束后清掉重复运行/解压副本，保留最终交付和必要证据。
- 清理前核对绝对目标、父链reparse、当前进程、hash/文件身份和恢复清单；不把硬链接路径长度相加当实际回收。不修改历史guard/start来制造通过。
- 脏树、唯一脚本/配置/素材、SDK/模型原件、Git共用库和许可对应源码保护；归档分支须有tip映射和verify过的bundle。不要强制移除脏工作树，不复制整工程作日常备份。

## 产品与实现合同

- Windows x64、C++20、D3D12、Qt/QML主界面；直接NGX，不再接一套Streamline链路。播放、采集、图片/视频导出共享增强逻辑，不复制三套颜色/Guidance/reset。
- 采集卡/视频只有最终像素。估计光流、深度等不能宣传为游戏原生数据；要插A与B之间的帧必须等B到达，不能把驱动缓冲当免费未来帧。提交FPS不等于物理刷新率或屏幕端到端延迟。
- 默认处理顺序与允许的实时低延迟NR→SR路径按现有图执行；FG在效果后，字幕/OSD按现有叠加合同。导出、暂停单帧和图片不得按实时预览跳帧策略丢源帧。
- 输入显式解析range/matrix/transfer，统一工作纹理；输出只做一次明确转换。HDR代理增强与原生HDR模型推理分开表述。
- 历史型模块在seek、转场、PTS断点、capture drop、resize、源切换、暂停恢复和device lost时统一reset。保持有界队列/latest-frame mailbox；不放宽告警、伪造时间戳或静音来掩盖欠速。
- 正常GPU处理不加像素回读、逐pass CPU fence等待或无界队列。使用既有GPU slot/fence轮转和NVENC输入协议，诊断/兼容fallback必须明确标记。
- Guidance motion遵守current→previous、post-SR workingExtent像素单位；低置信度区域衰减，depth/confidence格式及NGX参数类型按既有合同，不凭名称猜。
- 保留RAII、模块/Feature/资源逆序释放和受限绝对路径DLL加载；HRESULT/NGX错误、尺寸/格式、GPU计时须进入日志。
- Smooth Motion由用户在NVIDIA App自行管理；不自动改驱动，不强制与内部补帧互斥，不把驱动生成帧计入软件录制/导出能力。

## 运行组件、来源与发布

- SDK、DLL、模型、头文件、LIB、抓帧、凭据、配置不进入源码Git、LFS、资源嵌入或base64代码。专有二进制不修改、不重签名、不伪装，不从未知来源补齐。
- 当前默认发行组件按2.1.1正式包manifest、`docs/BUILD_2.1.1.md`及其指向的固定来源核对；旧组件哈希在历史规则中。用户允许替换DLL，不用固定哈希/签名拒绝用户替换；发布者默认包仍逐文件核验身份与许可。
- ReShade/RenoDX add-on仅可作隔离对照，主线不加载、注入、链接或随包分发。Lecram研究暂停，不自动继续。
- 有成熟、许可证兼容的开源实现优先适配复用；在代码和THIRD_PARTY_NOTICES记录来源仓库、固定提交、许可证、修改。Veyra原有代码GPLv3；含Chiaki串流的组合程序同时遵守AGPLv3及OpenSSL例外（licenses/remoteplay）。无许可证项目不复制代码，厂商分发权不自行扩大。
- 发布授权按用户当前任务决定；本轮无push/Release。发布包只带已批准组件，manifest、许可证与对应源码完整；二维码区保留两图并排各width220，资产公开后核对真实访问和摘要。

## 交付与续接

- WORKLOG和本次执行记录写实际命令、结果、失败/修复、产物路径、空间新增/回收、已验与未验。
- 提交前检查diff、相关构建和回归，不以接口桩、成功counter、仅编译、同帧重复或理论FPS声称功能通过。
- 用户2026-10-20已确认此前NV显存增长解决、AMD实测无问题，不能把旧调查自动重列为当前缺陷；新改动的硬件验证边界仍按本次证据表述。
