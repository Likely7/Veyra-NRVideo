# Veyra 项目 Agent 执行规则

> **2026-10-19 2.1.0 正式发布（Codex）**：用户已实测并批准 `test-packages/bilibili-2.1.0-20261019` 两包原样发布，禁止重新构建/打包。按 docs/RELEASE_2.1.0_PLAN_2026-10-19.md 整理2.0.9以来全部合入内容、替换用户新6群二维码、更新README/Release，主Release只放两个GPU包，源码另页；允许main普通快进/push/新标签及发布。Lecram两个实验分支暂停保留，不入本版。公告只拟稿，不代发、不关机、不派Agent、不改运行文件/驱动/真实配置；产物全E盘。

> **2026-10-19 2.1.0 整合与测试包（Claude）**：用户要求“之前的所有修复，包括 Codex 那边的都一起合并到 Main，然后打包 2.1.0 测试包”，并审查 Codex 的修复。在 `claude/release-2.1.0-20261019`（main 1dc8c18）合并 fullscreen-input（含 AMD NR 0.44）、#48、美颜（含 E0/AA）与赞助页，版本 2.1.0；先存档 `checkpoint/pre-release-2.1.0-20261019` 与 `archives/claude-release-2.1.0-20261019`。允许本地 main 快进、以 2.0.9-test 两包为底打 NVIDIA/AMD 2.1.0-test 包（美颜两模型按名称+哈希放行、AMD 换 0.44 运行库）。记录见 docs/BUILD_2.1.0_TEST.md 与 docs/RELEASE_2.1.0_TEST_2026-10-19.md。不改运行库/模型字节、驱动或用户配置；无 push/Release/上传/子 Agent。

> **2026-10-19 首页文案与赞助作者页（Claude）**：用户要求首页“今天看点什么？”改为“Veyra NR Video”、小字改为“本软件开源免费，如果对你有帮助，你可以 → 请作者喝杯咖啡”并跳到设置新增的“赞助作者”页；页内放作者各平台主页（图标 + 前往主页按钮，不显示链接）、用户提供的微信/支付宝收款码与 Ko-fi 咖啡按钮。分支 `claude/sponsor-20261019`（main 1dc8c18），产物 `{用途}/claude-sponsor-20261019`，记录 docs/SPONSOR_PAGE_2026-10-19.md。品牌图形取自 Simple Icons（CC0，标志仍属各自所有者），咖啡/外链图标取自 Lucide（ISC）。只改界面、桥接链接、翻译与测试；不改引擎、运行库、模型、驱动或用户配置；无 merge/push/Release/子 Agent。

> **2026-10-19 PR64授权**：用户要求继续合并适配PR64的PR63音频修正，按docs/PR64_AVERMEDIA_PLAN_2026-10-19.md在独立codex/pr64-avermedia-20261019实施；允许圆刚开关/载波/位流解码/会话交付及必要测试/CMake/文档适配，覆盖这些接点旧冻结。存档91旧树104分支/509脏路径（桌面466），无Agent、全E盘、构建-j2≤900秒/测试≤300秒，保留UI33ms/250ms及其他链路/运行库/真实配置。通过定向验收后允许本PR及适配合本地/远端main正常push；不夹带Claude及其他工作、不发布/代发/关机，无圆刚实卡不宣称硬件通过。

> **2026-10-18 PR63接入授权**：用户明确要求合并适配PR63（圆刚厂商音频图）。按docs/PR63_AVERMEDIA_AUDIO_PLAN_2026-10-18.md在codex/pr63-avermedia-audio-20261018实施，允许音频源/位流解码/会话/状态及必要CMake/测试/文档适配，覆盖这些接点的旧冻结。先存档90工作树/103分支状态与哈希、旧桌面466脏路径ZIP（另四旧树43，合计509；统计更正），另记录Claude美颜21在改文件；保留PR57和其他未合分支，不碰Claude施工。通过本机Windows定向验收后允许合并本PR和适配到main、普通GitHub同步，不发布/代发评论/关机。厂商DLL只可读本机已安装原件、无实卡不虚报；专有文件不改不进Git，产物全E盘，构建≤900秒/测试≤300秒、限-j2、无Agent/UI频率变更。

> **2026-10-18 全屏输入修复授权**：用户明确专业模式全屏误暂停/恢复、极简全屏指针不隐藏一起修。按 docs/FULLSCREEN_INPUT_PLAN_2026-10-18.md 在 codex/fullscreen-input-20261018（基于 AMD0.44 f6c4fd9）隔离修改 QML 画面点击与 apps/veyra-qml 原生窗口指针接点，覆盖本范围历史冻结；不改引擎/媒体/效果/刷新频率，不干扰 Claude。全 E盘、私有配置、限并发构建与真实原生 UI 回归；不合 main/push/发布/关机，不派 Agent。


> **2026-10-18 AMD NR0.44隔离接入授权**：用户提供Magpie-DLSS5-AMD-0.44.zip并要求开分支接入，明确Claude同时工作、不得互相影响。从main26c0d61在codex/amd-nr-044-20261018实施，计划docs/AMD_NR_044_PLAN_2026-10-18.md。允许AMD独立C ABI runtime0.44固定4764e4f1、原样权重/HIP资源、必要Small/Original模型参数/设置/UI/共享图及构建部署/定向回归接点，覆盖这些历史冻结；NVIDIA现行路径、33ms/250ms、采集/补帧/颜色合同保持。源包447952711bytes/SHA CF812FE7909FBD53B38F54DA8ABD584698525F81F7EA71CAB19BA14BAB48E030与作者热修相符，875条内部SHA已验证。88原树/101分支/509脏文件及source-before.bundle已独立存档；Claude可正常推进其分支，不重置/覆盖/锁住其工作，不发送跨任务消息、不派Agent。所有产物E盘独立amd-nr-044-20261018，CPU构建限并发，GPU回归前核对空闲，不停止他人进程。测试≤300秒、构建≤900秒。无本轮main合并/push/Release/正式包/关机授权；原SDK/专有DLL/权重/驱动及真实配置不改，不进源码Git。无RX9000实卡须明确未验，不把fake ABI或5070测量冒充AMD推理。

> **2026-10-17 #48 网络稳定与服务器缓冲授权（Claude 子会话）**：用户原话“http点播你随便找个Twitch的试试吧，我也没试过这玩意，然后服务器视频也可能是他的服务器速率太慢了，跑那种超大MKV根本跑不动，我自己手里也没有服务器不能测试，所以你就看着弄吧……开工吧”。按docs/NETWORK_BUFFER_48_PLAN_2026-10-17.md，从main 26c0d61在claude/network-buffer-48-20261017实施#48（报错“源视频包含损坏数据，已停止处理”后黑屏）与服务器视频缓冲：C1网络/MPEG-TS损坏容错（导出保持严格）、C2自动重连、C3连接/读超时拆分、C4点播·NAS·直播单连接共享解复用与缓冲、C5字幕扫描不再另开整流读取、C6设置与信号面板（四语）、C7带宽低于码率的“暂停缓冲”。允许必要的MediaFileSource/FFmpegDemuxer/NetworkInput/AudioPipeline/EngineController快照/Subtitles/QmlPlayerBridge/设置QML/翻译/CMake/测试接点，覆盖这些接点历史冻结；本地SSD文件行为与2.0.9一致，UI33ms/250ms刷新、GPU默认优先级、运行库/模型/驱动/用户真实配置不变。存档checkpoint/pre-claude-network-buffer-48-20261017与archives/claude-network-buffer-48-20261017（bundle已verify，start.json记录88工作树/108脏文件）；产物统一{build,tests,logs,tmp,reports,archives}/claude-network-buffer-48-20261017，单测试≤300秒、构建≤900秒，不派子Agent。Twitch签名令牌不得写入提交/文档/日志；绝不跑PS5模式测试；可在本分支提交，不merge/push/Release/打包，不评论或关闭issue；真实NAS与用户直播源须明示未验。

> **2026-10-17 美颜节点（Claude）**：用户“抗锯齿和美颜好好做，开工吧”（背景：用户同日决定 #48/#53/美颜三项并行，见 reports/claude-plan-48-53-beauty-20261017/PLAN_48_53_BEAUTY_2026-10-17.md）。从 main 26c0d61 在 E盘 claude/beauty-20261017（worktrees/beauty-20261017）实施 Codex 美颜节点方案 docs/BEAUTY_NODE_PLAN_2026-10-16.md（codex/beauty-node-plan-20261016）与复审 10 条修订，计划与 P0 结论写入 docs/BEAUTY_IMPLEMENTATION_PLAN_2026-10-17.md。存档 checkpoint/pre-claude-beauty-20261017 与 archives/claude-beauty-20261017（全引用 bundle 已 verify）。P0 用独立测试程序做人脸定位推理路线对照（系统 WinML / YuNet 手写 HLSL / ONNX Runtime+DirectML）、SkinSoft（yasunari01/obs-filters fd5690f，GPL-2.0-or-later）柔肤 D3D12 计算着色器移植原型（GPUPixel Apache-2.0 作画质参考）、同帧原画与 HDR 合同验证；允许新增 src/beauty、include/veyra/beauty、shaders/Beauty*、tests 与测试用 CMake 目标，覆盖这些新文件的历史冻结。P1–P4 在抗锯齿会话交付共享基础（claude/effects-infra-20261017：链容量、计时掩码、通用插入槽、格式版本）后合入再做，届时美颜节点所需 settings/preset/会话/导出/bridge/QML/翻译/CMake 接点获授权；不自行改 E0 范围。YuNet 权重（OpenCV Zoo 47534e27，MIT）下载到 E盘 deps/claude-beauty-20261017 并记录 SHA256，模型不进源码 Git；若选定路线必须新增第三方运行库二进制，停下等用户授权。不 merge main/push/Release/打包，不派 Agent；日志不记录人脸裁剪、关键点轨迹或像素；UI 33ms/250ms、GPU 默认优先级、运行库/模型/驱动/用户真实配置保持，本机真实 PS5 配对绝不触碰；单测试≤300 秒、构建≤900 秒，产物全 E盘 `{用途}/claude-beauty-20261017`。AMD/Intel、真实采集/串流、光学延迟未验项如实列出。

> **2026-10-17 效果器扩展基础 E0 与 #53 空间抗锯齿（Claude 子会话）**：用户“抗锯齿和美颜好好做，开工吧”（并要求开子会话用 Sonnet5.5 Extra 开工），#53 要求加入 FXAA、SMAA 之类的空间抗锯齿并与 VSR 超分搭配（DLSS 类超分会破坏 UI、VSR 抗锯齿不稳定），参考 SAOG0721/Magpie 的各种抗锯齿方案，以新效果器方式加入：列表模式是“画质”页下的新效果，节点模式是新的节点项。按 docs/EFFECTS_INFRA_AA_53_PLAN_2026-10-17.md 与 reports/claude-plan-48-53-beauty-20261017/PLAN_48_53_BEAUTY_2026-10-17.md 第 3、4 节，从 main 26c0d61 在 E盘 claude/aa-53-20261017（worktrees/aa-53-20261017）隔离实施 E0（EffectType 追加 AntiAliasing/Beauty、链容量 16→32 及满载明确拒绝、导出协议 9→10/预设库 v14/链会话 v9、GpuTimer 64 位掩码、调色专用四类插入槽通用化为每槽 pass 列表并新增源分辨率槽与 NR 组之后槽）与抗锯齿（FXAA/SMAA/MLAA、实验时域 SMAA；EnhanceGraph 接点、新 AntiAliasPass、着色器、bridge/QML/翻译/预设/会话/导出/许可），这些具体接点覆盖旧冻结。E0 提交并回归后建 claude/effects-infra-20261017 指向该提交，供美颜会话合入且不再移动。先存档 archives/claude-aa-53-20261017（start.json：88 工作树/100 分支/主检出 64 脏文件逐文件 SHA256；source-before.bundle verify 通过，76,295,892 字节，SHA256 f22a36f9…1095）。调色输出须与改动前逐字节一致；UI33ms/250ms、GPU 默认优先级、运行库/模型/驱动/真实配置不动，不派 Agent，不 merge/push/Release/打包，不评论或关闭 issue；测试≤300秒、构建≤900秒；产物全 E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/claude-aa-53-20261017。AMD/Intel 实卡与用户 Switch 素材未验须明示，不用本机 RTX 结果外推。

> **2026-10-17 PR57本机接入授权**：用户要求合入其云端Agent的PR57并做好测试。按docs/PR57_INTEGRATION_PLAN_2026-10-17.md，从main d68d6be在codex/pr57-capture-window-20261017保留原head09ac54b/1295d6b整合；允许窗口捕获游戏输入/缩放/resize及采集能力/精确格式/旧版选择入口的必要适配、源/引擎快照/bridge/QML/CMake/翻译/回归接点，覆盖这些范围历史冻结。本机Windows构建/实测通过后允许本地main正常合并与普通GitHub同步以完成PR57；不发布新版本、不改已发2.0.9资产/标签、不代发评论/关闭无关issue/派Agent/关机。84旧树/96旧分支/509脏文件与PR/源码bundle先存档；全E盘，测试≤300秒/构建≤900秒，UI33ms/250ms、既有低延迟/运行组件/模型/驱动/真实配置保持。云端测试待办不扩展当前授权，实卡未覆盖项须明示。

> **2026-10-17 2.0.9 正式发布授权**：用户明确“发布吧，老样子发好给我群公告”。按docs/RELEASE_2.0.9_PLAN_2026-10-17.md从已验bfce70b建立codex/release-2.0.9-20261017，允许已验代码main正常快进/普通push nrvideo/main、新v2.0.9与v2.0.9-sources标签、正式Release及对应源码上传。NVIDIA/AMD沿用已交付2.0.9-test ZIP，外部正式命名，包内字节/test标识保持，不重编译重封包。中英README和完整更新报告覆盖全部2.0.8后改动，保留六群/赞助width220、Discord/Ko-fi/知识库；主仅两包，源归档上传应用源与新增去隔行源，旧FFmpeg/依赖源保留固定链接。83旧树/95旧分支/509脏文件/六原归档与旧Release保护，全E盘，无子Agent/代发/关闭issue/关机。最新授权覆盖此前本轮no-main/no-push/no-Release，不扩展为修改专有二进制、驱动或用户配置。

> **2026-10-17 2.0.9 本地测试包授权扩展**：用户要求移除 Lecram/旧 SF-v2，RTX20–50 默认新版 SF 310.9.16，增加默认开启的性能模型开关、NVIDIA 原版置灰，AMD保持，并构建2.0.9供实测后发布。允许在本轮隔离分支整合已完成的 NR2060、粉丝字幕/鼠标穿透、issues41–45 修复，完成模型契约验证、列表/节点/预设/会话/导出迁移、构建版本与NVIDIA/AMD本地打包的必要接点。此前本轮“无用户包”限制被本条覆盖；main/push/Release/关机仍未授权，旧分支/工作树/历史包/真实用户配置原样保留。候选DLL原件及原NVIDIA/AMD运行时保持身份，不修改专有二进制、不进源码Git；全E盘、测试≤300秒/构建≤900秒、不派Agent/造压、不提高UI33ms/250ms刷新。20/30/40实卡待用户验证。

> **2026-10-17 SF NR 310.9.16 本地替换授权**：用户提供桌面 nvngx_dlssnr.dll，要求开分支接入并明确先直接替换 SF-v2。按 docs/NR_SF_310916_PLAN_2026-10-17.md，从 main b571a44 在 codex/nr-sf-310916-20261017 隔离；存档82旧树/94分支/509脏文件与候选原件。允许新兼容槽原样复制、必要 NR 架构策略/标签/本地 staging 身份及定向构建回归，覆盖这些接点旧冻结/固定身份。候选 310.9.16.0、276162560 bytes、NotSigned、SHA256 CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED，已与作者 compat-fp16-310.9.16-cuda13.4.3 原始RAR解出的DLL完整SHA匹配。不得修改运行库/模型/驱动或真实配置，不派Agent/制造竞争负载，保留UI33ms/250ms与其它效果。产物全E盘，单测试≤300秒/构建≤900秒。本轮无main合并/push/Release/正式包/关机授权；本机5070结果不外推20/30/40实卡。

> **2026-10-16 RTX2060 光流注册修复授权**：用户在 logs(19).zip 诊断后明确“补一下，补之前存档”。按 docs/NR2060_SYNC_PLAN_2026-10-16.md，从 main b571a44 在 E盘 codex/nr2060-sync-20261016 隔离修复 NVOF 资源注册同步、初始化/部分失败清理及 NR 未执行的界面状态和必要回归/翻译接点；这些范围覆盖旧冻结。先完成 checkpoint/pre-nr2060-sync-20261016、全引用 bundle verify、80旧树/92分支/509脏文件与日志备份。默认算法/模型/逐帧调度、33ms/250ms、GPU优先级/真实配置保持，不派Agent、不造压、不改驱动或专有运行库；产物全E盘，测试≤300秒/构建≤900秒。无新main合并/push/用户包/Release/关机授权，2060现场恢复必须实卡复测，不能用5070替代。

> **2026-10-16 粉丝字幕/鼠标穿透适配授权**：用户要求适配所附2.0.8补丁、设置中自定义鼠标穿透快捷键，完成后汇报再关机。按docs/FAN_SUBTITLE_PASSTHROUGH_PLAN_2026-10-16.md从main b571a44隔离codex/fan-subtitle-passthrough-20261016，允许必要字幕缓存/游戏模式/桥接/QML/翻译/构建测试接点，覆盖这些旧冻结。先存档81旧树/93分支/509脏文件和附件，保持其它任务及独立NR修复。产物全E盘，UI33ms/250ms与增强调度保持，不派Agent/造压/改驱动或专有运行库/真实配置；构建≤900秒、测试≤300秒。无merge/push/Release/新用户包授权。完成源码与证据保存并汇报后正常关机，不强制丢弃用户未保存内容。

> **2026-10-16 PR44授权**：用户要求研究合并HDR显示页PR44。基于main8d8eb09，在codex/pr44-hdr-display-20261016保留PR44 head3ee17b6并按docs/PR44_HDR_DISPLAY_PLAN_2026-10-16.md适配HDR显示/曲线/记忆/元数据/低频GPU统计及必要shader/构建回归，覆盖这些接点旧冻结。正常播放不回读像素、只读GPU归约统计且面板隐藏无采样；不提高33ms/250ms频率。78旧树/509脏文件和上一轮issues分支保持；产物全E盘，测试≤300秒/构建≤900秒。不派Agent、不改专有运行库/模型/驱动/真实配置。检查通过允许PR44合main及普通GitHub同步；无Release/用户包/评论/代发/关机授权。

> **2026-10-16 issues45/43/42/41 修复授权**：用户要求解决 GitHub 四项，#43 明确为英文专业模式。基于本地 main8d8eb09，隔离 codex/issues41-45-20261016，按 docs/ISSUES_41_45_PLAN_2026-10-16.md 实施 VRR 可用/生效判定、专业模式状态换行、RTSP/HTTP 地址播放和文件隔行处理及必要的共享媒体/引擎/UI/设置/构建/回归接点，覆盖这些范围旧冻结。77旧树/509脏文件及main开工快照在archives/issues41-45-20261016；所有研发产物E盘，测试每进程≤300秒，构建≤900秒，不派Agent。保持33ms/250ms刷新、真实PTS/有界队列/颜色合同、专有运行库/模型和驱动/用户真实配置；新增独立LGPL去隔行模块，固定来源/patch/对应源码/许可并审计部署；不代发评论、不关闭issue、不push/Release/交付包/关机，不合入旧实验；现场VRR/用户电视节目未验须明确。当前用户仅授权修复，没有另授本轮main合并。

> **2026-10-15 2.0.8 正式发布授权**：用户实测当前两个2.0.8测试包正常，明确要求仅改外部文件名直接发布2.0.8，不重编译、不重打包。按docs/RELEASE_2.0.8_PLAN_2026-10-15.md，汇总Claude与Codex自2.0.7以来全部最终保留内容，重点为延迟链路、窗口/显示器捕获游戏模式、可变刷新、采集卡画面与手柄转发以及搭配第三方远程软件的电脑游戏使用方式；公开说明不叙述用户要求省略的旧显示同步设定。允许本地main文档合入、普通推送nrvideo/main及v2.0.8/v2.0.8-sources标签、GitHub正式Release，主下载区只放NVIDIA/AMD两包，源材料另放非latest归档并引用已有依赖/FFmpeg源。保留六群/赞助二维码各width220、Discord、Ko-fi、知识库。77旧树/509脏文件及五个原归档保护；所有产物E盘，不改产品/运行库/模型/配置，不派Agent、不代发、不关机。内部2.0.8-test及manifest标签原样保留并明示，实测/模拟/未验证边界如实说明。

> **2026-10-15 Codex 接手 2.0.8 收尾**：用户要求读取刚刚 Claude 的对话并完成剩余工作；继承其“B1–B5、直播伴侣兼容、issue38，全部修复含 Codex 合入本地 main，打包2.0.8测试”的范围及具名 dlssg-lite 授权。基线 f074e40，复用 E盘 release-2.0.8-20261015 工作树，切 codex/test-2.0.8-handoff-20261015；原 Claude 分支保留。77旧树/509脏文件及原对话、构建指针由 archives/test-2.0.8-handoff-20261015 独立保护。按 docs/TEST_2.0.8_HANDOFF_PLAN_2026-10-15.md 补齐有证据的验收夹具、构建和回归、文档、main快进与同源码双厂商测试包；保留33ms遥测节奏、原用户配置/运行组件身份/非强制撕裂策略，不连接真实配对主机，不派Agent，不重复执行已完成的关机请求。无push/GitHub Release/上传授权；BMD/GC550/真实直播伴侣和AMD实卡未测须如实报告。

> **2026-10-15 现场修复（Claude）**：用户提交 veyra-qml(56).log（卡顿、未响应后条栏图标消失，重连采集后恢复）与 logs(18).zip（2.0.7 打开采集卡闪退），要求排查修复、汇报后关机。存档 `checkpoint/pre-claude-field-20261015`（main 7571919）与 E 盘 `archives/claude-field-20261015`，分支 `claude/field-20261015`。只改采集格式列表缓存/桥接属性/枚举去重、界面设备重建后图标重绘、采集卡 GPU 失败后自动重连一次、收尾补写 DRED 及三个默认不生效的测试开关（`VEYRA_TEST_CAPTURE_EXTRA_RATES`、`VEYRA_TEST_UI_DEVICE_REMOVE`、`VEYRA_TEST_GPU_FAILURE`）。不改默认 GPU 优先级/NR 版本、运行库、模型、驱动、用户配置；无 merge/push/Release、无子 Agent。长时间变卡与 GPU 卡死根因未定，不宣称已修复。用户追加：帧率菜单默认只列常用帧率（±0.15% 含 59.94 类），帧率多的卡出现“显示全部可用帧率”开关（偏好 captureShowAllFrameRates，默认关）。用户追加（5090 日志 44/57）：原尺寸 RTX Video SR 只给压缩视频，屏幕捕获与无压缩采集格式在原尺寸不跑超分（回到 2.0.6）。用户追加：2.0.8 发布前整体审查，只审查和测量、不改产品，报告见 docs/AUDIT_2.0.8_2026-10-15.md。详见 docs/FIELD_FIXES_2026-10-15.md。

> **2026-10-14 DLSS 性能模式 / 双卡运算 / 全屏 Home 面板（Claude）**：用户“零缓冲先不做吧，现在做”：1）适配 NVIDIA App 新增的 DLSS 帧生成性能模式；2）设置“GPU 占用监控”下加“同时用这块显卡运算”；3）PC 游戏：极简全屏 Home 要能呼出专业模式调整菜单，节点模式呼出节点画布；鼠标透传“先出方案给我，先不动”。分支 `claude/fg-gpu-home-20261014`（基于 e550f96，latency-min 之上），产物 `{用途}/claude-fg-gpu-home-20261014`，记录 docs/FG_GPU_HOME_2026-10-14.md。性能模式 = NGX 创建参数 `DLSSG.LiteCandidateBlendingEnabled` + 只有它支持的 310.9.16 运行库；用户选择“打包进运行库”，授权把 NVIDIA App OTA 下发的 310.9.16 放入 `runtime/experimental/dlssg-lite`（具名例外，见 Release Runtime Pack 规则）；只用于 RTX 50（40/30 解锁补丁只认 310.9.1），默认关，导出用标准模式。双卡：`VEYRA_COMPUTE_ADAPTER_LUID` 软偏好（导出子进程继承），能力与默认补帧方式跟随运算卡，运行中切换按重启规则迁移。透传：用户拍板“在捕获内加一个开关，热键就用你这个吧，不要倒计时，捕获显示器直接去掉黄色的指示框，全屏 Veyra 后相当于也能直接操作整个电脑”，已实施为屏幕捕获“游戏模式”（docs/PC_GAME_MODE_2026-10-14.md：Qt 标志穿透 + 进程内热键/置顶/前台/看门狗；显示器捕获去黄框、控制条不再进画面）；双卡切回恢复补帧设置；VFG 无新版本（High≈15–17 ms/张为模型本身）。屏幕捕获“帧率上限”下加红字（开 DLSS5/NR 建议 60）。PR #7（PS5 只传手柄、画面走采集卡，便于再串流给掌机）：用户拍板“方案A吧，开工！”，已实施（docs/PS5_CAPTURE_CONTROL_2026-10-14.md）：采集卡对话框“PS5 手柄控制”分区；后端从 PR #7 移植并修正（视频收下即丢，音频挂空回调——不挂会泄漏；引擎专用控制线程；共享手柄引用计数；谁按键就用谁；后台转发默认关）；测试开关 VEYRA_TEST_PS5_CONTROL_HOST 只连 192.0.2.1，绝不用本机真实配对；测试包 r6。真 PS5 未测。不改其它运行库/模型/驱动/用户配置；无 merge/push/Release/子 Agent；测试≤300秒、构建≤900秒。

> **2026-10-13 issues36/37 与 BMD 日志53**：用户将延迟工作交给Claude，要求检查issues37/36并继续修复BMD原生仍黑屏。按docs/ISSUES_36_37_BMD_PLAN_2026-10-13.md从main7571919在codex/issues36-37-bmd-20261013隔离修改对应节点布局、文件旋转/呈现运动与BMD明确输入选择、协商/保存及必要构建回归接点，覆盖这些范围旧冻结。73旧树head/status及原始输入独立保护；不修改同步/队列/XeLL延迟策略，不派Agent、不改驱动持久配置/EDID/运行库模型/用户真实配置，不盲切端口。产物全E盘，测试≤300秒/构建≤900秒，无main合并/push/发布/交付包/关机。BMD无实卡不能宣称现场恢复。

> **2026-10-13 更新检查授权**：用户要求真正检查更新、每次启动后台检查、新版本顶部小提示自动消失、无更新静默。按 docs/UPDATE_CHECK_PLAN_2026-10-13.md 隔离 codex/update-check-20261013，仅必要 UI 网络服务/bridge/QML/翻译/CMake/验收；覆盖这些接点旧冻结，70旧树/脏文件保护。保持引擎/运行库/用户配置及33ms/250ms刷新，不派Agent。产物全E盘，无main合并/push/Release/关机。


> **2026-10-13 2.0.7正式发布授权**：用户明确直接采用已验2.0.7-test-r2两个包发布GitHub、六群原图替换、README/Release加知识库、完整说明2.0.6以来变化。按docs/RELEASE_2.0.7_PLAN_2026-10-13.md，只改发布文档/图片和外部文件名，ZIP/EXE/运行组件逐字节保持。允许本地main合入、普通push nrvideo/main与两个标签、主v2.0.7两资产及独立非latest源码归档；保留原赞助/Discord/Ko-fi/width220。69旧树/509脏文件、旧包和旧Release保护，全产物E盘；无子Agent/代发/关机。不伪称包内test标识被去掉，不把混合模型取舍或合成误差包装成普遍画质/提速，也不宣称NV显存/BMD/HDR现场已根治。

> **2026-10-12 用户确认 Claude 内容节奏修复实机通过并授权合并重打 2.0.7**：从 main05bc4b5 隔离合入 claude/cadence-exact-20261012（60a93f3、8d11cad），按 docs/TEST_2.0.7_REFRESH_2026-10-12.md 重新构建、定向回归、本地 main 合并与 NVIDIA/AMD 完整测试包；显示2.0.7-test-r2。保护68旧树/509脏文件及已交付原包，全部研发产物在E盘新任务目录。不改用户正在运行的播放器/配置、UI频率/默认GPU优先级/运行库/模型，无新push/Release/关机或子Agent授权。

> **2026-10-13/14 极限低延迟（Claude）**：用户要求把显示同步“自动”恢复成游戏同款（不撕裂、不等垂直同步）、“延迟必须要做到极限的低！但不要允许撕裂”，随后“全部做掉！”授权 docs/LATENCY_MIN_PLAN_2026-10-13.md 的 B1–C3。存档 `checkpoint/pre-claude-latency-min-20261013`（main 7571919），分支 `claude/latency-min-20261013`，产物 `{用途}/claude-latency-min-20261013`。已完成：A1/A2 “自动”=`Present(0,0)`+诊断（c1dd0a2）；B3 只有显式垂直同步节流，启用时重置可等待对象、读帧前取容量，自动/撕裂永不等待，“低延迟队列”=一帧在处理+Reflex（ada49ff）；B1 XeSS 提交即送显、XeLL 周期对齐（3c2d6ca）；C2 实测全屏且控制条隐藏少一个刷新；C3 采集拷贝研究，多线程无效、零拷贝不实施（8182472）。10-14 用户改动：B2 不再自动降倍数/旁路，改为状态栏与“倍率”下方提示“当前帧率超出屏幕刷新率，建议降至…”；C1 改为“显示”里的“可变刷新”开关（屏幕不支持时灰，开且可用时按 G-SYNC/FreeSync + V-SYNC 呈现，不用撕裂标志）；修正垂直同步送显→上屏测量（统计落后时旧帧被算成约 19ms）。T911 10-14 上午恢复，B1/B3 实卡已验证；本机 320Hz 屏驱动报告不支持 VRR，真实 VRR 与 PBP 60Hz 光学复测待用户；测试包 tests/claude-latency-min-20261013/Veyra-2.0.7-test-latency-r3-NVIDIA。不改运行库/模型/驱动/用户配置；无 merge/push/Release/子 Agent；测试≤300秒、构建≤900秒。

> **2026-10-12 内容节奏修正（Claude）**：用户实测 2.0.7：“采集 60→30”被植入内容节奏判断、原始帧率跳动，要求还原为最简单的 60 帧 AABBCCDD→ABCD 稳定隔帧；新“自动识别内容节奏”去重不稳定（VFG 耗时、GPU 占用忽高忽低），游戏运行中出现 6fps，怀疑不是百分百相同的帧也被去掉。追加授权关闭用户正在运行的 Veyra、直接用 T911 实卡（2K60 NV12、游戏 30 帧）测试。存档 `checkpoint/pre-claude-cadence-exact-20261012`（main 05bc4b5）与 `archives/claude-cadence-exact-20261012`，分支 `claude/cadence-exact-20261012`。60→30 恢复 `CaptureHalfRate`（与 85cbd5a 之前相同、不经去重）；内容模式整画面逐像素与上一张新画面比较，每个分量都在 ±3 级（8 位刻度）以内才算重复（`PictureCompare.h`，采集回调只在去重开启时比较驱动样本并给帧编画面号）；逐字节相同在实卡上从不成立（T911 重复帧 ±1–2 级噪声），该版本已被实卡数据否决。见 docs/CONTENT_CADENCE_EXACT_2026-10-12.md。不改默认设置、NR/SR/FG 模型、运行库；用户设置只读取并复制进测试包；无 merge/push/Release/子 Agent。

> **2026-10-12 构建前采集格式追加授权**：用户要求分辨率先行、MJPEG 标红（不建议使用）、无效组合明确弹错且不自动路由其他格式。允许最小 UI/桥接/采集协商修复和有关回归，保留有效上次配置；与本轮整合一起本地合入 main 和 2.0.7 测试包。

> **2026-10-12 各分支整合与 2.0.7 测试包授权**：用户明确要求将 Claude 最新已验 XeSS 和各分支已完成修复合入 main 并构建 2.0.7 测试包。按 docs/TEST_2.0.7_PLAN_2026-10-12.md 从 main a167831 隔离整合 Claude e46bbdf 与 Codex d883216（含34f479e），必要冲突适配后构建、组合回归、本地 main 合并及 NVIDIA/AMD 完整便携测试包。66旧树/509脏文件及来源 tip 存档保护，不派 Agent，不改驱动/运行库/模型/真实配置；UI33ms/250ms、GPU默认优先级与已撤回负优化保持。产物全E盘，测试每进程≤300秒、构建≤900秒。此范围覆盖历史 no-merge/no-package；没有新 push/Release/上传/关机授权。

> **2026-10-12 专业模式画面遮挡播放条**：用户反馈原生视频位置错误并要求追溯来源。按docs/PRO_VIDEO_GEOMETRY_PLAN_2026-10-12.md从已验34f479e隔离codex/pro-video-geometry-20261012，仅统一Main/ProPage响应式尺寸、加入实际HWND回归；不改引擎、33ms/250ms节奏、GPU优先级、运行库或真实配置，不派Agent。65旧树/509脏文件由本轮start/bundle保护；产物全E盘，每测试≤300秒。无main合并/push/打包/Release/关机。

> **2026-10-12 日志50补帧修复授权**：用户在完成 RTX5070Ti/Elgato 日志诊断后明确“尝试修复”。按 docs/FG_CAPTURE_RECOVERY_PLAN_2026-10-12.md 从 main a167831 在 codex/fg-capture-recovery-20261012 隔离修复异常短 PTS 污染节奏基线及有证据的必要接点，覆盖这些具体范围旧冻结。独立 start/bundle 保护64旧树/515脏文件；已有交换链复用先验证，不重复改写。保持真实 PTS、VRR、真正断点重置、容量上限、UI33ms/250ms、GPU默认优先级与用户配置，不派Agent、不改驱动/运行库/模型。全产物E盘，测试每进程≤300秒/构建≤900秒；无main合并/push/打包/Release/关机。本机回放不能冒充Elgato实卡或物理屏幕性能验收。

> **2026-10-12 PR34 合并与采集设置授权**：用户明确合并 PR34、补齐当前卡全部设备格式及采集页自动保存/继续上次/启动自动恢复。按 docs/PR34_CAPTURE_SETTINGS_PLAN_2026-10-12.md 从 main76f41b3 在 codex/pr34-capture-settings-20261012 隔离适配必要 source/bridge/QML/preferences/测试接点，覆盖这些范围旧冻结。构建及定向验收后允许 main 合并与普通推送以完成 GitHub PR 合并。62 旧树/509 脏文件保持，main仅按授权更新；其它尚未合并分支不混入。保持 UI33ms/250ms、默认优先级与正常采集缓冲，不派Agent、不改驱动/运行库/真实配置。全产物E盘，测试≤300秒/构建≤900秒，无打包/Release/关机。

> **2026-10-12 边缘神盾开关与 AMD 光流（Claude，同一分支续做）**：用户问精修是否只对 NV 光流生效，要求“如果不可能就把AMD光流也加上这个优化，并且把这个设置成一个独立的开关，在选择补帧光流的下面，你给取一个屌炸天的名字”。开关「边缘神盾」（`EnhancementSettings::fgMotionRefine`，桥接 `edgeAegis`，默认开）放在列表/节点页“光流算法”下面，作为全局控件存进界面偏好 `edgeAegis`（不改会话/预设格式），只在 XeSS+运动来源“光流”时生效并重建图。AMD FidelityFX 光流接入同一精修，无后向光流时用新 `FlowClaim.hlsl` 认领检查判定露出背景；NVIDIA 路径逐位不变。合成测试 AMD XeSS 轮廓带误差 −4%~−29%，成本约 +0.6 ms；AMD 实卡未验。无 merge/push/Release、无子 Agent；详见 docs/XESS_FLOW_REVIEW_2026-10-12.md 第 9 节。

> **2026-10-12 XeSS 光流修复复审（Claude）**：用户“codex完成了后续的修复工作，但实际效果一般，你可以审一下，看看还有没有可以优化的地方，有没有可能是默认光流是1080p的问题？当然codex还在处理其他的任务，你不要打断他，你们不要互相影响到”。只读 Codex 分支/构建/回执；存档 `checkpoint/pre-claude-xess-tune-20261012`（f0fafdf）与 E 盘 `archives/claude-xess-tune-20261012`，分支 `claude/xess-flow-tune-20261012`，产物统一 `{用途}/claude-xess-tune-20261012`。屏幕/GUI/GPU 计时前检查无其他 Veyra 进程，Codex 测试期间等待。结论：Codex 接线无缺陷；1080p 默认光流不是原因（全分辨率几乎无改善且更贵）；新增 NVOF 后向光流的前后向一致性只用于判定/填补人物移开露出的背景（候选选择保持 Codex 版），仅 XeSS+光流+NVIDIA NVOF，NR/SR 光流逐字节不变；`VEYRA_TEST_FLOW_CONSISTENCY=0` 恢复 Codex 版。一致性参与选择的负收益方案已撤回。四肢与栏杆残差未解决。不改自动模式/其他补帧/运行库/模型/用户配置；无 merge/push/Release、无子 Agent。详见 docs/XESS_FLOW_REVIEW_2026-10-12.md。

> **2026-10-12 Claude 计划漏项补齐**：用户追问 FSR bug 和 Claude 其他工作完成情况。重新核对其最后获准的 JELLY_FIX_PLAN，FSR 单独开启补帧时初始化光流本来在接手范围内，初次接手排除它是 Agent 漏项。保留 f468dfb 和初次验收存档，在同一本地隔离分支按 docs/FSR_FLOW_FOLLOWUP_2026-10-12.md 补齐 FSR 光流消费者条件及真实 GPU/GUI 回归；不把 XeSS 精修扩到 FSR，不改自动选择规则、其他算法/UI节奏/默认优先级/运行组件/真实配置。新产物 E:/项目/Veyra/{用途}/fsr-flow-followup-20261012，初次所有回执和旧树基线保持。无子Agent、main合并、push、发布、关机。已证旧代码 NR/SR/standalone 全关时NVOF=0且合同失败，修复后显式光流/Automatic各9次、Zero0次，八组回归、真实FSR GUI及双入口通过；同时补齐密集竖条横移11/11中间帧A/B，无本组负收益。代码/本机回归完成，真实游戏观感待用户，栏杆模型问题/延迟仍暂缓。

> **2026-10-12 XeSS 修复接手**：用户要求接手 Claude 额度耗尽的 XeSS 修复。按 docs/XESS_HANDOFF_PLAN_2026-10-12.md 在 codex/xess-handoff-20261012 从 7fe3ea6 继承独立封存的五文件未完成修改，只完成 XeSS 开启光流时的运动边界精修及必要生命周期/构建/真实画质和耗时验证。61 旧工作树/509 脏文件由 archives/xess-handoff-20261012/start.json 保护，Claude 原树不改；此具体范围覆盖历史冻结。不派 Agent、不修改运行库/模型/驱动/真实配置；33ms 统计和默认 GPU 优先级保持。全部产物 E 盘、每测试≤300秒/构建≤900秒；无 main 合并/push/Release/关机。未获益的精修撤回，合成画面结果不冒充真实游戏或其他硬件通过。

> **2026-10-12 补帧人物边缘果冻 J0（Claude）**：用户要求先排查果冻（延迟暂缓），按 reports/jelly-latency-20261012/方案选项.md 先做 J0 判定，先测 DLSS/FSR，随后按用户“主要是XESS”补测 XeSS（SDK 只显示插值帧加桌面复制抓屏）。存档 `checkpoint/pre-claude-jelly-20261012`（main 76f41b3）与 E 盘 `archives/claude-jelly-20261012`；分支 `claude/jelly-20261012` 新增诊断测试 `veyra_fg_motion_boundary_tests`、`veyra_fg_xess_boundary_tests` 及文档；产品代码只加默认不生效的测试开关（XeSS 只显示插值帧、fgMotionProbe 进 XeSS 链路、NVOF 网格），不改默认行为、运行库/模型/驱动/用户配置；无 merge/push/Release。结论见 docs/JELLY_J0_2026-10-12.md。

> **2026-10-11 当前修复已合入本地main**：Codex4557ab6（含e8300d9）与Claude1d4c33f经隔离整合、42依赖闭包/13EXE构建、完整CMake、16组回归、12真实GUI状态/6布局及SR+VFG组合持久化/重启通过；实测产品4904b73、记录tip adcf509通过功能merge d2e1396进入main，完整Git树相同。VFG默认中档、DLSS K、33ms统计/250ms状态及负优化撤回保持。59开工树中唯main按授权更新，其余58树/504脏文件、来源分支及原包1626文件保持；final.json见reports/merge-current-fixes-20261011。首次组合GUI夹具漏FG节点失败，修夹具后通过，原证据保留；没有新的性能A/B或全硬件/长期显存通过声明。无push/打包/Release/关机。

> **2026-10-11 当前修复本地合并授权**：用户明确要求将 Codex 与 Claude 当前修复合入 main。按 docs/MERGE_CURRENT_FIXES_PLAN_2026-10-11.md，在 codex/merge-current-fixes-20261011 整合已验4557ab6（含e8300d9）与Claude1d4c33f，必要冲突适配后构建/定向回归，再本地main合并；覆盖这些接点历史no-merge。59旧工作树/504脏文件及来源tip由独立start/bundle保护，main仅按本次授权更新。默认档位、33ms统计/250ms状态节奏保持，不派Agent，不改运行库/模型/驱动/真实配置。产物全E盘，每测试≤300秒、构建≤900秒；不push/Release/打包/关机，不恢复已撤回负优化或旧未完成实验。

> **2026-10-11 内容节奏/显示限速状态授权**：用户要求纠正去重、垂直同步等正常运行误报“输出未达标”，状态上方显示选项与原因。按 docs/CADENCE_STATUS_PLAN_2026-10-11.md，从已验e8300d9在codex/cadence-status-20261011隔离实施共享状态判定/bridge/QML/翻译及定向测试；覆盖这些UI接点旧冻结。58旧树/504脏文件与运行库原件保护；不派Agent，不改采集/去重/补帧/NR/SR/呈现算法、不加Timer或提高UI刷新率/GPU优先级，不改用户配置。产物全E盘，测试≤300秒/构建≤900秒；无main合并/push/打包/Release/关机。

> **2026-10-11 DLSS 4.5 SR 接手授权**：用户要求接手 Claude 最新未完成的超分接入并启动软件供测试。按 docs/DLSS45_SR_PLAN_2026-10-11.md，在 codex/dlss45-sr-handoff-20261011 从049902b继承已封存的9文件patch，完成 K/M/L 模型选择的必要 settings/NGX/graph、预设/session、bridge/QML/翻译及定向构建验收接点；此范围覆盖历史冻结。原57工作树/504脏文件由独立start保护，Claude原树不改；不派Agent、不改运行库/模型/驱动/用户配置，不改变33ms UI统计/GPU优先级，不顺手合入VFG mixed。全产物E盘，每测试≤300秒、构建≤900秒；本轮不main合并/push/Release，画质由用户验收，不把Create成功当画质提升。

> **2026-10-11 VFG 混合档与 GPU 镜头切换判断（Claude）**：用户看完调研 reports/vfg-deep-opt-20261011/方案选项.md 后要求“先做1+2吧，创建存档开分支，然后直接做好，混合档位要做三档 一个高+中 一个高+低 一个中+低 适配各种机型”。存档 `checkpoint/pre-claude-vfg-mixed-20261011`（main 049902b）与 E 盘 `archives/claude-vfg-mixed-20261011`（bundle 已 verify，start.json 记录 55 工作树/495 脏文件）。分支 `claude/vfg-mixed-20261011` 只改 VfgBackend（第二个 NvVFX 实例、每对 A/B 的 GPU 镜头切换判断 shaders/VfgShotChange.hlsl + predicated 拷贝、关闭 SDK 自带检测；`VEYRA_TEST_VFG_AUTO_SHOT_CHANGE=1` 恢复旧行为）、图中 capture 的一个参数、vfgQuality 取值 0–5（bridge/两页 QML/翻译/状态提示）、CMake 着色器登记与 VFG 测试。默认“中”不变；DLSS/XeSS/FSR、NR/SR、运行库/模型/驱动/用户配置不改。旧版打开含混合档的预设库会拒绝并保留原文件。无 merge/push/Release。详见 docs/VFG_MIXED_TIERS_2026-10-11.md。

> **2026-10-11 Claude修复合并完成**：已验整合tip `9c14ee2` 通过本地main功能merge `3a08ce8` 合入，完整Git树相同，随后只补收尾文档。六Claude分支、UI29e33df和退出回调竞态修复在main；26项回归、滚动clip0、卡死诊断/恢复/有界退出及普通class2 NR性能核对通过。软件CPU首次+15.09%触发守卫，倒序复测及双顺序平均+6.52%通过同一阈值，完整失败/复测数据保留，不宣传普遍性能收益。保持33ms统计、负优化回退和未实卡边界；54旧工作树/496脏文件、七来源tip及原包1626文件保持，唯main按授权更新。没有push/新包/Release/关机；最终回执reports/merge-claude-fixes-20261011/final.json。

> **2026-10-11 Claude 修复本地整合**：用户明确要求将目前 Claude 分支修复合入 main。按 docs/MERGE_CLAUDE_FIXES_PLAN_2026-10-11.md，在 codex/merge-claude-fixes-20261011 从 main5ba186a 整合六个已提交的 Claude 修复分支及已验 UI29e33df；保留分支内负优化回退，只做必要冲突/生命周期适配、构建与定向回归，覆盖这些接点的历史冻结及 no-merge。通过后允许本地 main 合并，不 push/Release/打包/关机。独立 start/bundle 保护54旧工作树/496脏文件，来源分支不改；不派Agent、不改运行库/驱动/模型/真实配置，保持33ms统计和普通GPU优先级。所有产物E盘；每测试≤300秒、构建≤900秒，现场未验边界如实记录，不把各分支历史通过冒充合并验证。

> **2026-10-10 VFG 优化（Claude，任务一）**：用户“开始进行优化吧，全部搞定掉”“开始之前先创建存档，然后开分支弄，如果是结果是性能反而倒退，直接退回”。存档 `checkpoint/pre-claude-perf-20261010`（main 5ba186a）与 E 盘 `archives/claude-perf-20261010`（bundle 已 verify、46 工作树/495 脏文件 start.json）；在 `claude/vfg-20261010` 只改 VFG 后端/图内 VFG 分支/实时 VFG 准入/状态提示，软件链路在独立分支，二者不互相依赖。同 EXE 测试开关 A/B，负优化当场退回并留证：V1 保留（4X 201→215 fps），V2、V3 退回，V5 状态提示保留。不改 DLSS/XeSS/FSR、运行库/模型/驱动/用户配置；无 merge/push/Release，测试≤300 秒、构建≤900 秒。详见 docs/VFG_TASK_2026-10-10.md。
> **2026-10-10 软件链路优化（Claude，任务二）**：用户“开始进行优化吧，全部搞定掉”“开始之前先创建存档，然后开分支弄，如果是结果是性能反而倒退，直接退回”。存档 `checkpoint/pre-claude-perf-20261010`（main 5ba186a）与 E 盘 `archives/claude-perf-20261010`；在 `claude/pipeline-20261010` 只改采集→效果衔接→送显/上屏及其诊断与提示，VFG 在独立分支。同 EXE 测试开关 A/B 并用 PresentMon 对齐上屏：C1 上屏时间日志与 C2 提示保留，C3 配对节奏样本保留，C7 交换链等待负优化已退回，C4/C5/C6 经实测无可得收益未实施。不改 NR/SR/FG 模型与运行库/驱动/用户配置；无 merge/push/Release，测试≤300 秒、构建≤900 秒。详见 docs/PIPELINE_TASK_2026-10-10.md。
> **2026-10-10 采集中卡死（Claude，logs(15)）**：用户要求排查修复“玩着玩着卡死”，并按“NVIDIA 信息浮窗开启后 2.0 以后卡死”的线索参照小飞机适配处理，游戏加加一起适配。存档 `checkpoint/pre-claude-freeze-20261010`（main 5ba186a）与 E 盘 `archives/claude-freeze-20261010`；分支 `claude/freeze-fix-20261010` 只改监控/叠加软件兼容（启动检测 NVIDIA Overlay.exe / NVIDIA Share.exe / GamePP*.exe → 软件界面，运行中出现提示重启）、引擎卡死监视（[engine-hang] 栈、veyra-hang-*.dmp、提示）与有界退出。nvspcap64.dll 浮窗关闭时也会加载，不能作为开启信号；GPU 优先级“高”同样卡死，默认值不改。本机未复现，现场效果待用户确认。无 merge/push/Release。详见 docs/FREEZE_FIX_2026-10-10.md。
> **2026-10-11 补帧内容节奏重设计（Claude）**：用户要求重新设计补帧内容节奏里的去除重复帧（30/40 帧游戏 60 帧采集、非整数倍率，不误删真实帧、不加明显耗时），用 GTA6 30→60/58 渲染测试，完成后汇报并关机。存档 `checkpoint/pre-claude-dedup-20261011`（main 5ba186a）与 E 盘 `archives/claude-dedup-20261011`；分支 `claude/dedup-cadence-20261011` 只改直播来源的重复画面判断、留帧和内容时间戳（ContentDedup.h、EngineController 读帧处、EnhanceGraph::lumaSignature、回放测试与单测）及“内容节奏”提示文案。默认模式、NR/SR/FG、运行库、驱动、用户配置不变；硬解串流没有 CPU 画面，只保留 60→30 按时间戳减半。无 merge/push/Release。详见 docs/CONTENT_CADENCE_2026-10-11.md。
> **2026-10-11 文件播放间歇卡顿（logs16，Claude）**：用户提交 logs(16)（播放视频开补帧间歇卡顿）。存档 `checkpoint/pre-claude-logs16-20261011`（main 5ba186a）与 E 盘 `archives/claude-logs16-20261011`；分支 `claude/audio-reader-20261011` 只改文件音频读包（AudioPipeline 预读线程、拖动/起播/恢复时同步读）、audio-continuity 诊断与音频测试。默认设置、补帧/NR/呈现、运行库、驱动、用户配置不变；`VEYRA_TEST_AUDIO_DIRECT_READ=1` 为旧读法。无 merge/push/Release。详见 docs/AUDIO_READ_AHEAD_2026-10-11.md。
> **2026-10-11 Elgato HDR/RGB 对齐 Nitlink 与 OBS（Claude）**：用户反馈 1.x 接入 Nitlink 修好的 Elgato HDR 红色溢出又出现、RGB 黑屏。核查确认 2.0 保留了 be3e2e6，但它只覆盖 4K60 Pro MK.2 + P010。分支 `claude/elgato-hdr-20261011`（存档同 logs16 任务）只改 Elgato 厂商控制（全部 Elgato 名称、8 位格式打开卡内色调映射、关闭时恢复）、非 MK.2 P010 的全范围色度开关（`fullRangeChroma` / YuvToLinearRgb 模式 2）及测试。其它采集卡、默认设置、运行库、驱动、用户配置不变；无 Elgato 实卡，现场需反馈者日志确认。无 merge/push/Release。详见 docs/ELGATO_HDR_PARITY_2026-10-11.md。
> **2026-10-11 软件 UI 残影接手**：用户要求读取刚才 Claude 的 UI 修复聊天并接手，明确不得恢复 2.0.4 高频 UI 绘制导致的性能回退，随后补充超分图标滚动越界。按 docs/UI_SOFTWARE_DAMAGE_PLAN_2026-10-11.md，在 codex/ui-smear-handoff-20261011 从 main 5ba186a 隔离继续必要 QML 图标/虚线边框与宿主日志修复、定向构建测试，覆盖这些具体 UI 接点旧冻结。独立 start 保全 53 既有工作树/496 未提交文件与 Claude 未完成补丁；旧 start/guard 不改，不派 Agent，不动其它 Claude 修复、GPU 优先级、视频/NR/补帧算法、运行库原件/驱动/真实配置。最终使用软件路径缓存 SVG Image，撤回整窗重绘、QuickPrivate 与软件 Shape layer 原型，保持 33ms 有界统计，不增加持续绘制。产物全 E 盘，测试每进程≤300秒、构建≤900秒；无新打包/main合并/push/Release/关机授权。

> **2026-10-10 已验修复合入本地main**：用户在完整测试包交付后明确“合并到main吧”。已将codex/sr-visual-quality-20261010的48d8f34及其11个未合入提交通过--no-ff整合，功能merge为ff80a49，Git树与实测分支完全相同；随后仅更新合并记录/当前状态。本地main包含PGS字幕、播放列表/PR31、BMD原生开关、设置恢复/输入焦点/布局/关闭项编辑等已验修复，SR画质实验保持撤回。范围与验收见docs/MERGE_MAIN_2026-10-10.md；46工作树/495脏文件存档到E盘archives/merge-fixes-20261010/start.json，除本轮main外均保持。新构建明确配置DeckLink SDK，打包使用已验zlib/PS5 slices256版FFmpeg，不无意退回旧依赖。此次只本地合并和文档收尾，不push/Release/换版本/重打包/关机，不改运行库原件/用户配置，不删旧分支或工作树；历史“尚未合并”仅代表当时。

> **2026-10-10 超分实验撤回与本地测试包**：用户要求停止超分继续优化、去掉负优化并汇报，随后明确“弄好了打个测试包给我”。本轮所有SR生产实验（局部/整帧静态运动校验、额外纹理/pass、DLSS档位及FSR曝光实验）全部撤回到c664a67，保留此前已验功能修复和共同参考测试工具。本地制作2.0.6-fixes-20261010完整NVIDIA测试包与对应源码，逐文件核对既有批准runtime、patched FFmpeg的zlib/PS5 slices256、许可证、私有配置冷启动与ZIP；覆盖下条“不打测试包”。不新发正式版、不merge/push/Release/关机，不派Agent，不动45既有工作树/495脏文件/main或真实配置。不得把重复帧误差改善说成一般视频画质提升。

> **2026-10-10 超分画质需求纠正**：用户明确RTX Video SR/DLSS/FSR均提升不明显，要求改善已经运行时的实际画质。按 docs/SR_VISUAL_QUALITY_PLAN_2026-10-10.md 从c664a67隔离codex/sr-visual-quality-20261010，必要SR后端/共享采样及前后链路/验证接点覆盖旧冻结。45既有工作树/495脏文件/main独立start保护；全产物E盘，不派Agent、不改运行库/模型/驱动/真实配置，不打测试包/merge/push/Release/关机。像素变化只能证明执行，不能当画质提升；先同源参考和实际视觉检查，修有证据的问题，不强加滤镜/隐藏延迟或编造收益。

> **2026-10-10 关闭效果器编辑卡顿修复**：用户反馈仅编辑未开启效果器也卡顿，按 docs/INACTIVE_EFFECTS_PLAN_2026-10-10.md 从30c6f60隔离codex/inactive-effects-20261010，允许必要设置比较/共享引擎更新路径/定向测试接点，覆盖这些接点旧冻结。QML重启回归发现的全局抗闪烁保存缺口仅补已有preferences读写，不改会话格式。独立start保护44既有工作树/495脏文件及main；全产物E盘，不派Agent、不改运行库/驱动/模型/真实配置、不打测试包、不合并/push/Release。每测试≤300秒，保存关闭参数、开启时实际应用，验证共享光流及多NR实例，不能将软件连续提交冒充屏幕端到端延迟；此前关机任务已完成，本次没有新关机指令。

> **2026-10-10 四项现场修复授权**：用户提供日志46及手动数字输入后快捷键失效截图，要求修复设置切换致软件错误、数字输入焦点、优化超分和多语言/中文布局溢出，完成后汇报并关机。在 codex/settings-sr-ui-20261010 从 af2edf4 隔离实施必要 engine/presenter/SR/共享QML组件/翻译与构建定向验收接点，覆盖这些必要接点的旧冻结。独立不可变start保护43既有工作树/495脏文件与main；产物全E盘，保留已验DeckLink/字幕/播放列表改动。不派Agent，不改驱动、运行库/模型原件或用户配置，不打测试包、不merge/push/Release。测试每进程≤300秒，构建≤900秒；实测与现场未复现的边界分别报告。源码/报告/证据保存后按本次用户授权安排关机，不强制丢弃未保存内容。

> **2026-10-09 BMD 原生修复授权**：用户在原生信号核查后明确“修”，随后要求“做成和美乐威一样的开关”。按 docs/DECKLINK_NATIVE_PLAN_2026-10-09.md，在codex/decklink-native-20261009实现DeckLink COM输入、共享采集/模式恢复、必要10位GPU解包/颜色合同及采集设置开关（美乐威旁、默认关闭、保存选择、重连生效），覆盖这些必要接点的旧冻结。42旧工作树/495脏文件及main0e7018ad按独立start保护。SDK原件/生成头文件留E盘，正式构建须明确传VEYRA_DECKLINK_SDK_DIR，不进Git，不改驱动/运行库/模型/用户配置；无子Agent/测试包/合并/push/Release/关机。没有BMD实卡不得宣称现场已经恢复或延迟收益。实施证据见同轮REPORT。

> **2026-10-09 BMD 信号核查**：用户提供 veyra-qml(45).log，要求具体核对 madVR/Blackmagic 官方软件并解释持续黑屏。本轮按 docs/BLACKMAGIC_SIGNAL_RESEARCH_PLAN_2026-10-09.md，在 codex/blackmagic-signal-research-20261009 基于973fc9b隔离记录，只读核对日志、产品源码、固定上游与官方文档；没有新的产品修复/实卡通过声明。独立start保护41既有工作树/495脏文件，产物全E盘；保留“不用打测试包”，不改用户配置/驱动/运行库，不派Agent，不合并/推送/发布/关机。原生DeckLink能力缺口与本例尚未确认的物理原因必须分开。结论见同轮REPORT。

> **2026-10-09 PR31 与播放列表授权**：用户要求处理 PR31 / issue21，播放列表采用文件夹导入、极简底栏按钮、可移动缩放浮窗、拖动排序/单项移除/名称排序/追加拖入/正在播放标记。按 docs/PR31_PLAYLIST_PLAN_2026-10-09.md 在 codex/pr31-playlist-20261009 隔离接入 PR31 的采集音频输出切换修复及必要 UI 模型/bridge/QML/构建/翻译/定向验收；这些具体接点覆盖旧冻结。基于已验字幕分支27cf4b4；独立 start/bundle 保护40既有工作树/495脏文件。产物全E盘、不制作测试包、不派Agent、不改驱动/用户配置/运行库原件。只做本次局部修改，不推进其它增强算法；本轮不发布、关机。每项测试≤300秒，构建≤900秒，当前主线及其它工作树不动。

> **2026-10-09 字幕格式修复授权**：用户要求修复PGS/SUP不显示并检查其它字幕格式，确认多位其他用户的内嵌PGS轨道显示不可用。按docs/SUBTITLE_FORMATS_PLAN_2026-10-09.md从main 0e7018ad在E盘codex/subtitle-formats-20261009隔离排查/修复共享字幕解码/加载/字幕层和必要bridge/菜单/翻译及定向验收，这些具体接点覆盖历史冻结。独立start/bundle保护39既有工作树/495脏文件；main与其它工作树保持，不改旧guard。所有产物E盘；原runtime/模型/SDK字节不变、不进Git；允许针对已复现的Matroska压缩PGS缺口，在E盘另行构建同版本patched FFmpeg，仅启用现有静态zlib、保留PS5 slices256/dav1d/原解码能力并记录对应源码/许可证/实际配置，不覆盖原件。此为字幕修复必要接点，不改proprietary运行库。不派Agent/造压，不改用户配置/驱动，无新merge/push/Release/版本更新/关机授权。现场视频未提供，不以公开样本冒充反馈者全部通过。

> **2026-10-09 HDR 补帧开关与 main 整合授权**：用户先要求将目前修复合入 main，再明确 HDR 补帧兼容必须改为设置页开关、默认关闭。按 docs/HDR_FG_COMPAT_TOGGLE_PLAN_2026-10-09.md 在当前 FG 隔离工作树修改必要呈现设置/bridge/QML/翻译及定向验收，覆盖这些接点的历史冻结。通过后允许本地 commit/checkpoint/main 合并 BMD、HDR 开关和对应诊断脚本/记录；不 push/Release/更新版本/关机，不派 Agent、不造压、不改运行库/模型/驱动/用户配置。39 工作树、495 脏文件按 archives/hdr-fg-toggle-20261009/start.json 保护，旧 guard/start 不改，产物全 E 盘；测试≤300秒、构建≤900秒。HDR 现场根因、5090 复测及 BMD 实卡/延迟仍未确认，不把兼容候选或开关测试冒充根治。

> **2026-10-09 补帧闪黑现场续修**：用户提交诊断ZIP与日志44，反馈诊断不闪、原版闪，授权排查修复并收尾关机。按docs/FG_FLASH_FIX_PLAN_2026-10-09.md，从main dbfceab在E盘codex/fg-flash-fix-20261009隔离实现NVIDIA HDR应用FG最终合成GPU复制候选与定向测试/本地可回退包；这些具体presenter/ABI重编译接点覆盖历史冻结。诊断增加GPU拷贝、改名EXE/私有配置/关QML缓存，不能当根因证明；本机5070未复现5090闪黑，候选现场效果未验，禁止宣称根治。先执行fg-flash-fix-control.py，独立不可变pre-document-state保护37工作树/495脏文件，不改旧guard。不派Agent/竞争压力，不改驱动、DLL/模型、用户配置或其它工作树；无新main合并/push/Release。正常链路不加像素回读/CPU逐帧等待。测试≤300秒、构建≤900秒、产物全E盘；源码、候选、回执保存核对后按用户要求关机，不强制丢弃未保存内容。

> **2026-10-09 BMD 黑屏跟进**：用户提供 veyra-qml(43).log，要求继续修复 2.0.6 未解决的 BMD 黑屏，确认 OBS 两种来源都可出画。按 docs/BLACKMAGIC_FOLLOWUP_PLAN_2026-10-09.md 从 main dbfceab 在 E 盘 codex/blackmagic-followup-20261009 隔离修复必要 DirectShow 接收协商及定向测试/本地候选；这些具体接点覆盖旧冻结。独立 start/bundle 保护38原工作树和495脏文件；旧 guard/start 不改。普通采集、音频、NR/补帧/显示算法、运行库/模型原字节保持；不派 Agent、不改驱动/用户配置/其它工作树。测试≤300秒、构建≤900秒，产物全E盘。无新合并/push/Release/关机授权；BMD 实卡未验不得声称已修好。

> **2026-10-08 桌面项目清理追加授权**：用户要求“桌面这个也清一清”，仅处理桌面 Veyra 项目的忽略产物。按 docs/DESKTOP_STORAGE_CLEANUP_2026-10-08.md，历史 logs lossless 去重归档 E 盘后移除桌面副本，8 套旧 CMake 构建保留 EXE/配置/源码片段后清缓存，旧 1.2.0 解压副本与原 ZIP/额外日志归档核对后移除；35 工作树、495 脏文件及 66,680 个保留文件哈希通过。保留 out/remoteplay 当前依赖、out 内工作树、SDK/runtime/素材及 .git，不涉及桌面其它文件。不改历史 guard/start，不改产品、不 push/Release/关机；C 回收与 E 归档成本分别报告。

> **2026-10-08 本地合并与项目清理授权**：用户要求把当前修复合入 main，然后排查并清理项目磁盘占用。已验 VFG 71723e0 从 main 96a7c8de 快进合并；按 docs/PROJECT_STORAGE_CLEANUP_PLAN_2026-10-08.md 盘点并删除经清单核实的过时可重建构建、私有测试 app/缓存、冗余解压副本。此授权覆盖这些产物的历史保留限制，不改历史 guard/start 基线，不删除工作树、源码、未提交修改、依赖/SDK/运行库原件、用户素材或正式发布 ZIP/对应源码。独立 start/495 份脏文件备份在 E:/项目/Veyra/archives/project-cleanup-20261008；按硬链接身份及实际磁盘空间分别报告。保留当前可用构建、2.0.6 两显卡包与最新 VFG 候选、必要日志/验收记录。不派 Agent、不改产品实现或用户配置、不 push/Release/关机。

> **2026-10-07 VFG继续优化授权**：用户要求继续尝试VFG优化。按docs/VFG_NEXT_PLAN_2026-10-07.md从正式2.0.6后main 96a7c8de在E盘codex/vfg-next-20261007隔离实施VFG必要互操作/提交/专属排队预算及定向测试、本地候选；覆盖对应旧冻结。先运行vfg-next-control.py，独立start/bundle保护34工作树、495脏文件和3691项原件，不改旧guard。其它补帧/NR/时钟/显示策略不动，DLL/模型原字节；不派Agent/压力竞争、不改用户配置/驱动，不merge/push/Release/关机。测试≤300秒、构建≤900秒、产物全E盘。

> **2026-10-07 2.0.6正式发布授权**：用户明确构建2.0.6并发布GitHub，替换满员4群为所附5群二维码，发布后交付群公告。按docs/RELEASE_2.0.6_PLAN_2026-10-07.md从已验bf6e651在E盘codex/release-2.0.6-20261007整合VFG/Blackmagic/AMD NR-FSR修复，允许版本/双语README/新二维码/构建定向验收/同源码双显卡包及本轮commit/tag/main合并/普通push/v2.0.6正式Release与5资产上传。覆盖历史no-merge/no-push/no-Release；不扩改已验算法或运行组件，不改用户配置/驱动/其它工作树，不派Agent/竞争压力，不关机，群公告只交用户不代发。先运行release-2.0.6-control.py，独立start/bundle保护33工作树、495脏文件与3678项原件；仅main允许按合并回执推进，旧guard/baseline不改。测试≤300秒、构建≤900秒、产物全E盘。保留赞助QR/Discord/Ko-fi、双QR各width220，AMD/Blackmagic实卡与NV显存增长边界如实说明。

> **2026-10-07 AMD NR/FSR现场修复**：用户报告1080p文件与XSX1080p60开关AMD NR无画面变化，提供veyra-qml(37).log。按docs/AMD_NR_FSR_PLAN_2026-10-07.md从4294341在E盘codex/amd-nr-fsr-handoff-20261007隔离修复共享NR→FSR资源交接和必要定向测试/构建/本地候选；这些具体接点覆盖历史冻结。先运行amd-nr-fsr-control.py，独立start/bundle保护32个工作树、495个脏文件与3672项已发布/候选文件，不改旧guard。运行库/模型原字节、正常链路不回读；无子Agent/竞争压力、不改用户配置/驱动/其他工作树、无新main合并/push/Release/关机。每测试≤300秒、构建≤900秒，所有产物E盘。本机RTX共享交接验证不等于RX9070/HIP/XSX实卡验收。

> **2026-10-07 Blackmagic采集兼容授权**：用户要求兼容日志中的 Blackmagic WDM Capture / Decklink Video Capture，并说明不能确认物理卡数。从已验VFG候选cc22e88在E盘codex/blackmagic-capture-20261007按docs/BLACKMAGIC_CAPTURE_PLAN_2026-10-07.md隔离修复必要DirectShow上游连接/HDYC格式/驱动属性页/源黑帧诊断与QML及针对性构建验收。本次具体接点覆盖历史冻结；不改其它补帧/NR算法、不碰驱动/用户配置/其他工作树。独立start和Git bundle在archives/blackmagic-capture-20261007，先执行blackmagic-control.py，不改旧guard/baseline。无子Agent、压力/竞争负载；单测试≤300秒、构建≤900秒，产物统一E盘。运行库/模型原字节且不进Git；没有新merge/push/Release/关机授权。实卡未验不能宣称已解决。

> **2026-10-07 VFG专项优化授权**：用户明确“尝试优化VFG，其他的补帧暂时不动”。从已封存排查aec800e在E盘codex/vfg-optimization-20261007按docs/VFG_OPTIMIZATION_PLAN_2026-10-07.md实施VFG必要提交/互操作/专属调度及定向构建验收、本地候选；这些具体接点覆盖旧冻结。DLSS/XeSS/FSR实现与策略保持，不改NR算法、用户配置、驱动或其它工作树。独立不可变start及source-before.bundle在archives/vfg-optimization-20261007，先运行vfg-opt-control.py，不改旧guard/baseline。不派Agent、无压力/竞争负载，测试≤300秒、构建≤900秒，产物全在E盘；没有新merge/push/Release/关机授权，运行库/模型原字节保留且不进源码Git。

> **2026-10-07 2.0.5 正式发布授权**：用户明确“发布吧，2.0.5 老样子发完给我弄个群公告”。按docs/PUBLISH_2.0.5_PLAN_2026-10-07.md，仅将已测2.0.5做发布文档/元数据整理，允许本轮main合并、普通推送、v2.0.5标签、正式GitHub Release与5项资产上传；本条覆盖此前本地交付的no-push/no-Release限制。产品EXE/QML/shader/运行库/模型保持；NVIDIA显存持续增长未修复，留待下一版，不实施假说。群公告只交用户、不代发，不关机。独立start基线保护28既有工作树/495修改文件/53历史证据，先运行本轮publish-2.0.5-control.py，旧guard不修改。不派Agent、无压力/竞争负载、不改用户配置/驱动，所有产物E盘，保留双二维码width220、Discord和Ko-fi。

> **2026-10-07 2.0.5 本地整合授权**：用户明确要求先将目前完成的修复合入 main，构建 2.0.5 给用户测试，显存持续增长留待下一版本详细排查。按 `docs/RELEASE_2.0.5_PLAN_2026-10-07.md` 在 E盘 `codex/release-2.0.5-20261007` 整合已测 ef2069f/4c917aa 及其调查记录，仅修改版本、文档和必要构建/定向验收/打包脚本；不实施未证实的 NR Shutdown、驱动或显存假说修复。允许本地 commit/checkpoint/main 合并和 NVIDIA/AMD 同源码测试包，不新增 push/GitHub Release/上传/关机授权。独立不可变基线 `archives/release-2.0.5-20261007/start.json` 保护27个原工作树（main只按授权推进）、495个未提交文件及14份已发布文件；先执行本轮 control，不修改旧 guard/baseline。不派Agent、不做压力/竞争负载、不改DLL/模型/驱动/用户配置；测试每进程≤300秒，构建≤900秒，全部产物统一 E:/项目/Veyra。

> **2026-10-06 稳定性/导出/性能回归修复授权**：用户要求一起解决 AMD 补帧选择异常、补帧导出 DLSS 限制、NR 导出 1080p 限制、跨来源撕裂及 2.0.3→2.0.4 性能回退，确认使用当前电脑、NVIDIA 原版 NR、4K 视频输入。按 `docs/STABILITY_EXPORT_PRIORITY_PLAN_2026-10-06.md` 在 E盘 `codex/stability-export-priority-20261006` 从 main `578d63c` 隔离修复共享 graph/presenter/导出冻结与 codec/能力选择/QML及必要构建测试接点；本次具体范围覆盖历史冻结。先运行本轮 `scripts/acceptance/stability-export-priority-control.py`，独立开工基线不可修改；保留25个工作区及已发布包。不得派Agent、压力/竞争负载、修改DLL/模型/驱动/其他应用或用户配置。每个测试≤300秒，构建≤900秒，产物统一E盘。本轮交付本地可测修复包，未授权新的公开发布、贡献者消息或关机；AMD真实HIP推理、物理屏幕扫描与RTX软件测试分别报告。

> **2026-10-06 PR #19/#20 审查与适配授权**：用户反馈9000系NR已通过其测试，并要求检查项目PR19、PR20、判断能否合并并做好适配。按 `docs/PR19_PR20_INTEGRATION_PLAN_2026-10-06.md`，从刚交付的 `63ce396` 在 E盘 `codex/pr19-pr20-20261006` 隔离审查/适配HDR输出调优、预设/会话迁移、QML、呈现metadata生命周期、MSVC日志转换和RemotePlay条件编译及必要构建/定向测试。此具体范围覆盖历史冻结，不复活暂缓的PR13/14。验证后允许将可合并PR及已测现场修复整合main，普通推送完成GitHub PR合并状态；保留作者原始提交、其他工作树和原发布资产。不派Agent，不做压力/竞争负载、不改DLL/模型/驱动或用户配置；测试每进程≤300秒，构建≤900秒，产物统一E盘。先运行独立 `scripts/acceptance/pr19-pr20-control.py`，开工start基线禁止修改。本轮无新Release/资产上传/贡献者消息或评论/关机请求。用户RX9000反馈与本机RTX/HDR软件验证分别报告，不外推全部型号或物理HDR实测。

> **2026-10-06 OBS/导出/AMD NR 现场修复授权**：用户提交 logs(14).zip、veyra-qml(30).log 和两张截图，明确要求修复 OBS 兼容模式滚动拖影、视频导出失败及 AMD 开 NR 黑屏，并对照 lmxxf 与 A-ENTROPY 项目。仅在 E:/项目/Veyra/worktrees/obs-export-amdnr-20261006 / codex/obs-export-amdnr-20261006，从 main f8045fb 隔离实施必要启动/QML 绘制、导出冻结协议、共享 AMD NR/codec/互操作、构建和定向验收接点；按 docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md 执行。这些具体接点覆盖历史冻结，其他功能保持。不派 Agent、不做压力或竞争负载；测试每进程≤300秒，构建≤900秒。所有产物 E 盘，先运行 scripts/acceptance/obs-export-amdnr-control.py，独立 start 基线禁止修改。保留 main、其它工作树、用户文件/配置和已发布资产。允许本地修复及候选交付，不包含新公开发布/推送/合并或关机；不修改 proprietary DLL/模型/驱动，不用本机 RTX 测试冒充 RX9070 真推理通过。

> **2026-10-06 正式发布授权**：用户明确要求发布2.0.4到Likely7/Veyra-NRVideo，README替换2.0.3更新内容，其余主体保持；新增Discord `https://discord.gg/c9aREyMj8` 与Ko-fi `https://ko-fi.com/likely7`，保留原微信群及微信赞助二维码各width=220；Release详述优化、实测数据与限制，完成后提供1000字以内群公告并关机。本轮允许必要发布文档/打包/源码归档、main合并、推送、v2.0.4标签与Release资产上传和正式发布。按docs/PUBLISH_2.0.4_PLAN_2026-10-06.md执行；沿用已验收程序及运行组件原字节，不扩改引擎、不派子Agent、不做压力测试。发布核实后才能安排关机；公告只交付用户，不代发群。本条覆盖此前本地候选的“不发布/不关机”限制。

> **2026-10-06 列表预设纠偏**：用户明确不要独立补帧预设，要求图示光流/估算质量/内容节奏进入已有列表预设，并重新打包2.0.4。按docs/LIST_PRESET_FLOW_PLAN_2026-10-06.md在E盘codex/list-preset-flow-20261006实施必要PresetLibrary/bridge/QML/翻译/定向测试/本地main续修与打包；保留旧包/原工作区及用户配置，不替换运行库。新guard list-preset-flow-control.py使用独立不可变基线。此具体修复覆盖这些接点历史冻结；不推送/公开发布，不派Agent/压力测试/关机，沿用≤300秒测试与≤900秒构建。

> **2026-10-05 2.0.4 整合与测试包授权**：用户要求检查其他分支及 Claude 刚完成的修复，与 NR 强度/三风格画面调控、采集/Xbox 修复整合到本地 main，打包 2.0.4 给用户测试并准备正式发布。工作树 `E:/项目/Veyra/worktrees/release-2.0.4-20261005`，分支 `codex/release-2.0.4-20261005`；按 `docs/RELEASE_2.0.4_PLAN_2026-10-05.md` 执行。本轮允许存档、提交、标签、本地 main 合并、必要版本/打包/翻译/定向验收及发现的整合缺陷修复；仅合入已审查的产品改动，不复活历史撤回实验。Claude UI 工作树的 18 份未提交文件已逐字节封存并复制，原工作区/桌面/用户配置保持。新 guard 为 `scripts/acceptance/release-2.0.4-control.py`，不可变 start 与全引用 bundle 在本轮 E 盘 archives。继续 NVIDIA/AMD 同源码分包，保留 patched FFmpeg、libass、原组件身份；不派 Agent、不跑压力或竞争负载。测试每进程≤300秒、构建≤900秒。此次是本地测试交付及发布准备，未请求新的 push/GitHub Release/上传或关机；HDR/Dolby PR13/14仍暂缓。本条覆盖此次整合所需的旧 main/分支冻结，不扩大无关算法或运行库改动范围。

> **2026-10-05 当前采集/Xbox现场反馈**：用户提交 `logs(4).7z`，最后明确 `veyra-qml(28).log` 为“xbox问题”。按 `docs/CAPTURE_XBOX_FIELD_PLAN_2026-10-05.md` 在 E 盘 `capture-xbox-field-20261005` / `codex/capture-xbox-field-20261005` 从 `33d6685` 隔离排查修复。这次仅涉及 Xbox 视频错误/丢AU后的关键帧恢复、有界硬解重开与必要软件回退、采集时间线诊断标签、定向测试/文档/本地候选；覆盖这些接点的历史冻结，不改 NR/颜色/补帧算法或串流协议。每轮先运行 `py -3.11 -B scripts/acceptance/capture-xbox-control.py`，不可变基线独立于旧 guards；main、其它 18 个工作区、用户配置和已有包保留。无新 merge/push/Release、压力测试、关机或子 Agent 授权；所有产物 E 盘，真实 Xbox/AMD 效果未实测不得称通过。

> **2026-10-05 NR 强度与画面调控授权**：用户明确要求“自动调控做为开关，在NR里，也可以选择手动调节，然后保留原版的强度5，开始吧搞定他”。仅在 `E:/项目/Veyra/worktrees/nr-strength-protection-20261005` / `codex/nr-strength-protection-20261005` 从本地 main `de18fc4` 隔离实施。按 `docs/NR_STRENGTH_PROTECTION_PLAN_2026-10-05.md` 修改共享 NR settings/预设/会话、graph/时域接点、残差 shader、QML bridge/逐层面板、翻译、来源 notice、必要 CMake/定向测试与本地打包；此明确范围覆盖历史 UI-only 冻结。默认调控关闭，保留原有路径及总变化量 0–5；自动和手动设置分别保存，不更换、修改或下载 NR DLL，不改采集/解码/音频/补帧/旧 Win32。允许本轮必要 commit/tag/源码 bundle 存档和完整本地候选交付，不包含新 merge/push/Release/关机，不派 Agent。所有产物 E 盘，测试每进程≤300秒、构建≤900秒，不制造 GPU 压力。每轮先运行 `py -3.11 -B scripts/acceptance/nr-protection-control.py guard`；旧 guard/baseline 保留，当前不可变基线独立保存。

> **2026-10-05 本地 main 合并授权**：用户在 Codex 接管 Claude 最新验收和现场修复后明确要求“先合并到main，然后有其他修复等你做”。本次允许提交继承的未提交修复、存档、打标签和本地合并；已将 NR 性能分支及 `claude/field-fixes-20261005` 修复提交 `6e9d827` 合入本地 `main`，合并提交 `f7ced91`。此条覆盖这批工作的历史“不 merge”限制；不包含新的 push/Release、旧 UI 删除或剩余修复实施授权。合并记录见 `docs/MERGE_MAIN_2026-10-05.md`，后续新修复按用户下一条具体要求处理。来源分支和其他工作树保留，桌面旧工作区与用户配置不改，运行库/SDK/模型不得进入源码 Git。

> **2026-10-05 性能优化续推约束**：用户明确“继续做吧，我睡了，不要做什么压力测试了”。继续当前NR优化分支与逐节点存档；后续只做正常负载的匹配A/B/B-off和必要功能/画质回归，不启动额外GPU竞争程序、不人为满载或制造显存压力，不改用户其他应用/驱动/配置。该约束覆盖原方案3a/3b等的压力测试要求；动态档以真实播放负载与可控正常素材验证，未测压力边界如实记录。原压力数据仅保留作历史证据，不再执行其驱动。没有新增merge/push/Release/关机授权。

> **2026-10-04 NR性能优化目标授权**：用户要求按 `docs/PERF_PLAN_NR_2026-10-03.md` 完成优化，先更新进度文档、创建新分支/存档、记录当前版本基线，每节点存档，负优化回退并记录，有效优化保留实测。工作树 `E:/项目/Veyra/worktrees/perf-nr-20261004`，分支 `codex/perf-nr-20261004`，起点 `8cdc612`（保留本地RTSS重启/字幕/自定义倍速/UI/布局/全屏修复），执行记录 `docs/PERF_EXECUTION_NR_2026-10-04.md`。本方案必需graph/NGX/device/engine/presenter/export调度/性能设置/bridge/QML/diagnostic/shader/CMake及定向测试接点获当前授权，覆盖这些接点的历史冻结；不扩改解码/音频/串流协议或旧Win32。允许逐节点commit/tag和源码bundle存档；不merge/push/Release/关机，不派子Agent，不改桌面旧工作树、其他工作区、用户配置或驱动。所有产物E盘；运行库/SDK/模型不进源码Git且不改原件。每轮运行 `py -3.11 -B scripts/perf/nr-control.py guard`，不可变启动记录与逐节点证据在E盘archives/logs。未验证/外部条件缺口与负优化如实记，不把原方案估算当收益。

> **2026-10-04 2.0.3 发布授权**：用户明确要求将当前 Xbox/导出码率/RTSS 背景/NVIDIA 插件误判/极简像素缺口修复、AMD NR 与全档 VFG 整合到 `main` 并发布 GitHub `Likely7/Veyra-NRVideo` 2.0.3；另将便携包拆成 NVIDIA / AMD 两份，软件内不支持的功能保留显示并置灰。按 `docs/RELEASE_2.0.3_PLAN_2026-10-04.md` 在现有 E 盘隔离工作树的 `codex/release-2.0.3-20261004` 执行，允许必要能力判断/QML/打包/版本/验收接点和本轮 commit、tag、merge、push、Release 及已审计运行组件上传。本条覆盖此前本轮仅本地候选的限制；HDR/Dolby Vision PR #13/#14 用户明确暂缓，不适配、不并入。本次没有新的关机要求；不派 Agent、不改桌面旧工作树或用户配置。仍禁止 SDK/运行库/模型进源码 Git。

## 显卡分包规则（2026-10-04 起）

- 每个正式版本分别发布 `Veyra-<版本>-NVIDIA-win64-portable` 和 `Veyra-<版本>-AMD-win64-portable`，从同一源码/生产构建生成。各包包含完整基础播放器、采集、串流、音频、导出、Qt、patched FFmpeg 与必要公共组件，无运行时联网补齐。
- **按实际硬件支持分包，不按品牌名字盲删。** NVIDIA 包保留已审计 NVIDIA NR/DLSS/RTX SR/HDR/NVOF/VFG 与跨厂商可用的 FSR 3.1、XeSS 等，不包含 AMD RX9000 NR 的模型/HIP/runtime。AMD 包保留 AMD NR、FSR3.1/适用 FSR4 和跨厂商可用的 XeSS 等，不携带 NVIDIA NR/DLSS/RTX SR/HDR/NVOF/VFG/CUDA 等 AMD 无法使用的组件。共享依赖只保留必要闭包；显卡驱动文件不随包复制。
- **FSR 例外（用户本轮追加：“FSR3.1和4分开太麻烦了，就留着吧”）**：两包都保留现有已核验 FidelityFX SDK 2.3.0 的 loader/upscaler/framegeneration 三组件，不拆分 FSR3.1/4、不替换为旧 SDK、不添加旧 API 兼容路径。NVIDIA 只开放实际可用的 FSR3.1；FSR4 ML 入口仍可见并置灰。此决定覆盖前面“NV 包不带 FSR4 内容”的要求，仅针对共用 FidelityFX 组件，不能扩大到 AMD NR 或 NVIDIA 专用组件。
- 软件内全部功能入口保持可见。不支持的显卡、该包未包含组件或运行库缺失时置灰，并提供真实原因；列表、节点、版本/算法选择和预设加载统一消费硬件/运行组件能力，不让禁用项偷偷生效。初始化失败必须报告实际错误，不得宣称已生效；保留用户设置，不用删除 UI 或伪造支持掩盖缺口。
- 每包有独立 `package-manifest.json` 和运行组件来源/版本/大小/SHA256/签名/许可证记录，标明目标显卡与包含/排除组件；源码/依赖源码单独提供。打包前审计 imports/delay-imports，打包后校验全部文件、干净解压启动和缺失组件回退，确保被排除 DLL 不成为启动的强制依赖。
- 不为瘦身修改 DLL/模型字节、量化或删除未经证实无用的权重/内核，不移除已支持档位。分包大小和实测显卡分别报告；本机 RTX 测试不冒充 AMD 实卡推理或主机长稳验收。压缩格式须说明解压方法。

> **2026-10-04 追加授权**：用户要求VFG（消息写VSF，按前文VFG理解）和AMD NR尽量瘦身，稳定性优先，再构建新的完整本地测试包、汇报HDR PR合并计划、保存完成后关机。在 `codex/runtime-size-20261004` 按 `docs/RUNTIME_SIZE_PLAN_2026-10-04.md` 执行，基线fc2ca716，tag `checkpoint/pre-runtime-size-20261004`、bundle在E盘archives/runtime-size-20261004。只授权必要依赖加载/打包/定向验证接点；不修改proprietary DLL/模型、不牺牲功能档位或未经实测移除必要文件。HDR仍只方案等审批，无merge/push/Release授权，不派Agent。所有产物E盘；测试每进程≤300秒、构建≤900秒；完成包/报告/源码存档和停止本轮子进程后，按用户明确要求关机。

> **2026-10-04 当前授权**：立即修复极简窗口右侧像素缺口；HDR/Dolby Vision PR #13/#14 只做研究、静态审查和适配/UI方案，待用户审批才修复并入。在 `codex/minimal-edge-hdr-review-20261004` / 当前E盘隔离工作树按 `docs/MINIMAL_EDGE_HDR_REVIEW_PLAN_2026-10-04.md` 执行。黑边所需QML原生宿主窗口几何/裁切、默认极简窗口取整的最小显示几何接点及定向验收获本轮授权，覆盖对应历史冻结；HDR算法/UI/PR合并未授权。复现证据与新增PresentationGeometry/VideoPresenter范围先记录在方案，不改增强/颜色链。保留已验VFG/现场修复、main、桌面、候选和用户配置；不派Agent、不merge/push/Release，所有产物在E盘，测试≤300秒、构建≤900秒。

> **2026-10-03 VFG 接入授权**：用户“接入看看，8x也支持上”“所有档位都加上”。在当前 E 盘隔离工作树的 `codex/vfg-integration-20261003`，以 `1802f43565e07f3c3040d3fe745ca2e939aab00f` 为基线，按 `docs/VFG_INTEGRATION_EXECUTION_2026-10-03.md` 接入 NVIDIA VFG、全部 2X–8X 与 Low/Medium/High。必要的 native backend、D3D12/CUDA bridge、共享 graph/pools/leases、settings/preset/session/worker、preview/export、QML/bridge、shader/CMake 与针对性验收获本轮授权，覆盖对应历史冻结；不扩改无关链路，不派子 Agent，不 merge/push/Release。存档 `checkpoint/pre-vfg-integration-20261003` 与 `E:/项目/Veyra/archives/vfg-integration-20261003-start/source-before.bundle`；原五项修复、main、桌面、用户配置与已有候选保持。新 SDK/运行库/模型仍在 E 盘独立 deps，不进源码 Git；默认 Medium，高倍率实验标识、实测与未验边界如实报告。测试≤300s、构建≤900s。

> **2026-10-03 当前真人现场修复与升级授权**：用户明确要求 Xbox 无声/断开修复、lmxxf AMD RX 9000 NR 接入、导出码率回跳/设置无效修复、RTSS 兼容背景透明修复、取消 NVIDIA App 插件检测；先存档、写方案、创建分支再动手。仅在 `E:/项目/Veyra/worktrees/field-upgrade-20261003` / `codex/field-upgrade-20261003`（main `66cd3e5`）按 `docs/FIELD_UPGRADE_PLAN_2026-10-03.md` 实施。这五项必要 backend/NR/graph/source/QML/bridge/CMake/定向验收接点获本轮授权，覆盖对应历史冻结；无关链路不扩改。开工存档及标签见方案；旧桌面 guard/baseline、main、其他工作区、用户设置和已发布包保持。所有产物在 E 盘，不派子 Agent，不 merge/push/Release/删除旧 UI。单测试≤300秒、单构建≤900秒；现有运行库身份、patched FFmpeg 与源码/模型隔离不变，AMD/Xbox 实机未验如实记录。

> **2026-10-02 导出优化目标授权（用户：“开启目标模式搞定他”）**：按 `docs/EXPORT_PAGE_OPTIMIZATION_PLAN_2026-10-02.md` 在 `E:/项目/Veyra/worktrees/export-page-20261002` / `codex/export-page-20261002` 实现导出页布局、可编辑顺序队列、MKV/多音轨/内嵌字幕、取消重试及完成提示音，并做真实验收。本条仅解除该方案列出的导出后端、QML/bridge、必要 CMake 与定向测试接点的历史冻结；其他链路不改。每轮先执行本分支 `python -B scripts/acceptance/export-page-control.py`，不可变基线在 `E:/项目/Veyra/archives/export-page-20261002-start/scope-baseline.json`，不得修改旧桌面 guard/baseline。Claude 的其他工作区、主分支、已有候选与用户配置保持原样；专用 E 盘构建/测试/TEMP，不派子 Agent，不 commit/tag/merge/push/Release。单测试≤300秒，构建≤900秒；已有 runtime/patched FFmpeg 身份和源码隔离约束保持。
> **2026-10-01 Moonlight（PC 串流）接入授权（用户：“不用，我有用户有双机，你就先干Moonlight吧，开工”；并同意下载官方源码、按阶段在新分支提交）**：在 `E:/项目/Veyra/worktrees/moonlight-20261001` 的新分支 `codex/moonlight-20261001`（从 `main` `09392c4` 开出）按 `docs/STREAMING_PLAN_MOONLIGHT_XBOX_2026-10-01.md` 与 `docs/MOONLIGHT_EXECUTION_2026-10-01.md` 实现串流核心：新增 `src/moonlight`、`include/veyra/moonlight`、`cmake/VeyraMoonlight.cmake`、`scripts/moonlight`、`tests/moonlight`，以及接入所必需的来源类、引擎/桥接/QML 接点；只接入串流核心，效果器沿用现有同一条处理链，不为串流另设预设或开关。PS5、采集卡等既有来源路径不改行为。已按用户授权从 GitHub 下载并固定两个官方源码到 `E:/项目/Veyra/deps/moonlight/`：moonlight-common-c `f900dd4767759c7b9d0e93bcea666b55c69ea62f`（含 enet `aca87840…`、nanors `b1e3c22c…`）与 moonlight-qt 浅克隆 `8369d1a0e11b999d4d1598f62ca5f6dea49602fb`（仅作移植参考，不编译）。**不下载、不安装、不连接 Sunshine 或任何主机**（用户明确“不用”，真机互通由用户的双机测试者验证）；Xbox 仍只做调研。可在该分支按阶段 commit；不合并 main、不 push、不 Release、不碰桌面原目录。真机互通、4K60 实测与运动画面未验前，不得称 Moonlight 已通过。

> **2026-10-01 2.0.0 合并 main 授权（用户：“基本上2.0.0已经没啥问题了，剩下一些小bug和功能我后面再和你对，现在分支有点多……先合并一手，存档一下，更新更新文档，对齐目前进度”）**：整理 2.0.0 各分支并合并到**本地** `main`；先存档到 `E:/项目/Veyra/archives/merge-2.0.0-20261001/`（全引用 git bundle、合并前两处未提交状态、SHA256），打 `checkpoint/pre-merge-2.0.0-main-20261001`、`checkpoint/2.0.0-branch-tip-20261001`、`checkpoint/merged-2.0.0-20261001` 标签，合并后更新文档。授权范围：commit、打标签、本地 `--no-ff` 合并、删除已并入 `main` 且指向同一提交的重复分支名。**不含** push、GitHub Release、Runtime Pack 上传、删除旧 Win32 界面、删除 worktree 或用户文件、改动桌面原目录；这些仍需另行授权。此后 2.0.0 的新工作从 `main` 另开分支；本文件此前条目中的 `codex/*-2026092x/30` 阶段分支名均已并入 `main`，见 `docs/BRANCH_MAP_2026-10-01.md`。

> **2026-09-30 真人反馈修复授权**：先存档，再将 RTX40 NR 改为 SF-v2、RTX50 保留 Lecram，NR 节点提供双版本选择，淘汰新候选中的旧40运行库；随后按用户指定 `FIX_PLAN_1.4.4_FIELD_ISSUES_2026-09-30.md` 排查修复。仅在 E 盘 `p0-p1-r53-20260927`、新分支 `codex/field-issues-20260930`；保留当前 FSR/XeSS 和全部既有改动。必要的 NR 选择/配置迁移、GPU 故障、呈现、采集协商、HDR/比例/运动输入、分辨率/导出反馈修复接点获本轮授权，覆盖这些接点的历史冻结；无关改造仍冻结。范围、存档和验收见 `docs/FIELD_ISSUES_EXECUTION_2026-09-30.md`。原桌面 checkout、main、原 beta/历史发布/下载原件保留；不 commit/tag/merge/push/Release，不派子 Agent。新候选可原样使用已批准 Lecram/SF-v2，不 patch、不入源码 Git。

> **2026-09-30 本轮真人授权 XeSS / FSR 补帧修复（用户：“行，那就开始排查修复，以及增加FSR3 FSR4 写执行方案”）**：在 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` 的独立分支 `codex/fg-fsr-xess-20260930`，按 `docs/FG_FSR_XESS_EXECUTION_2026-09-30.md` 排查 XeSS 2×/4×、修复 FSR 热切换并增加 FSR 3.1 / FSR 4 入口。本条仅覆盖这些功能必要的 UI / bridge / settings / FG / graph / Present / 针对性测试及 CMake 接点的旧冻结；不改采集/解码/音频时钟、NR/SR 算法、旧 Win32、桌面原 checkout、原 beta 和 main。禁止用副显示器或第二隐藏视频窗口解决切换；FSR 4 RX 9000 实卡未验不得称通过。已有 169 个修改/未跟踪源码逐文件存档，范围基线 `E:/项目/Veyra/archives/fg-fsr-xess-20260930-start/scope-baseline.json`，SHA256 `c871f25fe503e30aeae3f63234c920a7574edf65da8d1643ec29e94b173d8a11`。本轮不 commit/tag/merge/push/Release，不派子 Agent；所有产物仍写 E 盘。

> **2026-09-30 补帧运行库升级 310.9.1（用户：“升级，然后文件我下好了……直接搞定掉”）**：`nvngx_dlssg.dll` 由 NVIDIA DLSS SDK 310.7.0 换成官方 310.9.1：SHA256 `FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82`，7,460,976 bytes，310.9.1.0，Authenticode Valid/NVIDIA；来源 `third_party_local/nvidia/DLSS_SDK_310.9.1`（用户自行下载）。310.7 运行库保留为回退，SR (`nvngx_dlss.dll`) 仍固定 310.7，不升级。40 系与 30 系的补帧解锁改为移植 SilyNoMeta/DLSSG-Transfusion（MIT）的 310.9.1 补丁：`src/ngx/DlssgTransfusion.cpp` 在 NGX 初始化前写入 provider（架构门槛 0x1b0→0x190/0x170、`GetGPUArchitecture`/`*_GetFeatureRequirements` 的 Ada 立即数、count/index validator、Blackwell 内核 fatbin 重建与容器就地重定向、DL1/DL2 网络内核替换、输出图像内核替换、valid-warp 质量策略），全部记录并逐字节回滚，NvAPI 只改 provider 自己的 `GetProcAddress` IAT 槽。优化内核以运行时文件随包提供（`runtime/experimental/dlssg-kernels`，29 个 .ptx），不进源码 Git；这两位作者都未授权这批内核，用户 2026-09-30 明确接受该风险并承担。本机 RTX 5070 已验收：补丁应用/回滚、harness 真值测试、产品 60fps 冒烟（2400+ 生成帧、failed=false）通过；**RTX 40/30 实卡未验收**。另有未决项：valid-warp 与优化内核同时生效时合成序列哈希与基线不同（单独任一项与基线逐位一致），需要判断是否为目标内的画质改动。发布、打包上传仍需另行授权。

> **2026-09-29 NR 默认运行库替换（用户：“直接接入，替换原有文件”）**：NR 默认位置（原“NVIDIA 原版”，`runtime/experimental/nvngx_dlssnr.dll`）改用社区 Lecram 构建 310.8.3.0：SHA256 `F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC`，165840496 bytes，Authenticode `HashMismatch`（NVIDIA 证书、内容已修改），来源 RankFTW/rhi-repo `dlssnr-310.8.Lecram`（zip SHA256 `ddb64e55…ca54`，用户自行下载）。依据：RTX 5070 原生 4K 实测 NR 输出与 NVIDIA 310.8.0.0 逐字节相同，耗时 -2.3%、功耗 -4.7W（`docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md` §8）。本机研发源为 gitignore 的 `third_party_local/nvidia/dlssnr-310.8.Lecram/`；`stage-runtime.ps1`、`package-portable.ps1` 和 `stage-ui-migration.ps1 -NrDefaultRuntime` 固定这一身份。NVIDIA 原版 E16BCF15 退为回退备份，社区版 984BEE0F 与 Ampere 版 DCC0DC24 不变。本条取代下文“二进制身份”与“当前 DLSSNR 许可范围”中对默认位置的 310.8.0.0/Valid 要求；1.4.4 发布包、原 checkout 不改；Runtime Pack 的发布/上传仍须另行授权。

> **2026-09-29 RTX30 NR 替换（用户：“30系列之前那个优化版本的NR……给30系列的替换掉”；同时决定 SR/FG 暂不升级 310.9.1，以免 30/40 补帧补丁失效）**：`nr-ampere` 位置由 NeuralScreen 1.8.2（DCC0DC24）改为 ShortFuse SF-v2：SHA256 `6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927`，165830144 bytes，版本字符串 310.8.SF.0（数字 310.8.2.0），`NotSigned`，来源 RankFTW/rhi-repo `dlssnr-310.8.SF-v2`（zip SHA256 `1da35941…955c`，用户自行下载）。SF-v2 除内核外还改写了模型权重区段（约 1.42 亿字节）；在 RTX 5070 上同帧 NR 输出与原版逐字节相同，RTX20/30 的 FP16 路线画面未测。它自带 RTX20/30 的 FP16 路线，因此兼容层对 310.8.2 及以上版本不再把 Ampere 改报为 Blackwell（`nrRuntimeNeedsAmpereRewrite`）；310.8.0/310.8.1 仍保持改写。本机只有 RTX 5070，**30 系实际效果未验证**，需持卡用户验收。DCC0DC24 保留在 1.4.4 包内作回退；`package-portable.ps1` 仅对 2.0.0 及以上版本改用 SF-v2。

> **2026-09-28 P4续推授权（用户：“p4估计快推进完了吧……把p4推完”，接续Codex会话01a0e687）**：Codex后半程把P4-b至P4-g全部按“UI-only冻结”记为partial并停止，但本分支P4授权本已写明“P4所需既有服务/bridge接点获本次功能范围授权”。本轮(Claude Code)按该授权补齐接点：P4-g接既有`ColorLutStore`/`ColorLookStore`；P4-c把既有`SubtitleLoader`/`SubtitleOverlay`搬入QML宿主（CMake仅给`veyra_qml_ui`加该源文件）；P4-d在`WasapiAudioSink`加指定渲染端点ID/回落默认/强制立体声（不改时钟、不丢PCM）；P4-b修`ExportJobManager`冲突项被递归跳过；P4-e/P4-f只接既有服务。扩展基线`E:/项目/Veyra/archives/p4-rest-20260928/scope-baseline.json`（P4-a文件哈希原样继承，仅追加allow）。后续追加`scope-baseline-b.json`（qml/Veyra全部组件）与`scope-baseline-c.json`（EngineController解码选项）、`scope-baseline-d.json`（qml/Veyra/backdrop-strong.png，修首页背景发白）、`scope-baseline-e.json`（2026-09-29 试用 18 条问题：保护区域椭圆的着色器/常量缓冲/分块导出与收尾诊断）、`scope-baseline-f.json`（2026-09-29 设计稿功能补齐：文件源“只用硬解”）、`scope-baseline-g.json`（2026-09-29 P5 逐屏差异修复：缩略图服务封面/队列缩略图、转圈/均衡条/图标按钮组件；用户要求删除全部内置预设）、`scope-baseline-h.json`（同轮：NR 层卡片改名后更新组件测试 tst_components.qml），同样继承哈希。硬件（PS5配对、采集卡实机、拔插）无法由Agent完成的项如实列未验。不commit/tag/merge/push/Release，不派子Agent。

> **2026-09-28 P4 授权（用户“开启目标模式干完P4”“别忘了存档”）**：在隔离工作区 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` 的 `codex/p4-functions-20260928` 分支，按 `docs/UI_V2_0_0_MASTER_PLAN_2026-09-27.md` 的 P4-a 至 P4-g 逐片实现并验证。P4 所需既有服务/bridge 接点获本次功能范围授权；不扩展到 P5 外观、P6 性能、无关采集/解码/音频时钟/算法重写。P4-b 仅列表模式/列表预设离线导出；节点模式与节点预设所有入口明确拒绝。原桌面 checkout、main、旧 Win32、已验候选、运行库和用户配置保持原样；不自动 commit/tag/merge/push/Release/删除旧 UI，不派子 Agent。每片存档并保留失败证据。开工归档 `E:/项目/Veyra/archives/p4-start-20260928/` 包含 104 个文件及哈希清单；独立 P4-a 不可变范围基线 `p4a-scope-baseline.json` SHA256 `5a4e63bdbdaa775db20912becb231a73324a644c109c38413e75967ce52501f7`。旧桌面 UI guard 开工 pass；后续独立 guard 与逐片记录见 `docs/P4_FUNCTIONS_EXECUTION_2026-09-28.md`。

> **2026-09-28 a32授权（用户：节点固定节点不能移动、节点参数必须在节点内调且与列表一致、贴近弹开动效、连线无效、拖到线上不能插入；“是p3就修掉”）**：按设计稿补齐P3节点编辑器：卡片内完整参数（与列表同源组件/接口）、输入/输出/光流/补帧卡可移动并持久化、settle防重叠弹簧、端口点击/拖拽连线（修a30引入的端口点击被画布空白点击清除的回归）、拖到连线插入与拖离断开。扩展基线`E:/项目/Veyra/archives/p3-nodeedit-20260928-a32/scope-baseline.json`（a31哈希继承，新增NrLayerEditor.qml）。不改引擎算法/CMake，不commit/push。

> **2026-09-28 a31授权（用户“把Alt那个问题也修了，同时补帧为什么没有开关？然后XESS补帧无效，DLss不是5x而是6x才对”）**：修无边框窗口单按Alt进入系统键盘菜单模式（`apps/veyra-qml/main.cpp`原生消息过滤SC_KEYMENU）、列表补帧开关、XeSS速率显示与4X上限、DLSS倍率列表按引擎合法集{2,3,4,6}与能力+1上限。扩展基线`E:/项目/Veyra/archives/p3-fgalt-20260928-a31/scope-baseline.json`（a30哈希原样继承，仅新增main.cpp）。不改补帧算法/SDK/Present，不commit/push。

> **2026-09-28 P4前修复批次授权（用户“可以写好直接开干”）**：修复用户手测a29-v2提出的9条问题（暂停无法继续、节点页胶囊/页面状态、极简预设按钮与跳转全屏、无边框窗口移动/缩放/三按钮、列表光流与HDR入口、确认框风格与过时文案、节点画布右键添加/中键平移/工具栏、列表耗时面板溢出、滚动条风格）。计划`docs/UI_FIX_BATCH_2026-09-28.md`；扩展基线`E:/项目/Veyra/archives/p3-fixbatch-20260928-a30/scope-baseline.json`，SHA256=`a1e5c69566875deb4cf167432e38f55ac905d89a891bca744769b76fb9ed3731`（a29哈希原样继承，仅新增QML/文档路径）。不进入P4新功能，不改引擎算法/shader/CMake，不commit/push。

> **2026-09-28 P3收尾授权（用户“全部同意，把p3给我干完”）**：针对Claude Code列出的三项P3剩余缺口全部同意：①逐节点GPU计时（解冻`include/veyra/diagnostics/FrameMetrics.h`阶段枚举并在`EnhanceGraph`逐NR层/逐Color实例打时间戳，bridge/NodePage显示）；②TrueHDR能力查询参数块所有权（`src/ngx/TrueHdrBackend.cpp`改为直接销毁并记录返回值，不经allocate跟踪表）；③原图像素、失焦、HDR/FG实卡生成帧补验。旧Win32 `apps/veyra/TelemetryWindow.cpp`名称表按枚举定长以免越界。新不可变扩展基线`E:/项目/Veyra/archives/p3-finish-20260928-a29/scope-baseline.json`，SHA256=`9eaa733b73432890bbe29a6b55c55fe9ff61474256536be82aa8497c35e18a09`（a8文件哈希原样继承，仅新增3条allow）。其余冻结（采集/解码/音频/Present、防闪、NR/颜色算法、shader、CMake）及不commit/push/Release不变。

> **2026-09-27 当前真人重新授权（“允许，继续吧”）**：针对上一条明确提出的“允许P3所需节点执行、独立配置、保存恢复后端接点；其他冻结范围不变，并恢复目标”，用户已明确同意。仅在`E:/项目/Veyra/worktrees/p0-p1-r53-20260927`、`codex/p3-node-backend-20260927`继续P3限定接点及直接验收，覆盖这些接点的UI-only限制；不改原checkout的冻结规则。现有32路径基线保持不改；不解冻采集/解码/音频/Present、防闪/1080p240、NR/颜色算法、CMake/旧Win32，不派子Agent、不commit/tag/merge/push/Release。按唯一P3剩余计划推进真实合法单链，不以拒绝合法顺序冒充完成。

> **2026-09-27 P3节点后端有限续接授权**：用户对“另开后端分支，仅补齐节点执行、独立配置及保存恢复”明确回复“可以，没问题”。在现有E盘隔离区切入`codex/p3-node-backend-20260927`，82份已有修改已逐文件保全。仅本次节点后端必要接点覆盖下方UI-only冻结；采集、解码、音频、Present、防闪、NR/颜色算法、CMake、旧Win32和发布仍冻结。先做状态隔离/持久化：22路径基线`E:/项目/Veyra/archives/p3-node-backend-20260927/state-scope-baseline.json`，SHA256=`d9943d4c28b305c80b7732558e50926a9c0a49e0a81c97c03df6eadb1e40fb9b`。执行器范围须写前明确最小依赖并另存不可变扩展基线；不得改旧基线洗白。沿用测试≤300秒、构建≤900秒、同因最多两次有据修复、20分钟无新证据停止该路径；不派子Agent、不commit/tag/merge/push/Release。见`docs/P3_NODE_BACKEND_EXECUTION_2026-09-27.md`。

> **P3续接范围澄清（2026-09-27）**：当前用户给出的UI-only冻结规则优先于下文历史P2解冻记录。P3仅在既定18路径白名单内修复QML/bridge接线与控件；不改engine/source/pipeline/shader/NR算法/export/color算法、CMake或旧Win32。已确认现有链转换不能保留任意节点拓扑，独立模式恢复接口也未接入；R5.4/R5.5执行器部分属于已登记的接口缺口，停止该路径，不扩baseline、不自动重试、不用节点预览冒充完成。后端须另行授权、另开分支和验收。P3文档不覆盖用户指定的UI_MIGRATION主方案/施工记录/恢复方案。

> **2026-09-27 P3 开工**：用户明确要求“开始干p3，开目标模式”，并指出调色未生效以及曲线、混色器、颜色分级只有滑条。当前先做 P3-a 调色接线与真实控件，只修改既有颜色合同的 QML/bridge 和直接测试/文档。独立基线 `E:/项目/Veyra/archives/p3-node-color-20260927/scope-baseline.json`，SHA256 `e78c75f6716a7fc59f8a7610b4e11468ec2820bff4dbf631e185f074514b5c9a`；18条白名单以该文件为准，不覆盖旧基线。后续节点执行器若出现接口缺口，先记录证据与范围，再按现有授权边界审查，不得以换界面为由顺手重构底层。防闪与1080p240继续暂缓；保全P2候选、原checkout/main/1.4.4/设计稿/运行库/用户配置。禁止子Agent、commit/tag/merge/push/Release与删除旧UI。每测试≤300秒、构建≤900秒，同因最多两次有依据修复，20分钟无新证据停止该路径。执行记录见 `docs/P3_NODE_COLOR_EXECUTION_2026-09-27.md`；整个P3未闭环不得标Goal完成。

> **2026-09-27 P2非防闪收口授权**：用户明确要求“开目标把P2剩下的推进完，防闪先放放”，包含前两轮指定的逐层NR编辑、全部参数可达及每层内部处理分辨率（默认1080p，1.4.4六档）。允许此P2切片必需的bridge、每层尺寸/实例描述及预设迁移最小修复；不进入P3节点执行，不改防闪shader/算法，也不以防闪未修阻塞本切片。工作区/分支不变。新独立基线 `E:/项目/Veyra/archives/p2-nr-ui-resolution-20260927/scope-baseline.json`，SHA256 `7a3f811468e76b545a6e88284f14953e4a7da75e71f757c3ebfe9e9d39e781ce`；旧基线、已验候选和用户手测程序保持不变。每测试≤300秒、构建≤900秒、同因最多两次有依据修复，禁止子Agent、commit/tag/merge/push/Release与无证据重复测试。

> **2026-09-27 P2 防闪修复解冻与迁移必要依赖授权**：用户明确回复“解除，为了新版的迁移，需要的合理部分都可以解除，不需要再问我”。本轮允许针对已复现的P2防闪网纹缺陷修改`shaders/NrTemporal.hlsl`及其直接回归测试；原定迁移中的必要依赖修复，有证据、有最小边界时不再逐文件询问。每次新增文件范围必须先记录依据，使用新的独立baseline，旧baseline/guard/已验候选永久保留；这不是解除所有技术保护、任意重构或洗白越界改动。当前目标仍仅P2，不自动推进P3–P8、R5.4/R5.5，不动已暂缓1080p240采集问题，不改原桌面checkout/main/1.4.4/设计稿/运行库/用户配置，不派子Agent、不commit/tag/merge/push/Release或删除旧UI。继续执行每测试≤300秒、构建≤900秒、同因一次诊断与至多两次有依据修复、20分钟无新证据停该路径。

> **2026-09-27 P2 接线追加确认**：用户在 NR 列表多层接线阻塞说明后明确回复“允许，这种原本设定中的都允许”。允许在现有 `codex/p2-nr-20260927` 隔离区补齐原定 P2 必需的 settings / EffectChain / GraphDescription / 多层参数应用及直接测试，必要时修正 QML 桥调用；此类计划内修复不再反复请示。旧 baseline 和已验部分存档不变，以新独立接线 baseline 记录新增具体白名单，授权文件更新后重新冻结。仍不授权 P3–P8、R5.4/R5.5、采集/解码/音频/补帧改造、merge/push/Release 或删除旧 UI。

> **2026-09-27 P2 追加授权（仅隔离分支）**：用户要求“现在继续开启目标模式，做 P2”，并暂缓此前讨论的 1080p240 采集问题（未证明原因是电脑性能）。复用 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927`，转入 `codex/p2-nr-20260927`；仅执行总方案 P2 的 NR 多层/单层旧链兼容复验及证据驱动最小修复。下方 P0/P1 限制和 UI-only 冻结只对本条明确范围让位；其他冻结仍有效，不授权 P3–P8、R5.4/R5.5、采集/解码/音频/补帧改造、旧 UI 改造或删除、merge/push/Release。先核验 P1 最终源码快照，P2 使用独立不可变 baseline；不得改旧 guard/baseline 洗白。执行范围、命令限时、失败上限和验收缺口见 `docs/P2_NR_EXECUTION_2026-09-27.md`。禁止子 Agent、修改原 checkout/已验候选/运行库/用户配置/账户或模型。

> **2026-09-27 P0/P1 追加授权（仅隔离分支）**：用户明确要求“先开目标模式把p0和p1解决掉”。只在 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927` / `codex/p0-p1-r53-20260927` 执行 `docs/UI_V2_0_0_MASTER_PLAN_2026-09-27.md` 的 P0/P1；本条仅为 R5.3 调色独立 pass 的证据驱动修复、保护和验收解除下条相关冻结，不授权 P2–P8、R5.4/R5.5 或重写播放链路。原 checkout、main、1.4.4 包、原型和运行库保持原样。每轮先运行本分支独立 P0/P1 scope guard；旧 UI guard 和旧基线不能修改或用来证明本分支保护。执行与停止合同见 `docs/P0_P1_R53_EXECUTION_2026-09-27.md`。禁止子 Agent、merge/push/Release、删除旧 UI、调整账户或模型路由。

> **2026-09-27 用户纠偏（覆盖 2026-09-25 的“QML 新界面 + 引擎改造”授权）：当前 2.0.0 只做 QML UI migration。** 现有分支里已经混入的 engine/source/pipeline/shader/NR/export/color 改动保持原样并冻结，不回滚、不继续修改、不计入 UI 完成度。禁止继续修改 `src/engine`、`src/pipeline`、`src/source`、`src/sink`、`src/media`、`src/gfx`、`src/ngx`、`shaders`、`CMakeLists.txt`、旧 Win32 `AppShell.cpp`，也禁止推进采集、解码、音频、导出、补帧、NR、R5.3/R5.4/R5.5。QML 只能消费已有接口；接口缺口必须记录并停止，不能借 UI 迁移之名改链路。每轮无人值守先运行 `scripts/acceptance/ui-migration-scope-guard.ps1`；guard 失败必须停工。活动方案以 `docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md`、`docs/UI_MIGRATION_EXECUTION_2026-09-25.md` 和 `docs/UI_MIGRATION_RECOVERY_PLAN_2026-09-26.md` 为准。后端能力另行授权、另开分支、另做验收；旧 Win32 删除、merge、push、Release 仍需用户明确授权。

> 2026-09-25 用户授权以目标模式实施 QML 新界面迁移与引擎改造：单分支 `codex/ui-qml-migration-20260925`（起点 `df41580`，标签 `checkpoint/pre-ui-qml-migration-20260925`），按 `docs/UI_MIGRATION_MASTER_PLAN_2026-09-25.md` 施工，进度记在 `docs/UI_MIGRATION_EXECUTION_2026-09-25.md`。允许引入 Qt 6（装在 `E:\项目\Veyra\deps\qt`，不进源码 Git）；新界面完成后删除旧 Win32 界面；按 Magpie（`SAOG0721/Magpie` `3841698`，GPLv3）移植 NR 多实例结构并逐项标注来源。补帧锁定在链路最后；只显示提交帧率；低延迟不得退化；每步打存档标签。合并 main、推送、发布仍需用户另行授权。

> 2026-09-19 用户授权修复全屏提示后构建并发布 1.4.2 至 Likely7/Veyra-NRVideo，整合本轮隔离修复到 main，更新双语 README 与 Release，重点说明 RTX Video HDR 和帧同步。授权公开本次新交流群二维码与 RTX Video HDR 对比图；赞助图保留，双二维码各 width=220。允许正式包携带已核验 TrueHDR 原件：RTX Video SDK 1.1.0，nvngx_truehdr.dll，3955752 bytes，1.1.0.0，Valid/NVIDIA，SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B，独立 manifest 与 NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf。其余运行组件身份沿用；NVIDIA FSR4 实验保持撤回。执行记录见 docs/RELEASE_1.4.2_EXECUTION.md。本条取代此前仅本地测试包的发布范围限制。

> 2026-09-18 用户授权目标模式实施可关闭帧同步、多模式与实际延迟验收。开工存档 `6e69eeb` / `checkpoint/pre-frame-pacing-20260918`，隔离分支 `codex/frame-pacing-20260918`。按 `docs/FRAME_PACING_EXECUTION_PLAN_2026-09-18.md` 施工，默认关闭、直接 NGX、保留 6X，不推送或发布。报告必须区分进程内排队、系统显示事件与未测量的屏幕端到端延迟。

> 本轮交付状态见 `docs/FRAME_PACING_ACCEPTANCE_2026-09-18.md`：本机软件短测完成，原生 Reflex 仅无补帧路径；FG 下显示回退低排队，XeSS 不叠加外部节奏。不得将软件驻留减少冒充屏幕延迟降低，不得把本机 RTX5070 验收扩展为 30/40、实卡采集、PS5 或 VRR 通过。完整 Reflex+FG 与显示延迟验收仍未完成；现有 beta 包不含本轮改动。

> 2026-09-18 当前用户授权制作 1.4.2beta 内测群便携包，包含 FSR4 回退及保留的 RTX Video HDR。允许携带已核验的 RTX Video SDK 1.1.0 TrueHDR 原件（SHA256 9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B，3955752 bytes，1.1.0.0，Valid），其余组件沿用 1.4.1 身份，逐文件 manifest 及许可证随包提供。仅本地交付用户自行发群；无 GitHub 推送、Release 或 Agent 代发消息授权。

> 2026-09-18 最新用户决定：停止并回退 NVIDIA FSR 4.1 实验，撤销其 UI、provider 接入及测试/打包入口；不继续画质修复。保留 RTX 超分、原有官方 FSR 和独立 RTX Video HDR 改动。本条覆盖下方 FSR4 本地试验授权；历史研究仅供追溯，不是待办。旧实验预设的 mode 6 读取时迁移到 RTX Video SR 高档，其他设置保留。

> 2026-09-18 用户追加授权：FSR 4.1 增加可保存的界面选择，随本地测试包携带独立 INT8 provider，方便直接试用。该本地实验入口取代上一阶段仅环境变量入口的限制；不代表画质验收或公开发布授权。

> 2026-09-18 当前授权：先更新项目文档，在 `codex/video-hdr-20260918` 隔离实施 RTX Video HDR，再验证 NVIDIA FSR 4.1.1 INT8 研究 provider。此决定扩展 09-16 的 NVIDIA FSR4 观察范围，允许本地构建/接入/测试，不代表正式支持或发布授权。存档 `checkpoint/pre-video-hdr-fsr41-20260918`；当前事实见 `docs/CURRENT_STATUS.md` 与 `docs/VIDEO_HDR_FSR41_EXECUTION_PLAN_2026-09-18.md`。旧计划中“尚未发布/尚未实现”的历史语句不覆盖 1.4.1 发布记录。

## 本机产物目录规则（2026-09-18 用户决定）

- 今后 Agent 创建的 Veyra 构建、测试、下载、打包、解压验证、诊断和临时文件，统一写入 **`E:\项目\Veyra\`**，不得再散落桌面、Downloads、C 盘根目录或系统 Temp，也不再在源码仓库内新建这些产物。源码、受版本控制的脚本和文档仍留在源码仓库；运行库、SDK 和模型仍禁止进入源码 Git。
- 按用途使用子目录：`build/<任务>`、`tests/<任务>`、`logs/<任务>`、`tmp/<任务>`、`downloads/`、`deps/`、`test-packages/<版本>`、`releases/<版本>`、`verify/<版本>`、`archives/<任务>`、`worktrees/<任务>`。任务名包含日期或明确标识；每轮在 WORKLOG 记录实际输出路径。
- 执行旧脚本前检查输出参数和硬编码路径，显式传入新目录；不支持时先做最小路径参数化并验证，再执行。测试/构建子进程需要临时目录时，仅对该进程及其子进程设置 `TEMP`/`TMP` 到上述 `tmp/<任务>`，不得修改全局环境。不得仅修改文档却继续默认向旧位置生成文件。
- 现存的 `C:\veyra-deps`、`C:\veyra-releases\1.4.1`、源码内现有 `runtime_local` / `third_party_local` / 当前构建及验收记录暂时保留，不能为整理目录直接移动或删除。需要迁移时先修正引用、重新配置并验证；后续新建或重新生成的产物遵守新目录规则。历史文档中的旧路径只是历史证据，不作为新任务默认输出路径。
- 此规则针对研发产物，不改变产品保存用户配置、PSN 凭据或用户主动截图/导出的路径，也不要求修改 Windows WER 等系统服务的全局设置。系统自动产生且无法通过子进程配置重定向的残留，应在任务结束时识别、记录并按当前清理授权处理。
- 每轮结束整理本轮中间包和解压副本，保留当前可用构建、最终交付包、对应源码及必要验收证据；不要复制整工程作日常备份，不删除用户文件或其他任务仍使用的产物。若 E 盘不可用或工具不兼容路径，明确报告并处理原因，不擅自退回桌面或 C 盘堆积。
- **2026-10-08 清理后约束**：同一版本的逐用例测试优先共享一份任务专用运行目录，用独立 `--data-dir`、日志和 TEMP/CUDA cache 隔离；确需改 QML/缺库/故障注入时使用任务专用副本，结束后清理，不修改交付包或依赖原件。不再为每个用例永久保留完整 DLL/模型/Qt 副本。收尾记录新增占用与实际清理量；未提交源码、唯一配置/脚本、必要日志与原件保留。旧临时副本已按 `docs/PROJECT_STORAGE_CLEANUP_REPORT_2026-10-08.md` 清理，引用这些路径的历史 guard 需要先从清单恢复或在新任务建立独立基线，不能改旧 start/guard 制造通过。

> 2026-09-18 用户决定：今后每个 GitHub Release 的正文必须保留交流群和微信赞助二维码，不能只放 README。固定区块见 `docs/RELEASE_SUPPORT.md`；两图并排、各 `width="220"`，保持用户认可的现有尺寸，未经要求不删、不换、不放大。发布及编辑后核对远端正文中的两个图片地址、尺寸与实际可访问性。用户授权公开的实测截图可作为 Release 图片资产展示，按截图事实标注显卡、设置和软件读数，不将显示提交 FPS 冒充物理屏幕刷新率。

> 2026-09-16 用户决定：**有成熟开源实现就直接搬过来改造，不要重复造轮子**。XeSS MFG 解锁（OptiScaler 的 XeFGUnlock/XeFGPacing，经 Magpie fork 适配）、40 系 DLSS MFG 解锁（RTX40MFG-Unlock、MFGAdaUnlock-RenoDx，MIT）、30 系原生 2X（dlssg_for_sm86）、AMD FSR 帧生成（FidelityFX SDK 的 Frame Interpolation / AMD FSR Frame Generation）等能力，一律**优先移植现成开源实现并接入 Veyra**，不做等价重写、不另起炉灶。搬运要求：逐项记录来源仓库、固定提交、许可证、被改动的文件与改动说明，写入 `THIRD_PARTY_NOTICES.md` 与对应源码；Veyra 自身为 GPLv3，与本批 MIT / GPL-3.0 来源兼容，禁止只改名不标注。对 NVIDIA / Intel 运行库只允许**进程内修改**，不改磁盘文件、不重签名、不伪装身份。移植后仍按本次改动范围做真实验证，未验证项如实报告，不得用上游项目名替代本机证据。计划见 `docs/FRAMEGEN_FSR_DOLBY_PLAN_2026-09-16.md`。

> 2026-09-16 用户追加决定：FSR 超分按显卡分档——**AMD 卡开放 FSR 4.1（ML）；N 卡只提供 FSR 2 / FSR 3.1，不开放 FSR 4.1**（N 卡跑 FSR4 目前无可用实现，仅列入观察名单，成熟后按"有开源就直接搬"处理）。AMD 补帧**必做且优先用新版**：先接 AMD FSR SDK 2.3.0 的 `AMD FSR Frame Generation 4.0.1`（ML），不支持的显卡回退本地 SDK 1.1.4 的 3.1.x。40 系 DLSS MFG 解锁已获条件授权：**只要确认不影响 50 系即可施工**（50 系走原生路径、不安装任何补丁）。

> 2026-09-16 用户授权合并到 main：隔离分支 `codex/framegen-fsr-dolby-20260916`（tip `ad7442d`）已合并（merge `24e7de7`）。存档 tag：合并前 `checkpoint/pre-merge-framegen-20260916`（main `87ad4cd`）、分支 tip `checkpoint/framegen-fsr-dolby-branch-tip-20260916`、合并后 `checkpoint/merged-framegen-20260916`；分支保留不删。冲突统一取分支原版实现，main 侧 RGB24 移植 `87ad4cd` 被 `015f8a4` 取代（其说明文档保留）。**未推送、未发布；push/Release 仍需当前对话明确授权。** N1–N4 本体未开工；40/30 系、真实位流卡与 AMD/Intel 导出实机验收仍待用户。

> 2026-09-16 用户授权开启采集链路优化（新隔离分支）：从 main `2406f81` 开 `codex/capture-decode-latency-20260916`（tag `checkpoint/pre-capture-decode-latency-20260916`），按 `docs/CAPTURE_DECODE_LATENCY_PLAN_2026-09-16.md` 施工：压缩解码架构（压缩直连 → D3D12VA 硬解 → MJPEG 并行软解）与原生 N1–N4（N4 缓冲协商 → N3 格式排序 → N1 逐像素转 GPU → N2 拷贝合并）。全部在隔离区，人工验收合格前不得合并 main；N1 必须保留 RGB24 方向契约（`015f8a4`）。真机验收用本机 USB3 卡 + 受影响用户；未生效的优化不得计入收益。

> 2026-09-14 用户授权实施 HDR 全增强与文件/采集 5.1，PS5 串流真实多声道不在本次范围。按 docs/HDR_ALL_EFFECTS_MULTICHANNEL_RESEARCH_PLAN_2026-09-14.md 施工，经过显式颜色合同扩展旧 SDR 边界；HDR 基底保留合成不宣称 NR 模型原生 HDR 推理。默认效果全关、共享处理图、直接 NGX、现有运行组件身份及 patched FFmpeg 保持。未授权新发布。

> 2026-09-14 用户授权发布1.1.1到Likely7/Veyra-NRVideo，包含RTX30 NR实验选项和首次效果全关。发布范围新增用户已指定的NeuralScreen1.8.2 NR原件：DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F / 165840496 bytes / 310.8.0.0 / HashMismatch，独立放在Release的runtime/experimental/nr-ampere/，逐文件manifest记录；原七组件与patched FFmpeg沿用。此授权扩展上次仅本地范围，不允许修改DLL或进入源码Git。RTX30实卡尚未验收，不能宣称全型号成功。更新双语README、完整便携包和对应源码；本轮未请求关机。

> 2026-09-14 用户授权实施 RTX30 NR 兼容选项与首次启动全部效果关闭。允许将用户提供的 NeuralScreen 1.8.2 包内 native/nvngx_dlssnr.dll 原样用于隔离本地接入测试：SHA256 DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F，165840496 bytes，310.8.0.0，HashMismatch。独立 runtime_local/nvidia/nr-ampere/，不覆盖原版/RTX40社区版；调用方兼容限定所选NR模块，不修改磁盘驱动或运行库。不授予新Release或将此文件入源码权限。初始NR/SR/FG全部关闭，已有明确保存的设置保留；RTX30实际性能需持卡验收，不能用本机RTX50通过代替。


> 2026-09-14 用户授权发布1.1.0至Likely7/Veyra-NRVideo：包含普通版Smooth Motion开启说明、允许自主叠加的策略及此前采集格式扩展；更新双语README与教程、完整便携包及对应源码。沿用已批准七个增强运行文件与patched FFmpeg，不新增驱动文件或SDK入Git。Smooth Motion由NVIDIA App管理，用户已反馈本机有效稳定，不宣称驱动算法内置、所有设备验收或叠加必然更好。发布完成并核实资产后按用户要求关机。

> 2026-09-14 用户确认 Smooth Motion 在本机实验版有效且稳定，决定普通版仅提供 NVIDIA App 开启方法与注意事项：只用驱动补帧时选择软件“关闭补帧”；换回内部 DLSS/XeSS 时由用户在 NVIDIA App 关闭 AI 插帧。允许用户自行叠加驱动与内部补帧，不检测、不拦截、不强制互斥，也不自动修改驱动配置。叠加效果未验证，不宣称更好；软件统计/截图/导出不冒充含驱动生成帧。本条取代此前 Smooth Motion 实验方案的强制互斥与软件内驱动配置管理要求；保留原实验分支作历史存档。本轮未授权新发布。

> 2026-09-13 用户授权修复持续播放后重连额度不恢复并发布1.0.2。连续30秒有解码进展、帧间无1秒断档后恢复本轮3次重试；会话累计计数/源epoch不回退，短暂出帧不续次数。包含此前手动占用重试、首帧等待和日志保留修复。沿用七个运行组件及patched FFmpeg；不把软件恢复改善宣传成Wi-Fi断流已根治。

> 2026-09-13 用户授权发布1.0.1：采集音频10ms缓冲协商与输入诊断修复合入主线、更新双语说明和完整便携包。沿用1.0.0七文件运行组件，FFmpeg slice补丁和对应源码要求不变。用户日志约半秒音频落后与旧缓冲缺口高度一致；修复后的反馈者实卡结果尚未取得，不宣称所有设备延迟已消除。

> 2026-09-13 用户授权整合已完成修复分支、替换透明 Logo，并向 Likely7/Veyra-NRVideo 发布正式应用版本 1.0.0。沿用七个已批准增强运行文件；保留 GPU DIS 为可选实验功能，NVOF 默认。必须使用带 PS5 H.264 slice 补丁的 FFmpeg，附对应源码、构建记录、补丁与开源许可证。正式应用版本号不改变 NR、DLSS FG、XeSS、GPU DIS、PSN/HDR 等功能的实验边界。SDK、DLL、模型、凭据、配置及测试媒体仍不得新增进入源码 Git。

> 2026-09-13 PS5 H.264 硬解细条修复：本机 FFmpeg n9.0.1 增加 `scripts/ffmpeg/ps5-h264-slices.patch`（32→256 slices），同一真实 PS5 AU 软/硬解已全图比较一致；另修正硬解 SRV 双帧覆盖。后续构建/发布不得无意退回未打补丁的 FFmpeg。对应源码应使用实际 patched tree，并携带 `veyra-local-build.json` 及补丁/重编译说明；原 vcpkg SPDX 仅代表底包。详见 `docs/PS5_HARDWARE_STRIP_REPAIR_2026-09-13.md`。本次只做本地修复，未授权新发布；NVIDIA 运行组件不变。

> 2026-09-12 用户授权合并 PS5 开发版并发布 0.0.5 至 Likely7/Veyra-NRVideo，更新双语 README 和完整便携包。沿用七个已批准增强运行文件，串流开源依赖附对应源码、固定版本、补丁及许可证。SDK、凭据、个人配置、测试媒体不进入源码或便携包。实验功能边界如实说明。

> 2026-09-12 用户授权实施 PS5 HDR、PSN 登录与主机保留方案（docs/PS5_HDR_PSN_HOST_PLAN_2026-09-12.md）。仅对本次经过显式HDR输入/输出契约的PS5路径扩展旧SDR限制；文件/采集/导出不得全局放开HDR防错检查。原生HDR旁路显示与“HDR转SDR后增强”必须区分，不能将实验NR的SDR结果冒充原生HDR增强。PSN凭据及主机密钥只在用户数据目录DPAPI加密保存，不提交Git。当前状态按对应施工记录，不由本授权推断测试通过。

> 2026-09-12 用户授权默认关闭的“低延迟模式”：实时预览可选 NR→SR→FG，取代本文件对该选项的固定SR先行限制。NR在源尺寸（或明确的实时内部尺寸）执行，残差/保护合成后再进入SR；共享现有图、光流和生命周期，不另造播放循环。默认仍SR→NR→FG，图片/视频导出保持原有完整处理。不得保证所有素材延迟下降；提示可能增加拖影/边缘瑕疵，真实屏幕撕裂不能简单归因于算法顺序。见 docs/NR_BEFORE_SR_PREVIEW_2026-09-12.md。

> 2026-09-11 用户授权执行实时调度修复（方案见 `docs/REALTIME_AV_SCHEDULING_REPAIR_PLAN_2026-09-11.md`，开工前存档点 `checkpoint/av-scheduling-2026-09-11`，隔离分支施工）：实时文件/采集**预览**在持续欠速时允许按真实 PTS 跳过已解码源帧的增强/呈现机会（先省补帧工作，再省原帧增强），稳态音频不停机、媒体时间保持一倍速、跳帧均匀覆盖时间线；源 PTS/序号/duration 保持真实，历史按断点 reset。该例外只限实时预览：视频导出、图片处理、暂停单帧查看仍必须完整处理，不得把丢帧扩大到导出，也不得把预览跳帧伪装成未丢帧。不得以停音频/丢 PCM/偷偷放慢声音制造同步。

> 2026-09-11 用户授权发布 0.0.4：当前音画同步与音频连续性修复推送到 `Likely7/Veyra-NRVideo`，更新中英文 README 并发布完整便携包。运行组件沿用 0.0.3 的七文件白名单及双 NR 原件身份，不新增或替换运行时；社区版继续记录 `HashMismatch`。源码 Git 禁止 SDK/DLL/模型，软件允许用户替换 DLL 的决定不变。

> 2026-09-11 用户授权发布当前0.0.3：源码推送到`Likely7/Veyra-NRVideo`，完整便携Release包含当前双NR运行版本和直播实验选项。此前用户指定的RTX40/50社区DLL可原样进入本次Release的`runtime/experimental/nr-community/`，身份沿用下条SHA256、310.8.0.0、165840496字节、`HashMismatch`，必须逐文件列入manifest并明确社区修改版；这是本次发布范围的扩展，不允许自动加入其他版本或修改文件。源码Git仍绝不包含SDK/DLL/模型。软件不恢复运行时哈希锁。OBS捕获说明按用户要求仅补README，不继续改软件提示。

> 2026-09-11 用户指定RTX40/50社区NR运行时接入：允许把用户明确提供的`E:/Ai/mg/DLSSNR-DLL-Options-310.8.0.0/Community-RTX40-RTX50/nvngx_dlssnr.dll`原样复制到忽略的`runtime_local/nvidia/nr-community/`进行本机切换测试。该文件SHA256为`984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`，签名状态`HashMismatch`，必须标注社区修改/实验，不能称为有效NVIDIA签名原版。用户指定此文件取代旧“仅根目录原版可本机加载”的限制；不授予Agent修改/重签名文件的权限，不自动扩大Release默认白名单，不上传DLL到源码Git。

> 2026-09-11 用户发布与替换决定：0.0.2发布到`Likely7/Veyra-NRVideo`，源码与Release二进制继续分离。用户明确要求去掉运行时校验、允许自行替换DLL：不得用固定哈希、签名或manifest拒绝用户替换的运行时；保留绝对路径加载、API存在性及初始化结果检查，替换不保证ABI/硬件兼容。下文相冲突的启动校验/禁止用户替换要求被本条取代。发布者制作的默认Runtime Pack仍按固定来源、哈希、签名与许可证审计；组件清单供用户查看，不作为软件加载锁。禁止将SDK/运行时/模型提交源码Git的规则不变。

> 2026-09-10 用户废弃旧 Loop：`loop/`、`scripts/loop-gate.ps1`、`CONTROL_HASHES.json` 及早期 Phase 排队/控制哈希停工规则全部退出当前执行流程，仅保留作历史记录。不得因旧 Loop 状态、STOP、控制哈希或旧文档顺序阻塞用户明确要求的修复，也不得更新旧清单来制造通过。当前任务以最新用户指令、对应修复计划和 `docs/WORKLOG.md` 为准。构建、针对性回归及必要的 `scripts/gates/delivery.ps1` 直接执行；单次测试最多 300 秒，并非全部测试累计 300 秒。运行时身份、源码/二进制隔离、真实验证与发布授权规则继续生效。

> 2026-09-07 新用户决定：默认明确标注的实时档（4K输入可用1080内部处理），保留原生4K可选；视频导出仍原生4K。不得要求以本机原生4K NR达到60fps作为本次默认档门槛，也不得把实时档冒充native4K。最终统一软件短测为 `scripts/gates/delivery.ps1`（phase5–7本次合同的合并检查），实卡仍由用户验收。其他安全/许可规则不变。

> 2026-09-06 用户授权接管修订：当前推进、五分钟短测与用户实卡验收以 `docs/ACTIVE_DELIVERY_PLAN.md` 为准，取代下文旧的严格串行施工/30分钟测试/未接设备阻塞全部交付规则。历史记录不是当前通过证明。
> 2026-09-09 用户发布决策：Veyra 采用“完整开箱即用的实验 Runtime Pack”发布策略。用户明确接受该 Pack 可能被 GitHub 下架或被权利方要求移除的风险，并授权将经过身份校验的指定实验运行时作为 **GitHub Release 资产** 随 Veyra 用户包分发。该例外不允许把 NVIDIA SDK、运行时、模型、头文件、样例或压缩包提交进源码仓库、Git LFS、Git 历史或 C++ 源文件；不允许修改、重签名、伪装或从游戏/驱动缓存提取二进制。详情以“Release Runtime Pack 规则”为准。

本文件对在本目录工作的所有 Agent 生效。不要只扫标题；开工前必须完整阅读：

1. `README.md`——当前状态和唯一入口；
2. 当前任务对应的 `docs/*PLAN*.md`——当前施工方案；旧 `VEYRA_AGENT_EXECUTION_PLAYBOOK_V1.md` 仅作技术契约与历史参考；
3. `VEYRA_PRODUCT_SPEC_V1.md`——产品与技术边界；
4. `docs/COMPETITOR_AUDIT_2026-09-03.md`——竞品事实、许可证和可迁移启发；
5. 当前阶段的 `docs/WORKLOG.md`——已完成、失败记录和下一步。

旧 Loop 不再要求阅读、同步或运行。最新用户指令优先于历史方案；仍适用的技术、运行时身份和许可证约束以本文件为准。

## 不可擅自改变的决定

- V1 是 Windows x64、C++20、Win32、D3D12 项目。
- V1 直接调用 NGX；不要同时接入 Streamline。Streamline 仅作未来替换方案和文档参考。
- 固定帧 harness 分两步：Phase 1 直接生成 RGBA8 Proxy → Feature 18 → Raw 抓帧；Phase 2 才加入 Original → Parity Encode → Feature 18 → Parity Decode。不是先做完整播放器。
- `nvngx_dlssnr.dll` 是 DLSSNR 运行时；只从项目根目录的已知文件复制到 `runtime_local`，不得联网寻找“更新偷跑版”、不得修改、重签名或提交 Git。
- NVIDIA 已正式发布 DLSS 5 消费者功能，但公开开发包尚未提供可直接替换当前路径的通用接口。用户于 2026-09-06 决定继续用固定身份的实验 Feature 18 做本机研发；这不证明画质与官方游戏/Magpie 等价，也不授予分发权。不得以“官方已上线”为由从游戏目录或驱动缓存抽取新 DLL。
- `renodx-dlss5-1.addon64` 是未签名的 ReShade/RenoDX 二进制 add-on，不是配置文件。它只准用于隔离的参考/对照环境，最终程序不得加载、注入、链接或随包分发它。
- 最终主线不依赖 ReShade。所谓“类似 ReShade”指独立复现它的前后颜色传递、参数映射和输出处理。
- V1 必须同时完成采集卡实时增强、DLSS 5 播放器、图片/视频增强导出。删掉其中任何一条都属于擅自改产品，不得用“以后做”放行。
- 三个入口共享 `FrameSource -> EnhanceGraph -> FrameSink`；禁止复制三套 NGX、颜色、Guidance 或 reset 逻辑。
- V1 是首发产品，不是最小 MVP。必须保证 4K SDR 全链路：Windows DirectShow/UVC 1080p/2160p 30/60、最高 3840×2160 H.264/HEVC 播放、D3D12 NVENC H.264/HEVC 视频导出、PNG/JPEG 图片导出、DLSSG 2X。HDR、3X/4X、厂商私有采集 SDK、AV1/ProRes 与 VFR 原样输出仍不是本次承诺，不能把它们混同为“4K”。
- V1 的质量主线是 NVOF motion + 置信度门控 + 可选 Depth Anything V2 Small；Zero Guidance 只允许作为诊断/回退，不得作为“高质量完成”。
- 采集卡/普通视频不含游戏引擎原生 depth、motion、exposure、camera matrices 或 HUD-less color。任何 Agent 都不得宣称估算输入“等同原生”。
- DLSS SR（若启用）在 Feature 18 前；DLSSG 在 Feature 18 后；播放器 OSD/字幕在 DLSSG 后合成，避免 UI 被插帧扭曲。
- 采集卡的固有延迟不等于可用 lookahead。若要生成 A 与 B 之间的帧，必须等 B 真正到达；额外用 C 做一致性验证还要再等一个源帧。不得把采集卡内部/驱动缓冲宣称为“免费未来帧”。
- 已接受 Magpie Experimental 对“可调用”的可行性证明，不再重复市场可行性争论。但 Create/Evaluate、真实输出、颜色、guidance、时序、延迟、稳定性及相对画质仍必须工程验证。

## 二进制身份，任何不一致都立即停工

```text
nvngx_dlssnr.dll（默认位置，2026-09-29 起）
  SHA256: F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC
  Size:   165840496 bytes
  Version: 310.8.3.0 (community Lecram)
  Signature: HashMismatch (NVIDIA certificate over modified content)

nvngx_dlssnr.dll（NVIDIA 原版，回退备份）
  SHA256: E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
  Size:   165840496 bytes
  Version: 310.8.0.0
  Signature: Valid, NVIDIA

renodx-dlss5-1.addon64
  SHA256: 837B6A34D41C0EB75CB105AFEB5B985CFC72CB7F3A786C5DBB3F5415C45C978F
  Size:   359424 bytes
  Signature: NotSigned
```

如果 hash 不同，不要“先试试看”；记录实际值并询问用户。
## Release Runtime Pack 规则（2026-09-09 用户授权）

这是一项面向用户体验的、明确标记为实验性的发布决策，不是 NVIDIA 官方合作、官方支持或通用再分发授权。

- **源码与 Release 必须物理分离。** 所有 NVIDIA SDK/runtime、模型、头文件、`.lib`、样例、SDK ZIP、驱动文件和本机抓帧必须保持在 gitignore 的本地目录；不得提交、vendoring、Git LFS、嵌入资源、编码为 base64 或以任何方式混入源码/提交历史。
- **仅 Release 资产可带二进制。** 用户批准的完整包可从本机受控 staging 目录复制已批准运行时；每个文件必须在 `release-runtime-manifest.json` 中列明文件名、来源类别、版本、大小、SHA-256、签名状态、实验标记及是否可卸载。未列明的 NVIDIA 文件一律拒绝打包。
- **当前 DLSSNR 许可范围。** 仅允许身份与本规则上方完全一致的 `nvngx_dlssnr.dll`（310.8.0.0、指定 SHA-256、NVIDIA 签名有效）进入 `runtime/experimental/`。哈希、版本、签名或来源不一致时停止打包并询问用户；不得联网搜寻“更新偷跑版”。
- **不得篡改或隐藏。** 禁止 patch、重签名、修改 PE、伪装为 Veyra 文件、嵌进资源、从游戏安装目录/驱动缓存复制，或借助 RenoDX/ReShade 注入路径实现发布包。实验 Runtime Pack 必须以清晰目录、版本与哈希呈现。
- **运行时保护。** Veyra 启动时校验 manifest、SHA-256、签名和 GPU/驱动兼容性；校验或初始化失败时只禁用对应增强功能，基础播放器、采集和导出仍可启动。不得静默下载未知二进制，也不得把失败伪装为增强已生效。
- **发布说明。** Release Notes、README、软件内组件页必须写明“实验运行时 / community experimental”，不得宣称 NVIDIA 官方合作、认证、支持或完整官方 DLSS 5 集成；同时保留 Veyra 自身版本、runtime 版本、哈希和卸载说明。
- **范围最小化。** 用户包只包含运行所需 DLL/模型与适用许可证、notice、manifest；绝不包含 SDK 开发文件、样例、私有测试媒体、日志、PDB、LIB 或游戏文件。官方 SDK 中明确可集成分发的组件仍须逐项保留其许可证与来源记录。
- **DLSS-G 310.9.16 性能模式运行库（2026-10-14 用户授权，具名例外）。** 用户在对话中选择“打包进运行库”，允许把 NVIDIA App 11.0.9 OTA 下发、存于 `%ProgramData%\NVIDIA\NGX\models\dlssg\versions\20318480\files\160_E658700.bin` 的 `nvngx_dlssg.dll` 原样放入 `runtime/experimental/dlssg-lite/`：310.9.16.0，7,506,544 bytes，SHA-256 `CADEE1AA6DB3907BABDC3285DA2AEBC4553D92B89C38BA3EAE8ABD8BBA1BEA72`，Authenticode Valid（NVIDIA Corporation）。已核对身份后放在 `E:/项目/Veyra/releases/runtime-staging/dlssg-lite-310.9.16/`（附 `runtime-lock-row.json`）；2.0.x 打包把该行加入版本运行库锁/供体清单，`package-portable.ps1`（2.0.8 起）也校验同一身份。例外只覆盖这一个身份：版本、哈希、签名不同的文件，其它 OTA 模型或驱动缓存文件都不适用，需要重新询问；push/Release 仍需另行授权。
- **人工发布。** 任何 push、GitHub Release、Runtime Pack 上传、替换或删除都必须由用户在当前对话明确授权；无人值守 Goal 仍禁止发布。若平台或权利方要求移除，先移除/下架 Runtime Pack，再保留不含该 Pack 的 Veyra 基础包。

## 当前修复验收

当前修复按用户请求和当前方案推进，不再按旧 Phase 0→7 排队。每次交付必须同时满足：

1. 代码可构建；
2. 本次改动相关的自动或手工检查通过，未执行项明确报告；
3. `docs/WORKLOG.md` 写明命令、结果、日志路径、失败与修复；
4. 没有把 proprietary runtime 或本地 SDK 提交进版本控制；
5. 后续改动没有建立在未验证假设上。

历史 Phase/checkpoint 只证明当时的测试结果，不代表当前产品已经验收。当前状态按本次构建、回归、实卡和发布证据分别报告；不得拿早期通过或失败标签替代当前事实。

## 历史 Loop

旧循环状态与 Phase 记录只代表当时证据，不是当前任务队列或通过证明。保留历史文件供追溯，不再据其自动推进、阻塞或发布。工作记录统一更新到 `docs/WORKLOG.md` 和本次修复文档；未经实际独立复核不得声称有 Reviewer 验收。

## 实现纪律

- 先检查现状：`git status`、`rg --files`、hash、工具版本。不要覆盖用户已有改动。
- 每次只实现当前阶段最小闭环，不顺手重构整个工程。
- `tools/*_probe` 只能组装并验证产品库。禁止把核心实现永久堆在超大 `main.cpp` 中；当前约 3600 行的 `player_probe` 必须迁移到共享 engine，不能复制到 UI/Capture/Export。
- 任何 NGX/NVOF 返回值、HRESULT、SEH、资源尺寸/格式和 GPU timestamp 都必须进入日志。
- 所有历史型模块在 open/seek/resize/pause-resume/scene-cut/device-lost 时显式 reset。
- 正常播放、采集和最终视频导出路径禁止 GPU→CPU 像素回读、每 pass CPU fence wait、无界帧队列和多份隐式颜色转换。视频导出必须用 Video Codec SDK 的 D3D12 NVENC input/fence；raw pipe 只准作诊断 fallback，不能通过首发 gate。
- 采集 ingress 使用 latest-frame mailbox（容量 1）并丢弃过期帧；图内部另有严格上限的 A/B/C history window。低延迟 FG 需要 A/B 的一帧 lookahead window；这不是可伪造为固定 `1/f` 的实测显示延迟。高质量实时模式最多再保留 C 做验证。实时文件/采集**预览**在持续欠速时可按真实 PTS 跳过已解码源帧的增强/呈现机会（2026-09-11 修订，仅限预览；跳帧须均匀覆盖时间线且计入诊断）；视频导出与图片/暂停单帧仍不得丢源帧。
- 所有来源先显式解析 range/matrix/transfer，进入统一 linear working texture；所有 sink 只做一次明确的输出转换。
- Guidance motion 固定为 current→previous、单位为 post-SR `workingExtent` 像素；depth 为 R32F 相对深度；confidence 为 R8_UNORM。低置信度区域必须衰减/清零 motion，不能把坏向量硬塞给 NGX。
- seek、scene cut、PTS discontinuity、capture drop、resize、source switch、pause/resume 和 device lost 必须原子 reset SR/NR/FG/depth/flow 的全部历史。
- 创建 Feature 可以等待一次；逐帧执行 1080p 用 3–4 个、4K 用 4–6 个 command slots/fence values 轮转，不能每帧或每 pass `WaitForSingleObject`。
- NGX 参数名和参数类型必须逐项照 Playbook；不要凭名字猜 `int`/`uint32_t`/`float`/resource。
- 所有 DLL 用绝对路径、`LoadLibraryExW` 和受限 search flags 加载；禁止依赖当前工作目录搜索。
- 不允许把 `renodx-dlss5-1.addon64` 改名为 DLL，不允许尝试从中 `GetProcAddress` 当普通库调用。
- 不允许在磁盘上 patch `nvngx_dlssnr.dll`。调用方兼容层必须独立封装、运行时可关闭，并明确标记 local experimental only。
- C++ 层保持 RAII；逆序释放 Feature、parameter block、runtime、NGX Core、D3D12 resources。
- Shader 先独立测试。颜色结果异常时先查 transfer、range、resource format、subrect、state barrier，别先调“画质参数”。

## 许可证与分发红线

- Magpie（含 `SAOG0721/Magpie` experimental fork）与 OptiScaler 均为 GPLv3，Veyra 自身也是 GPLv3，**可以直接移植其源码**；义务是逐项标注来源、固定提交、许可证与改动，并随源码/对应源码包提供。禁止只做机械改名而不标注来源。第三方 MIT 项目（RTX40MFG-Unlock、MFGAdaUnlock-RenoDx、dlssg_for_sm86 等）同样逐项标注。
- `video2dlssnr` 当前仓库未提供许可证；不得复制代码。`DLSS5-Feeder`、`dlss5-infinity-studio` 和 `dlss5-visual-enhancer` 只能按各自许可证与第三方 notices 取用；默认只借鉴公开行为与架构，任何代码复用都要在 `THIRD_PARTY_NOTICES` 逐项归因。
- NVIDIA SDK/runtime 的分发权不得想当然地扩大。除“Release Runtime Pack 规则”列出的、用户明确批准并完成身份校验的 Release 资产外，默认只做本机研发；`runtime_local/`、`third_party_local/`、抓帧和 SDK 压缩包必须 gitignore。
- NVIDIA Video Codec SDK 头文件/sample 同样需要单独接受 EULA；系统 `nvEncodeAPI64.dll` 不复制进用户包。没有完成第三方分发审计前，不得把产品宣传为 NVIDIA 官方认证、合作或支持。
- 已批准的 Runtime Pack 例外只适用于 GitHub Release 用户包，不适用于源码仓库、Git 历史、CI artifact、第三方镜像或私下发送未经 manifest 校验的二进制。
- 任何 Runtime Pack 的新增文件、版本替换、push、GitHub Release、上传或删除都必须先在当前对话得到用户明确授权，并报告来源、SHA-256、签名状态、许可证文件和包内扫描结果。

## 每次交付必须报告

- 当前修复任务与完成门槛；
- 修改的文件；
- 实际运行过的构建/测试命令；
- Create/Evaluate 或失败码的真实日志，不得用“应该可以”代替；
- 已知风险和下一条唯一任务；
- 若没实际在 RTX/NVIDIA runtime 上执行，明确写“未执行”，绝不能声称成功。
- 长任务必须在当前修复文档保留可续接的状态、证据与下一步；不再同步旧 Loop 状态。

## 禁止用假完成糊弄

以下均不算完成：只写接口桩、只增加成功 counter 而实际工作仍由 harness 执行、只编译未运行、用 manifest 代替 provider、重复运行 1080p 冒充 native 4K、仅显示理论 FPS、把 Raw NR 直接展示却声称完成 parity、以 Zero Motion 冒充 NVOF、以重复/线性混合帧冒充 DLSSG、仅枚举采集卡却没持续显示、仅导出图片却声称视频导出、丢音轨/时间戳不报告、或捕获到黑图仍把返回码 0 当成功。
