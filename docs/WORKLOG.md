# 当前工作日志

## 2026-11-03 RX7000补丁后worker退出：新日志与诊断更新

用户veyra-qml(4).log与cache(1).zip表明NVAPI身份修复确实生效（m_gpuArch0x1b0），新失败是12:35之后三次worker管道中断。NGX已进入Small1920×1080网络建立，最后权重heap请求108.6MiB；不能凭末行认定显存不足或具体CUDA错误。当前只更新worker诊断：阶段/请求参数、CRT与Win32 stderr、Rust backtrace、普通未处理SEH独立记录；保持原Windows异常处理、不吞异常。算法/模型/共享GPU同步/主EXE/设置不改，附只读Windows1000/1001事件收集，不包数据库或模型。

构建23.1秒、最终增量4.2秒通过，task codex-rdna3-crash-diagnostics-20261103；最终CPU诊断14项、GPU协议54项通过（不是AMD推理），收集脚本5.1实际查询事件及输出内容/哈希验证通过。重跑旧证据目录一次被合同拒绝exit2，改独立目录；收集脚本Get-FileHash解析失败后改.NET SHA256，失败日志保留。命令/详细结果见docs/RDNA3_WORKER_CRASH_2026-11-03.md，reports/rdna3-crash-diagnostics-20261103/FINAL.json。本地诊断包test-packages/codex-rdna3-crash-diagnostics-20261103，优先收集既有崩溃事件，不让用户反复变更参数。源码/依赖/正式包原件和用户配置保持，未合main/push/Release。尚未修复新的网络创建崩溃。

## 2026-11-03 RX7000 cache.zip续查与多GPU映射修复

用户补充cache.zip，52份worker日志相同，NR创建前24GB显存可用、缓存覆盖gfx1100，NGX明确Unsupported GPU architecture 0x0/min0x1b0且m_gpuArch=0xe0。固定ZLUDA NVAPI源码硬编码CUDA设备0，而Veyra按独显上下文运行。使用正式包同SHA转发层及NVAPI实际复现“核显0/独显1”误报核显LUID。分支codex/rdna3-nvapi-luid-20261103基线ecfbb0f，重编译同源开源NVAPI，改用当前上下文设备并显式拒绝歧义/查询失败；不改模型/驱动/原件，未使用二进制补丁。

首次原源码Cargo构建48.5秒，修复2.3秒，从原源码重新套patch的正式构建脚本2.33秒通过；每次并发2，上限880秒，CARGO_HOME/target/TEMP全在E盘。最终DLL215040字节/a7adb043…。13组合同旧8失败、新13通过，双线程64次设备隔离和原NR架构身份保持；CPU夹具不冒充实卡推理。新旧正式两NVAPI原件SHA已与2.1.2 AMD ZIP比对一致。源码/许可证/构建脚本/夹具/来源记录入Git；产品补丁只一个开源DLL，夹具nvcuda.dll绝不随包。未动原app、用户配置、依赖原件、正式ZIP，未合main/push/Release。命令、边界与最终包/清理回执见docs/RDNA3_NVAPI_LUID_2026-11-03.md及reports/rdna3-nvapi-luid-20261103/FINAL.json。后续等该用户实卡确认NR是否成功创建及推理。

## 2026-11-03 RX7900 XTX的NR初始化失败（只读排查）

用户提供veyra-qml(3).log并确认软件能打开、NR开不了。原日志SHA256 d92fabe719b9c33bf287dc0e81d89b5813e49b2d0413164351ec42c8dc98f8ef，共5381170字节。三个启动会话中52次NR创建均失败，首次UTC10:36:08.991、最后12:00:52.234，全部内部1920×1080；错误为NVSDK_NGX_CUDA_CreateFeature 0xBAD00001（NVIDIA定义FeatureNotSupported）。主程序随后明确回退nr=false；未出现RDNA3 worker ready。不能把错误号等同显卡永久不支持，亦无显存不足证据。

核对Rdna3NrWorker/Backend及固定RedDuke network源：HIP加载、指定D3D12设备/共享预检和network.load/can_share均在CreateFeature之前，主日志只记录子进程返回的最终错误；具体拒绝条件应查软件目录runtime/amd-nr-rdna3/cache中的worker-*.log、gate.log及NGX日志。未提供这些文件，故本轮不猜测修改兼容层/驱动/模型参数，也未构建或运行GPU测试。只读证据回执reports/amd-rdna3-startup-20261103/FINDINGS.json（约17KB），含52个行号及分辨率上下文；原日志、配置、运行组件和源码保持，待补子进程日志继续定位。官方定义来源https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h。

## 2026-11-03 NVENC导出关键帧审查与修复

用户要求先审第三方报告再修。隔离分支codex/nvenc-export-keyframes-20261103，基线edad6e9，保留全屏/VFG实验改动，无main合并/push/Release。真实270帧H.264/HEVC MP4均复现仅首帧索引，HEVC另有2个CRA；preset实际IDR周期4294967295。仅覆盖当前codec idrPeriod=原GOP120并日志记录，不把非IDR I帧伪装成安全跳转点；码率/HDR/slot/fence和实时录制不改。普通copy重封装旧文件实测仍仅首帧索引。

构建scripts/acceptance/build-recording-handoff.py，task codex-nvenc-export-keyframes-20261103、既有build-dir、label2.1.2-test、jobs2，probe和主程序6.5秒通过。新增scripts/acceptance/verify-export-keyframes.py：修改前两用例预期失败，修改后12组SDR及2组HDR10通过，3780帧、28次中/末段跳转逐像素与完整解码一致，MP4原始stss=[1,121,241]、码流3个IDR。首次合成HDR素材未声明PQ导致两断言失败，只修正测试生成及输入校验后补测两HDR组；失败保留。全部命令、源码审查和结果见docs/NVENC_EXPORT_KEYFRAMES_2026-11-03.md及reports/nvenc-export-keyframes-20261103/FINAL.json。日志/测试/临时/补丁均在E盘同名任务目录，未动真实配置或原运行库，未占采集卡。空间和覆盖包身份以FINAL回执为准，不冒充用户原片或其他硬件验收。

完整旧日志：[截至本轮整理前的记录](history/worklog/WORKLOG_before_maintenance_2026-10-27.md)。当前状态见[CURRENT_STATUS](CURRENT_STATUS.md)，历史授权见[历史索引](history/INDEX.md)。

## 2026-11-05 RTX 视频超分三个版本与双重超分（Claude，未提交）

分支 `claude/vsr-rife-20261105`（基于 main `8eda262`，独立工作树），未提交、未合并、不 push/Release。记录见 [RTX_VIDEO_SR_FAMILIES_2026-11-05](RTX_VIDEO_SR_FAMILIES_2026-11-05.md)。

- 第一轮加入了 Video Effects VideoSuperRes 和 RIFE。用户实测 RIFE 在 4K 比 VFG 高档贵得多（本机 4K 每源帧 +32 ms，VFG 高 +13 ms，中 +7 ms），且 30 系 VFG 兼容已在 `codex/vfg-ampere-20261102`，按用户要求整个删除 RIFE，补帧代码回到 main 原样。
- 超分算法改为 RTX 视频超分 · 标准 / 高码率 / 流媒体，各自选档位（低中高超高，流媒体只有中和超高），原 1234 改为低中高超高。
- 双重超分和超采样适配三个版本，第二次超分可选不同版本；每次超分各自走 NGX 或 Video Effects，第一次不放大时直接复制。VideoSuperRes 必须用阻塞 CUDA 流（第一轮发现并修复的错帧）。
- 验证：导出 9 组（含四种双重运行库组合）全部逐帧对齐；实际界面新夹具 15 项、原双重夹具 21 项通过；新单测 240/0，VFG 设置 683/0，QtQuick 132/0/1，补帧集成两种模式通过。
- 未做：打包清单、其他显卡、HDR 内容；RIFE 下载与依赖约 1.6 GB 仍在本机，待用户决定是否删除。

## 2026-10-27 屏幕捕获帧率、采集对白与后续方向（Claude）

用户要求：屏幕捕获60帧只有55（日志67）、采集对白很轻（日志68）、后续方向只调研。分支claude/capture-fps-audio-20261027从main 7b0b64b；用户Veyra运行中，未停止、未占采集卡；不push/Release。报告[CAPTURE_FPS_AUDIO_REPORT_2026-10-27](CAPTURE_FPS_AUDIO_REPORT_2026-10-27.md)，证据reports/claude-capture-fps-audio-20261027。

- 屏幕捕获6376564：WGC默认MinUpdateInterval 16ms使165Hz下60帧只收55（日志67六个目标均止于55.0，间隔皆18.18ms倍数）。改为上限间隔一半减1ms；限速容差0.5ms→四分之一间隔；每秒HDR检查改缓存factory（原中位0.43ms/最长21.8ms）。探针60.00fps源：默认52.5–55.1，修复60.2；真实程序2.1.1为53.55/53.72，修复后60.0/59.99（60/120上限）。screen_capture_tests与main同一通过集（6项HDR桌面颜色检查原已失败）。
- 采集音频75444b3：G50仅双声道USB音频，PS5多声道LPCM时中置（对白）在卡内丢失，OBS同样；强制PCM不改PS5输出。通用16-bit立体声DirectShow引脚加PcmCarrierRouter，复用AverMediaAudioCarrier解码载波内AC3/E-AC-3/DTS（PS5位流），真PCM原路径不变。单测真实AC3 5.1中置有信号、无载波字节作PCM播放；carrier 39363、vendor 65+7、passthrough 26+8+8、IEC、采集音频（静音）通过。未上实卡/PS5。
- 调研：建议去色带、独立RCAS、对白增强优先，CRT光束模拟再评估；NR沿用10-26结论；DLSS RR（310.9.1 Preset F）需游戏G-buffer，不适用于视频/采集。

- 快捷键：截图默认Ctrl+S→F7，新增录制F8，可在设置改；游戏模式钩子扩为面板/退出/截图/录制四槽，仅在游戏或Veyra在前台时接管。老配置已占F7/F8时保留原功能、新项未设置。game_passthrough/UI合同/翻译/QtQuick及私有配置启动检查通过。

- 60→30：采集回调画面编号用于相位，连续3次第二份即挪到第一份；60帧/不规则内容取帧不变。实卡T911+PS5 30帧游戏：702保留帧中第二份19（2.7%），挪动5次。驱动缓冲：建议1/3均被无视（实际10），送达均匀无排队，缓冲不是延迟来源；T911 P010 1440p60时间戳到回调约77ms且曾跌到59.54帧累积，SDR建议NV12（需慢动作实拍确认）。

构建build-recording-handoff --task claude-capture-fps-audio-20261027（首轮--reconfigure 79.4秒，其后增量）。未验证：用户165Hz实机与游戏模式、G50位流透传、无该属性的旧Windows。

## 2026-10-27 项目整理1–4

用户授权执行全部四项：清退休产物、收拢分支/工作树、文档归档、按职责整理桥接与QML并验证。分支codex/project-maintenance-20261027，main基线7bf02d4，当前正式版仍2.1.1；不push/Release，Lecram暂停。

- 保全：全部609refs、101工作树状态；6脏树516状态原字节ZIP逐SHA核验（70,537,460字节），完整Git bundle 77,589,296字节通过verify且覆盖原引用。
- 产物：43退休目录/54,827文件，保留12,160份脚本/配置/日志/源码及应用EXE；按SHA对象去重的证据ZIP341,177,909字节，复核通过。先记录并剪枝旧runtime junction，再只unlink其入口，不递归目标。
- 执行：cleanup_native.ps1核对绝对路径/父链/进程、所有文件ID/mtime/size和链接目标；删除43旧目录。E卷空闲增加24,123,240,448字节（22.47GiB），与本轮归档成本分开报告。
- Git：retire_native.ps1重新核对HEAD/状态/ignored及bundle恢复tip，安全移除89干净工作树，122分支引用归入已验证bundle。6脏树/暂停线保留；远端refs、发行标签和共同Git库不动。
- 文档：旧日历式AGENTS/CURRENT_STATUS/WORKLOG移入history，历史计划/报告/发行过程分类并重写Markdown相对链接，旧路径映射可查；历史guard/start不改。恢复用户2026-10-20的NV/AMD纠偏，不拿旧状态当待办。

本轮全部原始证据：`E:/项目/Veyra/reports/project-maintenance-execution-20261027`；恢复归档：`E:/项目/Veyra/archives/project-maintenance-execution-20261027`。

代码整理提交`d4b1034`：桥接主文件9233→640行，14个领域CPP与私有状态头；对话框2277→134行；专业页3213→2144行，独立音频/显示/性能组件。公开头和引擎/管线/采集/音频/着色器未改；681个方法体、QML文案/objectName、2681条翻译内容一致。

构建52.7秒、后续10目标23.2秒通过；16组检查通过，QtQuick117/0/1跳过。四次真实软件/GPU/兼容启动、两种渲染器下十对话框/五页签/37%预设往返通过；软件/GPU文件播放各290实际呈现帧。View首次共享GDI初始化声明缺口已修为唯一实例；自动迁移误改三条对象名被结构检查拦下并恢复，最终复验通过；Playlist初次夹具参数错误exit2保留JSON，按合同改用新目录后通过。

611历史文档/规则归档并修正相对链接；当前和历史Markdown链接目标检查无缺失。新开发入口docs/README.md、DEVELOPMENT.md、ARCHITECTURE.md；历史guard/start保持，旧运行产物链接标注退休路径。

收尾保护检查：516条脏状态对应文件原始字节全部一致，保留树HEAD/status一致，2.1.1正式两包/源码SHA一致，两个正式运行供体全文件一致，测试运行组件179文件身份保持；461发行/检查点标签和远端refs未改。E卷空闲相对开工前净增约28.14GiB，最终值以FINAL.json为准。未重复真实采集或全硬件推理/性能验收。

最后本地整合main并关闭施工分支；不push、不Release，不修改真实配置、驱动、SDK/模型原件或暂停的Lecram。

## 2026-10-27 GitHub交流群二维码更新

用户明确授权更新GitHub群码。仅发布新群7截图、README.md/README_CN.md及RELEASE_SUPPORT中的图片地址与有效期，2.1.1 Release正文同步替换为2026-11-03前有效；赞助图、两图width220、其他发布说明与正式资产保持。README_EN.md为指向英文首页的入口，无需改动。

远端main原9520946，二维码提交a983c18；连接器上传后逐SHA/PNG解码发现图片损坏，改用原文件字节通过Git修复提交26480b2，最终远端原图77616字节、SHA256 AEDA3DF086F9C0983055A4F73EB2BD9F422C03E5C1910AF603A6ED0E79F2BDF9，与用户截图完全一致。两次远端提交均基于远端main，只含二维码范围，未推送本地项目整理提交。

GitHub连接器用于文件树/提交/引用更新，缺少Release编辑接口时用gh release edit --notes-file；随后合并远端二维码改动回本地main。verify.py核对远端三文档、原图字节与PNG解码、赞助图访问、两图宽度、Release正文仅群码/日期变化，全部通过；不涉及产品代码，无需重建。证据、原正文与最终回执：E:/项目/Veyra/reports/github-group-qr-20261027/。真实配置、依赖和正式包未改。

## 2026-10-28 单层RTX三段式超分接手完成

用户要求接手Claude额度用尽前未完成的功能。阅读原会话确认最后范围为单层RTX视频超分“先降到→超分到→再缩到”，沿用claude/sr-stage-20261028，基线7b0b64b；不合并main、不push、不Release，不把独立capture-fps-audio分支混入。保全33个改动文件/patch至archives/codex-sr-stage-handoff-20261028并逐SHA验证，起点标签checkpoint/pre-codex-sr-stage-handoff-20261028。

复查新增真实GPU边界测试，先复现4K原生NR先行→1080p先降→2K输出被旧NR≤work边界错误拒绝；修为按NR实际所在阶段nrFullExtent校验，保留逐层校验，修复后6帧处理通过。独立导出共享参数布局改变，协议11→12，补两个实际worker/NVENC任务验证新字段、执行尺寸与完整帧数。

构建：复用tmp/claude-sr-stage-20261028/build/build.py，MSVC/Ninja并发2，产品/相关测试目标均构建通过；主要增量26.8秒、后续6.9秒/3.1秒。运行目录复用tests/claude-sr-stage-20261028/app，通过新文件替换EXE/QML，不写穿供体链接。

本轮run.py记录：六组unit/contract/QML退出0（RepairContract272项0失败）；GPU stage-boundaries、stage、stage-capabilities、原单步SR四组退出0，新增24个实际VSR组合全部成功。GUI验收覆盖列表15项与节点4项，退出0、无QML错误；首次节点夹具未向空图添加SR而等待，停止本轮测试进程、保留失败记录，修正夹具后通过。两个实际导出各4源帧/4编码帧，ffprobe核对H.264、2560×1440及3840×2160。i18n extract --check缺失0、占位符0，git diff --check通过。

GPU首次runner的stdout路径重名，前组stdout被覆盖；各组exit/末尾标记仍在gpu.json，各组独立engine事件日志保留；runner已改用序号。未借机重复整轮测试。未做main二进制逐像素对照、长时动态画质/显存峰值或实卡采集；Claude原图片A/B与本轮定向验收分开陈述。

详细范围、命令、失败与验证见docs/SR_STAGE_HANDOFF_2026-10-28.md；本轮JSON/日志：reports与logs/codex-sr-stage-handoff-20261028。正式2.1.1包/SDK/模型/用户配置不改，Lecram继续暂停。

## 2026-10-28 三段式SR整机卡死后续防护

用户报告1080p先降/8K输出导致整机卡住后重启。新分支codex/sr-stage-freeze-20261028从cf84762开始，checkpoint/pre-sr-stage-freeze-20261028；旧测试构建不再推荐。只读保全实际hang dump、相关系统事件与会话参数，真实配置未改，不重跑原危险组合。

配置解码确认质量2 RTX视频超分、先降1080p、8K最终输出/不再缩、NR Ampere开启、VFG 2X开启。系统41证明非正常重启；重启后处理的旧日期141报告不能冒充本次证据。EXE无PDB，离线同OBJ/LIB重链接MAP的.text与旧构建逐字节一致，原始栈候选映射指向DXGI/PresentSink resize与VideoPresenter；线程当前IP在系统驱动查询/BitBlt。没有完整调用栈、当时显存值或对应内核dump，不把显存压力怀疑写成已证实根因。

防护：新三段式SR同时开NR/FG时，最终处理尺寸限制4K像素预算，保留8K内部评估。列表/节点参数在同一事务补齐再缩4K；旧会话恢复复制不可变节点文档后修正，明确提示；底层validateFixedExecutionPlan在分配/SDK前拒绝绕过UI的危险组合。首轮构建因写入const节点文档失败，改为复制后通过。原单步与纯SR规则保持。

复用既有MSVC/Ninja构建并发2，修复后产品和相关测试52.3秒构建通过。checks.py四组通过；EffectChain新增7项防护检查，QtQuick117/0/1跳过，i18n缺失/占位符0。sr-stage-safety-ui.py复制用户三个会话文件到私有profile，禁止自动恢复/预热，只开本地4K文件；7项断言通过，原会话SHA不变，NR/SR/VFG真实全开、内部8K/最终4K，日志无8K工作图，出帧约12秒后正常退出。未占用采集卡，也未复测原8K NR/VFG危险组合或声称长时稳定/底层驱动问题根治。

当前EXE在tests/claude-sr-stage-20261028/app原位置已更新；测试注入Main.qml恢复，97个运行依赖与正式供体逐SHA一致。诊断、离线工具和最终回执在reports/sr-stage-freeze-20261028，测试日志在logs同名目录；源文档docs/SR_STAGE_FREEZE_2026-10-28.md。无main合并、push、Release，不改SDK/驱动/模型/真实用户配置。

## 2026-10-28 真正双重超分纠偏

用户纠正了前述实现：要基础SR完成后降低其AI结果，再执行第二次SR；普通8K必须可用，第二设置放基础超分下的展开开关。新分支codex/dual-sr-20261028从67d0294开始，checkpoint/pre-dual-sr-20261028；本段覆盖上段单层语义和4K强制规则，保留旧故障证据。

实现独立第一输出RGBA8/FP16→线性面积降采样→第二RTX SR，两次Evaluate与质量参数独立；列表/节点共用开关及第二降低尺寸、50–90%比例、2K–8K目标、质量。默认关闭，旧先降/再缩字段不再改变基础SR，撤回4K限制与恢复时改参数。仅新双重图采用WDDM预算+SDK估算预留+20%余量准入，不静默缩小尺寸；普通单次沿用原预算规则。预设34/库17/会话12、独立.dual-sr旁路文件、导出worker协议13同步。

实际GPU首次双实例创建遇到FeatureAlreadyExists(0xBAD00003)，已保留gpu-create-key-before证据。按SDK返回码复用已有Feature，不取得/释放重复句柄；同一命令序列两次实际Evaluate、不同输入输出纹理，其他错误失败。本机一个SDK Feature执行两次，不宣称两个独立实例。补无效第二目标的计划拒绝，按创建逆序释放资源。

构建命令：python -X utf8 -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/build/build.py 后跟产品/相关测试目标，MSVC/Ninja并发2；完整核心209.6秒，后续增量111.8/95.2/23.3秒及小目标均退出0。日志在logs/claude-sr-stage-20261028/build-*。stage_app.py以新文件替换EXE/QML，不写穿正式供体链接。

验证：reports/dual-sr-20261028/checks.py cpu/gpu、finish_checks.py qml/export，以及scripts/acceptance/sr-stage-ui.py 本地1080p30文件 --task codex-dual-sr-20261028 --fixture dual-sr-ui.qml。三组CPU全部通过；GPU每帧2次、第二输入对第一次AI结果面积平均最大差1LSB、基础8K→1080p→第二4K、相同创建键、HDR恢复、关闭双重后的真正8K均通过。实际worker/NVENC H.264导出4源帧/4编码帧，ffprobe2560×1440/4帧。QtQuick117通过/0失败/1既有跳过；翻译2691条缺失/占位符0，diff检查通过。

UI真实运行核对列表与节点的展开/关闭、两次参数独立、第二8K不改基础4K、普通8K实际输出；最后4K双重+NR+VFG2X组合约12秒短测激活三效果、末段累计275源帧、显存4054MiB/WDDM预算11175MiB，正常退出，无QML错误。截图tests/codex-dual-sr-20261028/dual-controls.png，非截图模拟控件。

复验失败保留cpu-recheck-before.json及三个原日志：旧断言仍要求退休的srOutput触发重建，已按新产品合同改成不重建；测试runner重复用了旧故意损坏文件/旁路，改成每次唯一TEMP/fixture后全部通过。旧stage*诊断模式仅作历史，当前验收用dual模式。

交付沿用tests/claude-sr-stage-20261028/app；独立interactive-profile入口tests/dual-sr-20261028/启动双重超分测试.cmd，禁自动恢复/预热，不改原设置。AGENTS（活动树与E盘共享）、CURRENT_STATUS、文档索引及旧报告已标记最新纠偏。详细合同、命令与边界见docs/DUAL_SR_PLAN_2026-10-28.md，最终摘要/保护检查在reports/dual-sr-20261028/FINAL.json。

未复测原8K最终输出+NR+VFG危险组合，不宣称整机卡死具体根因已证实或驱动挂死根治。当前双重仅RTX视频超分，未扩为所有显卡/模型/实卡/长时画质验收。不复制新完整运行包，仅复用现有运行目录；新增小型测试、日志和证据实际逻辑字节见FINAL，未测本轮净物理占用/回收。正式2.1.1资产、SDK/模型/DLL/驱动不改，不合并main、不push/Release，Lecram暂停。

## 2026-10-28 双重超分复查与修正（Claude）

用户先要求检查Codex接手完成的情况，再要求在本分支修复超分卡片摘要，并删除或改写失效的旧stage测试和A/B工具。仍在`codex/dual-sr-20261028`（HEAD `e61e497`）工作区，未提交；不合并main、不push/Release。

复查：同一构建目录的产品EXE与FINAL记录SHA一致；CPU三组、GPU `dual`、真实界面13项复现通过。发现两处遗留：列表超分卡片摘要仍按退休单层字段拼接，RTX超分时总把保留的降低尺寸显示为“1080p→4K”（双重关闭时也显示），且缺第二目标/质量；旧GPU `stage*`模式、`sr-stage-ab.py`、`sr-stage-ui.qml`、`sr-stage-safety-ui.*`仍按退休的先降/再缩语义。

修正：ProPage摘要仅在双重开启且为RTX视频超分时显示“基础目标→降低尺寸→第二目标 · 质量 基础/第二”，删除“片源尺寸”词条。SrQualityTests删除`stage`/`stage-boundaries`/`stage-capabilities`/`stage-image`，保留`dual`，新增`dual-restore`、`dual-boundaries`、`dual-capabilities`、`sr-image`；`scripts/acceptance/dual-sr-ab.py`替代旧A/B；`dual-sr-ui.qml`加三项摘要断言和截图参数，`sr-stage-ui.py`传截图路径、默认媒体改为nr-1080p30.mp4。`sr-image`名称改为UTF-8输出（原宽字符逐字截断，有C4244警告）；旧实施报告的脚本提示加退休说明，DUAL_SR_PLAN补“复查修正”节。

命令：`py -3.11 -I -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/build/build.py <目标>`（并发2）重编veyra_qml_ui、i18n与测试目标，最后一次veyra_sr_quality_tests 5.4秒、0警告；`stage_app.py`以新文件替换运行目录。`tmp/claude-codex-review-20261028/run_tests.py`跑CPU/QtQuick/i18n，`gpu_final.py`用最终测试EXE重跑四个GPU模式；UI `py -3.11 -X utf8 -B -I scripts/acceptance/sr-stage-ui.py --task claude-codex-review-20261028`；A/B `py -3.11 -X utf8 -B -I scripts/acceptance/dual-sr-ab.py --mode native --image tests/claude-sr-stage-20261028/media/gta-4k-t780.png --tag gta-t780`（t95同）。

结果：产品EXE `542ad0b9…`、测试EXE `c00a86c9…`在构建与运行目录逐SHA一致。GPU `dual`、`dual-restore`（第一次1.2771、第二次1.2718，界1.319）、`dual-boundaries`（非整数降低最大差1LSB）、`dual-capabilities` 24/24全部退出0；EffectChain/PresetLibrary/RepairPreset通过；QtQuick 117/0/1跳过；i18n 0失败，`extract.py --check`缺失0（2690条）。真实界面16项断言通过、无QML错误，截图`tests/claude-codex-review-20261028/dual-controls.png`完整显示“RTX 视频超分 · 4K→1080p→2K · 质量 1/2”。ASCII与中文名称的`sr-image`各一次通过，A/B工具改动后冒烟通过。

过程失败：最初的HDR峰值判据不成立（恢复本就按钳位放大差值），改为对各自基础的恢复钳位界；摘要首版在窄面板被截断为“质量 1，…”，改为“质量 1/2”并去掉多出的词条；A/B裁切在组间同名覆盖，改加组前缀。均在最终结果前修正。

A/B（GTA VI原生4K两帧，全部质量4，完整表见DUAL_SR_PLAN“复查修正”）：两帧所有组双重的PSNR/SSIM都低于单次，如原生1080p t780单次43.38/0.9759/6.4ms、双重4K→1080p→4K 41.13/0.9673/13.4ms；梯度锐度除一例外高于单次，SR阶段GPU时间约1.2–2.9倍；放大裁切差异细微，双重1080p边缘更硬、暗边更明显。PSNR/SSIM只代表与原图的保真度（不超分/双三次高于所有RTX输出），不是主观画质结论。

追加对比（用户问双重是否有效、是否因中间分辨率过低）：`tmp/claude-codex-review-20261028/ab_extra.py`在同一运行目录用`sr-image`补跑75%/90%降低与单次超到8K，CPU线性光2×面积缩回4K；`ab_extra2.py`用浮点USM（高斯σ1）按梯度锐度匹配。结果写入`reports/claude-codex-review-20261028/ab/gta-t780|gta-t95/extra.json`：缩得越少越接近单次但都未超过，90%时SR阶段26.8–36.9ms；8K再缩回四组均高于单次、SR阶段只多1–3ms；单次锐化到双重同等锐度时四组都比双重保真。首轮用PIL UnsharpMask，100%以下整数截断小差值、匹配失效，结果作废后改浮点实现。

产物：回执`reports/claude-codex-review-20261028`（tests.json、gpu-final.json、sr-stage-ui.json、ab/、FINAL.json），日志`logs/`同名，A/B与GPU工作图`tests/`同名，任务脚本`tmp/`同名；收尾删除冒烟输出与测试缓存79,646,121字节，其余实际字节见FINAL。运行目录`tests/claude-sr-stage-20261028/app`的EXE/QML已是本次版本。

未做：未提交；`SrResolve`不会执行的再缩路径、尺寸日志`three-step prescale=`及旧注释未改；未重跑原8K最终输出+NR+VFG；未做视频动态、主观或多显卡画质评测。真实配置、SDK/模型/驱动、正式2.1.1资产未改，Lecram暂停。

## 2026-10-28 超采样选项“超分后缩回到”（Claude）

用户看过双重超分A/B后选择“做成选项”：先用RTX视频超分到更高尺寸，再精确缩回输出尺寸。仍在`codex/dual-sr-20261028`工作区，未提交；不合并main、不push/Release，不改真实配置/SDK/模型/驱动，Lecram暂停。

实现：`runningSrOutput`在单次RTX视频超分（NVIDIA、双重关闭）时恢复返回`srOutput`，执行计划与`activeVideoSettings`同步，GPU复用`SrResolve`线性光面积缩回，后续NR/补帧/显示按缩回尺寸运行。超采样图与双重图同样走WDDM预算+SDK预留+20%余量准入，纹理估算加超分结果RGBA8。列表/节点`SrStageEditor`新增“超分后缩回到”（不缩回/片源尺寸/2K–7K），双重开启时禁用并保留；实际尺寸与未生效原因移到编辑器底部，卡片摘要显示“8K→4K”。开启双重不再清空缩回选择。字段无格式升级（v33/16/11起已保存）；导出协议13→14。日志改为`supersample evaluate=… output=…`/`dual reduce=…`，三段式注释同步更新。

测试：EffectChain新增13项超采样检查，改写“双重关闭忽略再缩”旧断言；GPU新增`supersample`模式（SDR/HDR对同图8K结果的CPU面积平均、回到片源尺寸、非整数2K、双重忽略保留值）；`sr-image`增加缩回参数，A/B工具改名`scripts/acceptance/sr-ab.py`并加超采样用例；导出模式改为`sr-supersample`/`sr-supersample-2k`并核对worker日志；界面夹具加超采样步骤与截图，最后跑超采样+NR+VFG。

命令：`py -3.11 -I -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/build/build.py <10个目标>` 134.0秒、改提示后3目标2.0秒，均退出0（新警告仅EnhanceGraph.cpp既有C4189）；`stage_app.py`；`py -3.11 -X utf8 -I -B E:/项目/Veyra/tmp/claude-sr-supersample-20261028/checks.py cpu|qml|gpu|export`；`py -3.11 -X utf8 -B -I scripts/acceptance/sr-stage-ui.py --task claude-sr-supersample-20261028`；`py -3.11 -X utf8 -B -I scripts/acceptance/sr-ab.py --mode native --image tests/claude-sr-stage-20261028/media/gta-4k-t780.png --tag gta-t780`（t95同）。

结果：EffectChain 453、PresetLibrary 552通过，RepairPreset退出0；GPU `supersample` SDR最大差1LSB、HDR最大差0.031超差0，`dual`/`dual-restore`/`dual-boundaries`/`dual-capabilities`(24/24)通过；导出3840×2160、2560×1440、2560×1440各4帧（ffprobe）；QtQuick 117/0/1跳过，i18n 0失败、缺失0（2698条）；界面21项断言、无QML错误，超采样8K→4K+NR+VFG约12秒显存4029MiB/预算11175MiB。产品图内A/B两帧所有组超采样都高于单次（原生1080p t780 44.21/0.9799对43.38/0.9759，原生2K t95 46.52/0.9812对45.74/0.9781），超分阶段多约1.2ms。

过程失败：首轮界面截图显示“目标尺寸”说明换行6行，缩短两条提示并补翻译后重编复测；`checks.py`首版按项目FFmpeg目录找ffprobe不存在，改用PATH中已安装的ffprobe（同先前runner）。均在最终结果前修正。

未做：未提交；无长时播放、实卡、其他显卡验收，超采样+NR+VFG仅约12秒短测；原8K最终输出+NR+VFG未重跑。双重超分保留、默认关闭，是否撤掉待用户决定。详细见docs/SR_SUPERSAMPLE_2026-10-28.md，回执reports/claude-sr-supersample-20261028/FINAL.json。

## 2026-10-28 合入本地main（Claude）

用户明确要求“直接合并到Main即可”，覆盖此前各段“不合并main”。在`codex/dual-sr-20261028`把复查修正与超采样选项提交为一个提交，再以合并提交并入本地`main`（原`7b0b64b`，是该分支祖先，无分叉），沿用以往“Merge … for local integration”的做法；仓库没有启用的git钩子。活动树随后切回`main`，内容与已测试的工作区一致，无需重编。

并入内容：claude/sr-stage-20261028（cf84762）、codex/sr-stage-freeze-20261028（67d0294）、codex/dual-sr-20261028（e61e497）及本次提交。独立`claude/capture-fps-audio-20261027`未并入；施工分支保留未删。未push、未Release，正式版仍2.1.1，运行目录与真实配置不变。提交与合并哈希、合并后检查见reports/claude-sr-supersample-20261028/FINAL.json。

## 2026-10-28/29 英伟达低延迟三档与Reflex补帧接入

用户要求完善NVIDIA低延迟并命名“英伟达低延迟”，提供关/开/开＋增强。基线main4351bbd，隔离分支`codex/nvidia-low-latency-20261028`，存档`checkpoint/pre-nvidia-low-latency-20261028`。本轮不合并main、不push/Release。单Agent，复用既有构建和一份测试运行目录，所有新增证据/临时数据在E盘。

完成：独立三档配置和旧lowQueue迁移（旧开启仅迁移到开，不自动Boost；非法值关）；列表/节点共享控件；实际驱动状态、错误回退、XeSS提供方所有权说明及重启持久化。直接NVAPI原生SetSleepMode区分Boost，interval=0不另加限帧。源输入前Sleep并复用等待ID，增强提交后renderEnd；独立呈现队列注册带外工作，复制9/10和生成Present11/12使用公开SetLatencyMarker(queue)，真实Present4/5正常标记。生成帧不增加Sleep/模拟ID。暂停、模式切换、图重建和切源清理准备ID；关闭撤销Boost，失败不能假报成功。XeSS沿用自身XeLL调度。未引入Streamline或改动驱动、SDK/模型/运行DLL。

主要命令（均在活动源码树执行）：

- `python -X utf8 -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/build/build.py veyra_qml_ui veyra_presentation_pacing_tests veyra_repair_contract_tests veyra_ui_i18n_tests veyra_qml_quick_tests`；后续按变更仅重建相关目标，并发2/单次≤900秒。主要批次73.8/72.1秒，最终产品增量15.7秒、测试增量4.1秒退出0；原始MSVC/Ninja日志保留`logs/claude-sr-stage-20261028/build-20261028*.log`及`build-20261029-001739.log`/`build-20261029-002204.log`，不把既有编译警告写成零警告。
- `python -X utf8 -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/stage_app.py`：以unlink后写新文件更新EXE/QML，不写穿供体硬链接。
- `python -X utf8 -B E:/项目/Veyra/reports/nvidia-low-latency-20261028/run.py cpu|qml`；GPU用`gpu <档位> <倍率> <场景> [后缀]`，档位-1关/1开/2增强。场景native-plain、native-dlss-queue、native-dlss、native-vfg、native-fsr、native-xess、native-effects、native-lifecycle。最终组合及生命周期分别`gpu 2 2 native-effects final`/`gpu 2 2 native-lifecycle final`。每测试≤300秒。
- `python -X utf8 -B E:/项目/Veyra/reports/nvidia-low-latency-20261028/ui.py --retry`及`--retry --verify`：真实GUI/驱动、旧设置迁移与Boost重启恢复；`python -X utf8 -B scripts/i18n/extract.py --check`、`git diff --check`。
- `python -X utf8 -B E:/项目/Veyra/reports/nvidia-low-latency-20261028/finalize.py`：交互入口启动、源QML/翻译与EXE身份、供体与运行组件全核验。

结果：RepairContract293项0失败，QtQuick117通过/0失败/1既有跳过，i18n2704条缺失/占位符错误0；10个最终GPU用例退出0。真实RTX5070文件1080p30测试包括关/开/增强、DLSS2X/6X、VFG2X、FSR2X、XeSS提供方回退；最终NR＋SR＋DLSS组合21.03秒、生命周期20.94秒通过，组合源速0.988793、测量期间149处理帧。列表/节点/关后VFG继续/保存恢复真实GUI通过，无QML错误。162成功Sleep对应160真实/159生成Present为软件和驱动接口计数，不能冒充物理显示帧或延迟收益。

失败如实保留：初始AsyncFrameMarker返回-1（清内部字段仍失败），当场回退；改用NVIDIA公开queue目标SetLatencyMarker后9–12返回0，原两次engine.log保留。GUI初始夹具误用fg键，改现有vfg-fg后新配置复验通过。组合首次15秒首帧夹具超时、之后真实处理成功但整体仍记失败；只把该新增组合夹具等待改30秒后另目录通过，不修改产品或历史guard。清理脚本首版Split-Path参数集合不兼容、尚未删除即退出，改用IO.Directory.GetParent后通过。

保护及空间：EXE构建/运行SHA一致；QML/翻译全一致；97个运行组件与正式供体一致，正式供体1730个manifest文件全核验。GUI与最终交互启动前后真实配置SHA一致。只清本轮19个数字TEMP子目录，删除逻辑46,093,122字节，先核对绝对路径、父链/子项无reparse和进程占用；脚本、原始失败日志、配置、交互入口及运行目录保留。未测本轮净物理空间变化，收尾剩余任务逻辑字节见FINAL。

入口`E:/项目/Veyra/tests/nvidia-low-latency-20261028/启动英伟达低延迟测试.cmd`使用私有interactive-profile，初始关闭、不自动开媒体；启动冒烟退出0。运行目录仍`tests/claude-sr-stage-20261028/app`。回执/脚本`reports/nvidia-low-latency-20261028`，日志`logs/nvidia-low-latency-20261028`，截图`tests/nvidia-low-latency-20261028/controls.png`，执行细节`docs/NVIDIA_LOW_LATENCY_2026-10-28.md`。

未测30/40系持卡、采集卡、真实VRR/物理屏幕端延迟与长时稳定性；不重跑原8K最终输出＋NR＋VFG整机卡死组合。主线仍4351bbd，正式版仍2.1.1；本分支本地提交哈希与干净状态写入FINAL，不push/Release。Lecram继续暂停。

## 2026-10-29 英伟达低延迟复查修正与效果实测（Claude）

用户请Claude复查Codex的`4edcb49`后选择“修完再测效果”。修正提交到同分支，未合并main、未push/Release；真实配置、驱动、SDK/模型/DLL不动，测试全部用私有data-dir。

复查实测（修前，`reports/claude-nvidia-ll-review-20261029/ui-amd.json`、`ui-en.json`）：保存开＋增强后把运算显卡切到本机AMD核显，关掉旧“低延迟队列”后引擎仍`requested=false effective=true`；英文界面状态显示“英伟达低延迟：关 · Display sync: allow tearing”；专业页摘要只写“低延迟队列”。另从代码看出未补帧时带外队列通知失败会整段降级。DLSS 6X文件播放关/仅低排队/开/开＋增强各两轮（`fg-ab.json`）均180帧/秒、生成帧0过期，四档无可测差别。本机RTX5070在Veyra关闭时也停在P0约2730MHz，增强档在本机测不出时钟/功耗差别。

修正：`syncNvidiaLowLatencyQueue`统一低排队归属（英伟达上三档决定，其他显卡普通开关决定、英伟达选择只保留），`configurePresentation`只在英伟达适配器上折算；状态行改为无分隔符片段拼接并补17条翻译（`extract.py`→填写→`--check`缺失0）；摘要显示英伟达三档；未补帧时通知失败退回设备内标记。附带：关闭日志记录驱动Sleep耗时，测试程序新增`latency-screen[-sr]`屏幕采集场景。

构建（`tmp/claude-nvidia-ll-review-20261029/build/build.py`，并发2）：五目标78.6秒、增量16.4/4.4秒退出0；新增代码无新警告（日志中C4244来自既有PreviewView.h）。暂存：EXE、测速程序、1个QML、翻译以新文件写入`tests/claude-sr-stage-20261028/app`，97个运行组件与2.1.1供体manifest一致。

验证（脚本`tmp/claude-nvidia-ll-review-20261029/review.py`，每项≤300秒）：RepairContract 300项0失败（新增7项：归属规则4项、通知失败3项）；i18n 0失败（新增英文状态两项）；QtQuick 117/0/1既有跳过。真实GUI：AMD核显上关低排队后`requested=false effective=false`，英伟达控件显示“关”；切回英伟达重开后开＋增强恢复且驱动已启用；英文界面关/开/开＋增强在自动与垂直同步下状态无中文残留，摘要“NVIDIA Low Latency · On + Boost”。两次GUI前后原配置SHA不变、Main.qml复原，无QML错误。GPU回归9组（plain开/增强、DLSS 2X/6X、VFG、FSR、XeSS回退、NR＋SR＋DLSS、生命周期）全部通过；驱动Sleep平均约0.002毫秒、最长0.13毫秒。

效果实测（`review.py latency <场景> <倍率> 10 <显示同步>`，档位-1关/0仅低排队/1开/2开＋增强各两轮，窗口采集60fps代替采集卡；拆分脚本`latency_split.py`）：驱动Sleep平均约0.003毫秒；DLSS 2X窗口采集四档采集到呈现p50 16.87–17.03毫秒，SR 4K＋DLSS 2X 17.93–18.15毫秒，档位差别<0.2毫秒；GPU处理各档相同。无补帧窗口采集另发现与本功能无关的旧问题：呈现按首帧锚定，整场固定多等3.1–52毫秒（读到GPU完成<1毫秒），因此无补帧绝对延迟不用于档位比较。垂直同步DLSS 2X四档p50 16.87–17.05毫秒，同样无差别。未测采集卡、串流、30/40系与屏幕光电延迟。

## 2026-10-29/30 RX7000 NR接入

用户明确授权加入7000系。基线e7a9ae1，checkpoint/pre-amd-rdna3-nr-20261029；隔离分支codex/amd-rdna3-nr-20261029，不合并main、不push/Release。代码/运行协议/来源/许可/失败记录与完整命令读docs/AMD_RDNA3_NR_PLAN_2026-10-29.md，最终回执E:/项目/Veyra/reports/amd-rdna3-nr-20261029/FINAL.json。

复用RedDuke固定MIT网络层ab455d1、ZLUDA b135e23，每层独立worker，D3D12/CUDA原生共享纹理/fence；主进程不加载兼容替身。不使用ReShade/OptiScaler、游戏代理或旧帧残差。输入RGBA16F显示代理，网络每帧reset/零guide，Veyra原有HDR基底/残差/保护/历史合成保留；强度用解码混合，不随滑条重建Feature。RX7000和RX9000按真硬件选择不同后端，保存runtime数字ID4不变。Small性能模型和Original、25–100%手动内部比例在列表/节点独立保存。现有AMD1080p像素预算保持，未给7000虚开FSR4。

最终构建6.1秒退出0（logs/codex-amd-rdna3-nr-20261029/build-20261030-005622/compile.log）；CPU能力72、repair300、原lmxxf fake ABI66、GPU协议与parity54项均0失败；QtQuick123通过/0失败/1既有跳过；设置/导出冻结兼容和i18n2712条通过。真实GUI保存与重启通过，无QML错误，原用户配置SHA未变。复用原NVIDIA runner测NR+SR+DLSS2X+Boost，22.47秒通过，149处理帧/源速0.987573，回执真实位于reports/nvidia-low-latency-20261028/gpu-native-effects-2-2-rdna3-regression.json。失败夹具/编译记录完整保留，未篡改历史验收。

本地复用tests/claude-sr-stage-20261028/app，新增审计27项RX7000组件及guard shader；原NVIDIA runtime97项、正式供体manifest1730项核验不变。五个固定外部源码SHA与prepared源码核验，native worker和EXE与构建一致，未把测试copy worker装入产品runtime。310.9.16.0网络原件Valid/NVIDIA，SHA2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f；保留本地依赖/manifest/许可，不扩展正式Release白名单。入口tests/amd-rdna3-nr-20261029/启动RX7000-NR测试.cmd，独立interactive-profile、默认不自动开媒体；启动冒烟退出0，原配置SHA不变。

本机没有RX7000，54项GPU检查使用明确copy测试worker，不是AMD推理。没有实测RX7000画质、性能、HDR、导出、采集、VRR或长时稳定；没有重跑原8K最终输出+NR+VFG危险组合。不改真实配置/驱动/已有SDK与运行组件；Lecram继续暂停。本轮清理仅自己的重复下载ZIP及数字TEMP，删除身份/路径检查与逻辑字节另记cleanup.json，不宣称物理净回收。
## 2026-10-31 用户veyra-qml(69).log补帧间歇掉帧只读排查

本次读取用户提供的7940394字节日志（SHA256 f0b04d276358e98b01fafd7bd33d61e5c0a65d270e92c268f329361905ab7d9f），22611行、3次启动，按启动/session/revision及前5秒预热分开统计；未构建、未运行软件/占用采集卡、未修改产品代码/驱动/配置/运行组件，不push/Release。代码参考3172262，不假定用户包与本机提交一致。报告及原始派生证据E:/项目/Veyra/reports/fg-intermittent-20261031/README.md、analysis.json、segments.json、intervals.csv、steady-medium.json、evidence.json；可重算脚本analyze.py。仅新增诊断文档记录。

RTX5060Ti、4K30 NV12→VFG2X/60Hz，主要稳定段NR/SR关闭。中档84.016秒原帧呈现2518，评估补帧2518，生成呈现2420、过期98（约3.89%），邮箱覆盖/无效结果新增均0；GPU补帧P95指标中位5.573ms。另1秒样本原帧+30、生成+20、过期+10、邮箱不变、提交50fps，主要定位到软件生成帧过期路径而非证明采集不稳。源码采集生成帧deadline容忍10ms；优先调查自适应配对相位、ready观察/唤醒与VSync反压，但低频日志尚不能确定每张过期帧的实际GPU完成时刻。

分开处理：部分高档配置3030候选中2302次预算准入跳过，GPU约23ms；后续高档也有接近全执行的段，不能只以23>16.7断言整体吞吐不足。显示接口错误附近Present/输出检查路径单次约0.73–1.38秒，GPU耗时仅约8ms，属于独立显示调用停顿，根触发源未证实；overlay注入存在但不能直接归罪。DLSS段1838无效明确是contentDuplicate，不是providerDisabled；不冒充DLSS错误。起流discontinuity仅source1，无持续断线/device-removed证据。没有旧版同条件对照，不能确认新版回归，也未修复。

## 2026-10-31 继续复核VRR旧问题与4K30上选择60→30

用户指出现象类似已修VRR周期查询，并询问4K30 NV12上选择60→30的影响。只读复核正式v2.1.1与3172262代码及原日志，未改产品/配置/驱动，未运行或占用采集设备。报告E:/项目/Veyra/reports/fg-intermittent-20261031/RECHECK.md，脚本recheck.py/数据vr-recheck.json；源日志SHA未变。用户暂不清楚是肉眼卡顿还是仅FPS数字下降，未把静止画面过滤直接认作可见故障。

原31分钟/3次启动共22次后台显示query，7ui/13session-start/2display-change；271.988秒连续0次query，仍有8150次FG评估及249评估后丢弃，排除旧2秒周期查询复发。10:48首次728.741ms慢呈现开始比对应display-change query早1.842秒，不能颠倒因果。正式版仍在Present owner同步查DXGI统计，和旧NVAPI VRR探测不同；逐帧查询混在callMs、每秒统计位于presentEnd之后，是待单独计时盲点，未证明其为根因。

纠正上一轮表述：generatedExpiredAfterEval合计三种s.dropped（deadline过期、输出限帧跳过、显示容量最多2ms等待失败），98不能全部称为deadline过期。中档84秒中84条限频deadline日志均expired=false，无法据此分解实际每帧原因；当前优先调查60Hz VSync容量2ms策略与限帧，但尚未有用户设备单变量复现。原README同步更正，原计数证据未改。

五次连接均3840x2160 NV12/30.000030，softwareLimiter=0。37条rate日志requested60To30为true30/false7，而active=false37；1610条frame-rate capture60To30=false、rateSkipped=0，未把30变15。源码supported只接受59–61fps。但选项仍以contentRate=5进入图，上层排除Capture60To30的输入去重，下层却仅排除Transport而开启luma SAD重复判定；正式v2.1.1同样存在。DLSS段1838次contentDuplicate、providerDisabled=0与此策略一致，不能因halfRate未激活就说开关完全无影响，也不能无原画认定1838次均误判。VFG中档无invalid/contentDuplicate仍有98评估后丢弃，故不是单一根因。后续若授权修复，须分开丢弃原因与验证未激活半帧模式下的内容合同；本轮未修改软件。

## 2026-10-31 用户澄清最初为原始时间戳、未开60→30

现场确认与最初日志一致，不能将后期contentPolicy5重复判定倒推为首发原因。已单列reports/fg-intermittent-20261031/INITIAL_TRANSPORT.md并在README置顶。最早可连续统计VFG高档rev6为contentPolicy0/requested60To30=false、无contentDuplicate，显示Automatic/displayQueuePaced=false/vrrSync=false，故后期VSync容量2ms规则也不解释该段。101.089秒3030候选中2302（75.97%）在评估前被预算准入跳过，GPU FG P95指标约23ms；原帧呈现3030、邮箱新增覆盖0。具体日志remaining17.238ms+10ms不足predicted29.847ms（减已有进度约0.181ms），admitted=false。首发主线应查高档4K VFG的预算与配对相位，尚未证明是成本估计还是相位策略错误；后期中档/同步/去重分开保留。本轮只读分析及文档，不改软件，不运行测试、不发布。
## 2026-10-31 用户补充任何补帧都一样：跨提供方复核

不得将此前高档VFG预算拒绝当作全局结论。报告reports/fg-intermittent-20261031/MULTI_PROVIDER.md；原日志还含DLSS和XeSS。XeSS行9884（10:30:19.447）Present调用80.881ms、总81.097ms，前置记录提交0.207ms；生成和节奏在代理内部，不能将0.207ms冒充GPU推理耗时。SDK各短阶段有效样本大多58–62fps、cycleMismatches=0，关闭尾窗低FPS不代表播放持续掉帧；普通frame-flow原帧30也不是XeSS最终帧率。

源码明确XeSS生成帧不经Veyra生成子帧预算/10ms期限/2ms容量分支，每帧显示统计trackScanout也排除XeSS（每秒统计仍调用）；上述机制均不能被当作全提供方同症状的唯一原因。调查范围应扩至共用采集、owner调度、呈现入口、Windows/驱动/合成及窗口状态，但现有证据不能确定同一根因。无产品修改/新构建/现场测试/发布。后续需要同设置分提供方的丢弃原因计数及API耗时分解，不能继续把单段关联提升为定论。

## 2026-10-31 补帧接近刷新率修复

用户确认“开始修”。分支claude/fg-60hz-pacing-20261031，基于c6c28b9；未合并main、未push/Release，未提交。记录docs/FG_60HZ_PACING_2026-10-31.md，回执reports/claude-fg-60hz-pacing-20261031/FINAL.json。

改动：输出不超过刷新率×1.02时生成帧等到下一帧到期前0.5 ms；输出在0.65–1.02倍时自动模式走一帧队列，迟到容忍与准入预算同步放宽（60 Hz 2X为16.2 ms）；实时VFG跳帧后前2次不计入成本；新增fg-near-refresh、fg-drops、replacedAtLeast日志。第4项到达时刻平滑未做。

命令：tmp/claude-fg-60hz-pacing-20261031/build/build.py（并发2）；check.py unit/ab/near/gpu-regress/qml/stage，sweep.py，func.py，ui_smoke.py，脚本已复制到报告tools。

失败与修正：timing测试目标不带/utf-8，包含DisplayFgBudget.h编译失败，比值断言改放realtime测试；pacing日志漏传参数编译失败；测试脚本写入NUL字符常量编译失败；首轮从构建目录运行缺DLSS运行库，FG未生效，改在共享运行目录运行。窗口输入16 ms定时器加GetTickCount只能产出约28 fps，改高精度重绘后才达到目标速率。

05:50–05:53与06:06后用户自行打开E:/App下2.1.1，首轮上限/低比值A/B被污染（GPU约48%），已移到contaminated目录并重跑；未停止用户进程，之后脚本记录userVeyraRunning。首轮用samples比较时发现自动模式samples只是下限、与垂直同步口径不同，会高估旧版丢帧，新增replacedAtLeast后重测，结果按范围报告。

结果：320 Hz自动模式比值0.74–1.0，旧版实际显示少3–15%（范围），新版被替换0、容量丢帧不超过生成帧的2%；捕获→呈现P50差≤0.3 ms，P95 0至+1.8 ms。垂直同步回放容量丢帧78–101→18–22。高于刷新率规则不生效，结果在波动内。单元回放：6000对丢153→0；VFG陷阱600对评估10→600。repair/timing/scheduler/realtime/i18n 0失败，QtQuick 123/0/1跳过，9个原生GPU场景通过，运行目录97个运行组件哈希一致。

未验：真实60 Hz屏、采集卡、RTX 5060 Ti；VFG陷阱本机不能复现，只有日志数字回放；跳帧后高成本的原因未证实。屏幕采集固定延迟修复分支也改同几个文件，未合并、未联合测试。

## 2026-10-29 屏幕采集无补帧时的首帧锚定扣留（Claude）

上级会话复查英伟达低延迟时测到：60fps窗口采集、关闭补帧时，读取→GPU就绪每帧<1ms，但每帧在Present前被固定扣留，每轮p50 3.1–52ms，等于该轮首帧的合成→读取延迟；低队列下扣留超过一个源间隔还使下一次读取晚约15ms。此问题main已有，与Reflex无关。按要求从本地main `4351bbd`新建工作树`worktrees/screen-capture-hold-20261029`（分支`claude/screen-capture-hold-20261029`），不碰共享树中未提交的低延迟改动；未提交、未合并、未push/Release，未改真实配置/驱动/运行组件。

原因：`EngineController.cpp`无补帧分支用`paceSourcePts=options.fg||!physicalCapture`。2026-09-11（history/reports/CAPTURE_LATENCY_AUDIT_2026-09-11.md）只把物理采集改为GPU就绪即呈现；1.4.2加入屏幕采集时只接入了FG成对重锚，无FG仍走文件回放的源PTS时钟——每epoch只锚一次，`deadline=host_+(pts-source_)`把首帧晚读的偏移带到整段会话。同类第二条路径：开/关补帧会重建图，新图epoch从1重新计数，最后一个FG成对锚（按PTS）可能以相同epoch号留到关闭补帧之后继续扣留，物理采集同样受影响。

修改：
- `LivePresentationTiming.h`新增`livePacesSourcePts(fg,physicalCapture,screenCapture)`：物理与屏幕采集无FG按就绪呈现；文件回放、串流（每帧重锚）和FG成对相位保持源PTS。
- `PresentationScheduler::anchored(epoch,paceSourcePts=true)`同时比较锚定模式，另一模式的锚视为未锚定；默认参数让其他调用行为不变。
- `EngineController.cpp`该段改用以上两者；屏幕无FG的`capture-timeline pacing=`由`source-pts`变为`capture-ready`。文件回放、串流与FG路径的参数与原来一致。
- 测试：`LivePresentationTimingTests`新增6项（各来源策略、首帧晚读52ms后600帧的扣留、文件回放保留PTS钟、FG锚在重复epoch中不再扣留、锚记录模式）；`PresentationPacingTests`新增`latency-screen`（60fps窗口采集、开详细帧日志；与低延迟分支同名同义，但无英伟达模式映射），1X时断言`schedulingWaitP95Ms`不超过一个显示刷新+2ms。

构建：`py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task claude-screen-capture-hold-20261029 --root E:/项目/Veyra/worktrees/screen-capture-hold-20261029 --label 2.1.1-hold --jobs 2 veyra_live_timing_tests veyra_repair_contract_tests veyra_presentation_pacing_tests veyra_live_presentation_tests`，新建`build/claude-screen-capture-hold-20261029`，配置+编译218.6秒退出0，改动文件无警告。A/B基线：detached工作树`worktrees/screen-capture-hold-main-20261029`（main加本次测试文件，引擎未修复），同脚本加`--build-dir E:/项目/Veyra/build/claude-screen-capture-hold-20261029-main --label 2.1.1-main`构建两个集成目标210.7秒退出0。

运行：`py -3.11 -X utf8 -B -I E:/项目/Veyra/tmp/claude-screen-capture-hold-20261029/run.py stage|stage-main|unit|latency 3 10|pacing …|live …`（`--main`跑基线EXE，`--warm`共用CUDA缓存）。私有运行目录`tests/claude-screen-capture-hold-20261029/app`由2.1.1 NVIDIA供体硬链接，EXE/着色器以新文件替换，97个运行组件与manifest逐一一致；基线EXE以`*_main.exe`并存，两侧着色器字节相同。每进程≤290秒，TEMP/TMP/CUDA缓存私有。

结果：
- unit：live timing 106/0（含新6项），RepairContract 272/0。RepairContract首次以0xC0000135退出，是runner没给Qt/FFmpeg DLL路径，补PATH后重跑通过。反向检查`tmp/claude-screen-capture-hold-20261029/negative/negative.py`：恢复main的策略时新检查4项失败、其余102项通过；只恢复不比较模式的`anchored`时2项FG关闭检查失败、其余104项通过。
- latency-screen A/B（RTX 5070，2560×1440@320Hz，60fps窗口，无FG，每轮10秒，关/低队列/Reflex各3轮，hold=GPU就绪→Present开始）：修复后9轮hold p50均0.002ms、p95≤0.016ms、max≤0.087ms，`pacing=capture-ready`，9/9通过，读取max≤−0.05ms，未见延后。同一工具的main基线hold p50：关4.08/32.33/27.41、低队列36.74/33.38/32.01、Reflex 36.44/29.61/36.27ms（p95最高55.4ms），低队列/Reflex 6轮读取p95延后13.9–39.1ms（其中3轮p50也延后10.5–20.2ms），`pacing=source-pts`；9轮中8轮新断言失败（关第1轮4.1ms低于320Hz阈值5.1ms，所以确定性回归以单元检查为准）。两侧read(process−pts)约−1.4ms：WGC时间戳早于读取时刻，不能当作合成→读取年龄（上级会话数据同样如此）。
- 原有节奏测试（修复版）：lifecycle 13项、回放6X垂直同步4项、screen-dlss 4X 10项（capture-pair）、screen-xess 2X 10项、回放无FG 3项（仍`source-pts`）、文件2X低队列5项、overload 9项全部通过。effects在冷CUDA缓存时“first frames”（15秒内>10帧）失败、其余5项通过：NGX初始化约7.9秒；main基线同样失败；共享缓存后修复版2/2通过。
- live presentation（修复版）：默认回放28/0、--overload 3/0、--settings-rebuild 21/0、--reset-rollback 33/0、--inactive-effects 69/0。--source-gap用120秒片源时2过4失败（EOF须90秒内到达而片源太长，首项8秒内12帧赶不上冷启动），main基线完全一致（EOS 2726/2726/2723）。改用40秒切片（`tests/claude-screen-capture-hold-20261029/media/nr-1080p30-40s.mp4`，1200帧，ffmpeg流复制）：冷缓存修复版与main都4过2失败（启动项及EOF快照1199/1198；跨epoch日志“每帧恰好呈现一次”通过），预热后修复版6/0（1200/1200/1200）。

合并提示：引擎、两个头文件与单元测试补丁可直接应用到低延迟分支提交`4edcb49`（临时索引`git apply --cached --check`）；`PresentationPacingTests.cpp`第31行冲突，该分支未提交工作区已有同义`latency-screen`，合并时保留其版本、补上本次1X扣留断言即可。

空间：新增构建411,825,958字节、工作树88,250,651字节、运行目录新文件（4个EXE+着色器）56,796,288字节、40秒切片77,515,754字节、40个证据目录66,153,977字节、日志/回执约0.33MB；基线工作树已恢复为干净后正常移除，基线构建405,895,676字节确认无reparse/进程后删除，每次运行的临时CUDA缓存84,422,224字节已回收。

未做/边界：未用采集卡（物理采集无FG本已就绪呈现，只新增FG关闭后重复epoch的处理，由单元覆盖）；未测串流；运行中切换FG的屏幕采集只做了单元检查；未重编产品`veyra_qml_ui`（同一引擎库已编译通过）及独立probe工程的`veyra_remoteplay_core_tests`（未调用`anchored`）；Present开始是CPU侧观测，不是屏幕端到端延迟；仅本机RTX 5070/320Hz。回执`reports/claude-screen-capture-hold-20261029/`（FINAL.json及各阶段JSON），日志`logs/claude-screen-capture-hold-20261029/`，证据`tests/claude-screen-capture-hold-20261029/`。

## 2026-10-31 各分支修复合入本地main

用户要求“给目前各分支的修复都合并到main去，包括Codex那边对7000系的支持，和其他的修复，处理好兼容，冲突”。覆盖此前各段“不合并main”。未push/Release，正式版仍2.1.1，真实配置/驱动/运行库不改。回执E:/项目/Veyra/reports/claude-main-integration-20261031/FINAL.json。

并入（先在`claude/main-integration-20261031`合并并测试，再快进main）：`claude/fg-60hz-pacing-20261031`（8544c0c，含英伟达低延迟4edcb49/e7a9ae1、RX7000 3172262、现场诊断记录）、`claude/screen-capture-hold-20261029`（b819891，提交时取自其工作树未提交内容）、`claude/capture-fps-audio-20261027`（54c942e）。

未并入：`codex/lecram-native-20261019`、`codex/lecram-nr-113-20261019`（暂停线）、`codex/avermedia-51-switch-20260917`（诊断存档）。远端`nrvideo/pr-31`的修复main已有等价提交51591bc。6个旧脏树未动，最新改动早于10-24，没有新近未提交的修复。

冲突：只在合并扣留修复时出现。`PresentationPacingTests.cpp`保留已有latency场景并带上1X扣留断言；`LivePresentationTimingTests.cpp`和本日志两边都留。其余自动合并，翻译2717条无缺失。

兼容修正：
- RX7000把NR后端open拆开后，CMake给`veyra_amd_nr_graph_tests`注入假适配器的字符串替换不再匹配且没有报错，128项118项失败（“requires an AMD adapter”）。改匹配新调用，匹配不上时配置直接失败；修后128项0失败。Codex当时未跑此测试。
- `PresentationPacingTests` lifecycle在2X时期望Reflex变LowQueue，PacingMode自2026-09-22已不起作用，VideoPresenter里main和合并版都没有这个转换，所以2X必失败，属过时断言。改后1X/2X通过。

构建：`tmp/claude-main-integration-20261031/build/build.py all`（并发2，-k 0），只有`veyra_fsr_probe`、`veyra_fsr_upscale_probe`失败：缺`third_party_local/amd/FidelityFX-SDK-2.3.0`，文件未被本次合并改动。其余全部通过。首次未带-k时在此停止，随后补跑。

回归（`verify.py`，运行目录tests/claude-sr-stage-20261028/app，97个运行组件哈希一致）：
- CPU/合同：timing、scheduler、realtime、repair 300、i18n、effect availability 72、effect chain、preset、qml data、playlist、game passthrough、game mode 44、AverMedia carrier/vendor/passthrough、IEC、capture audio、NR model settings全部0失败。第一轮有7项因没传参数退出，补参数后通过。
- RX7000/AMD：rdna3合同54项、lmxxf ABI、metadata、AMD合成图128项0失败；都是假provider/复制worker，不是AMD推理。
- 界面：QtQuick 123/0/1跳过，启动无QML错误，F7/F8私有配置两例通过（未注入真实按键）。
- GPU：9个原生场景、screen-dlss/vfg/xess、latency 1X三档、2X Boost、SR 2X、overload、回放6X垂直同步、lifecycle 1X/2X、settings transaction、fg settings（含backend switch）全部通过。
- 扣留修复合并后仍生效：窗口采集无补帧hold P50 0.002 ms，`capture-ready`。
- 接近刷新率合并后仍生效：回放319.2呈现/秒，旧规则samples 287、被替换≥11/秒；新规则samples 320、被替换0、容量丢帧10秒6帧。垂直同步容量丢帧29→6。窗口39.6→40.0×6在合并后旧规则也不再被替换（samples 238/239.9），新规则P95 26.2→26.5，说明此前窗口场景的丢失与WGC抖动有关。
- 实时呈现：replay、overload、settings-rebuild、reset-rollback、inactive-effects通过。`--source-gap`的EOF快照检查不稳定：合并版3次过1次，未合并main基线3次过1次，差1–2帧；“每帧恰好呈现一次”每次通过。原有问题，未修。

未解决：屏幕捕获真实程序60上限接收59.8–60.0、处理57.0–57.4（3次），120上限60.0/60.0。10-27分支测得60.0/60.0，限速器代码与该分支逐字相同；此后驱动从32.0.16.1656变为32.0.16.2101，2.1.1正式版今天也从53.55/53.72变为56.27/55.27。推断来自环境（合成时间戳抖动），但没有在今天的环境单独构建分支对比，原因未证实。

未验：RX7000实卡推理/画质/性能；真实60 Hz屏、采集卡、RTX 5060 Ti；PS5加采集卡的载波音频；串流；打包。未重跑8K最终输出+NR+VFG危险组合。

## 2026-11-01 补帧延迟不增长返工与 VFG 快模型

用户：“全部都修复，并且保持好低延迟，延迟增长是我不能接受的”；后来“我现在把我显示器调整到60hz了，你重新短测一下，而且VFG高档为什么会掉百分之20？VFG的问题也要解决”。分支claude/fg-no-latency-growth-20261101，基于main 13016a8；未提交、未合并、未push/Release。记录docs/FG_NO_LATENCY_GROWTH_2026-11-01.md，回执reports/claude-fg-sidefx-20261101/FINAL.json。

改动：撤掉自动模式队列、长等待、放宽的VFG预算；LiveArrivalSmoother平滑呈现锚点（提前0.5 ms）；垂直同步容量等待随输出间隔变化；VFG 2X赶不上刷新或预算不够时这一对改用下一档模型（显存预算余量≥4.3 GB才加载）；FgCostSettling改为空转1秒后才记预热、失败探测2/4/8秒重试。更正10-31记录里算错的三组数字（模型把落在刷新边界的呈现算早了一个刷新）。

命令：tmp/claude-fg-sidefx-20261101/build/build.py（并发2）；verify.py stage/cpu/cpu2/qml/smoke/lifecycle/gpu/live/hz60/ab，脚本已复制到报告tools。片源：`ffmpeg -i tests/nr-manual-scale-20261024/media/nr-1080p30.mp4 -t 26 -vf fps=29.87,scale=3840:2160:flags=bilinear -an -c:v h264_nvenc -preset p1 -b:v 40M -pix_fmt yuv420p card-4k.mp4`；4.6K用hevc_nvenc。

失败与修正：
- 第一个抖动钩子在引擎线程空转，挡住调度器，VFG 30×2回调P50假增1.9 ms；两组数据作废，改为等待时继续advanceLive。
- 单个离群回调会把基线带偏十几帧；改为同侧连续两个才跟随。
- P99“增长”来自每轮开头约13帧慢10 ms，新旧规则都有；统计改为跳过开头。
- 垂直同步容量等待固定1 ms在320 Hz实测丢3–6%呈现且P99无收益；改为随间隔变化。
- 快模型第一版几乎全程跑中档：高档耗时估计卡在启动时的39 ms，且新探测全被当预热不计；改为取低包络，4秒没跑过就按七折试一次，结果高档/快模型128/235对。
- 第一版4K高档在本机只要约16 ms，复现不出丢帧；在VFG工作线程加9.4 ms模拟5060 Ti。
- 用户中途叫停一轮约26分钟的320 Hz全量A/B：“为什么跑那么久？我没时间和你耗”。最终代码的320 Hz全量A/B未跑完（1/22组）。

真实60 Hz结果（每项1轮14秒，自动模式显示帧/秒是下限）：模拟5060 Ti速度高档，自动43.0→59.2，垂直同步58.2→60.0；本机速度高档自动56.7→58.5；中档自动57.6→58.8，垂直同步59.0→59.8。真实帧呈现距采集时钟线的P95：34.30对37.65，34.34对44.01，22.33对25.59，22.52对25.64。回调→呈现P95高约2 ms。显存3973→5834 MB。

回归（60 Hz屏）：cpu2 12/0；qml 123/0/1跳过；smoke无QML错误；lifecycle 2/0；gpu 20/0；live 5/1，--source-gap的EOF快照失败，同一项在未改动的main上3次也只过1次。cpu里7项无参数报用法错误，由cpu2带参数跑。翻译2715条无缺失。全量构建只有veyra_fsr_probe/veyra_fsr_upscale_probe因缺本地SDK失败（原有）。

空间：tests/claude-fg-sidefx-20261101 868 MB（含片源295 MB），tmp 98 MB，日志1.7 MB，报告0.75 MB。

未验：真实采集卡、真实5060 Ti、全屏下高档单次39.6 ms现象、模型切换的可见程度、可变刷新和144/240 Hz上的快模型。

## 2026-11-01 VFG 自动调控开关与“自动剔除重复帧”改名

用户：“这个功能的话，放到VFG的参数里面吧，叫做VFG自动调控，然后还有一个自动识别内容节奏这个东西，很多人搞不懂是啥意思，你给改成自动剔除重复帧”。同一分支，未提交。

改动：EnhancementSettings::vfgAutoRegulate（默认开，存界面偏好，不进预设，切换重建图）；列表页和节点页加开关，低档和3X以上置灰；“自动识别内容节奏”改名（QML、Status、Chain、旧版SettingsWindow、四语翻译），只改文字。翻译2718条无缺失。

验证：tmp/claude-fg-sidefx-20261101/regulate_ui.py 真实程序两次启动，默认开、置灰条件、重启保持、中英文新名称都通过，截图 reports/claude-fg-sidefx-20261101/ui-edit.png。hz60 toggle 运行中开→关→开通过。cpu2 12/0，qml 123/0/1跳过，smoke、lifecycle通过。gpu、live未重跑。全量构建340秒，只有两个fsr探针因缺SDK失败（原有）。

注意：用户中途把屏幕调回320 Hz，所以开关这轮是320 Hz，hz60-on/off/toggle.json 的文件名不代表60 Hz，看里面的refreshHz。60 Hz丢帧数据仍是上一节那轮。节点页开关没有点过。


## 2026-11-01 用户授权将Claude最新补帧返工合入main（Codex）

识别claude/fg-no-latency-growth-20261101的37个未提交文件，起点main13016a8。先保全到archives/codex-main-integration-20261101（working-files.zip、binary diff、37项SHA），检查与测试后逐字节确认产品文件未被本轮改写，原样提交6e79a5e，main --ff-only合入，无代码冲突。前后checkpoint tags见docs/MAIN_INTEGRATION_2026-11-01.md。后续仅更新文档/AGENTS，保留原分支回溯，未push/Release；正式版仍2.1.1。

本轮构建15个相关目标0.7秒通过，日志logs/codex-main-integration-20261101/build-20261101-053508/compile.log（dirty=true/head旧值为提交前事实，由before.json快照映射到fix提交）。8组CPU合同/设置通过（含repair300、NR模型设置/旧预设兼容）；QtQuick123/0/1既有跳过；GPU三组VFG4K回放运行中开关、DLSS6X、XeSS2X退出0、真实提供方生成/干净退出、无D3D12错误。GPU与offscreen QtQuick有重叠，otherVeyra为本轮测试进程，只作功能回归，不作性能结论。沿用脚本的captureToPresent字段把相对PTS和host相减，数值无意义，未采信为延迟。

真实GUI两次启动、独立profile：列表开关、低档/超过2X置灰、节点实际双向切换及置灰、中文/英文改名、保存重启均通过，无QML错误；补上Claude只加载节点页未点开关的空缺。Main.qml恢复。EXE/worker/shader与构建一致，QML/翻译一致；正式供体1730项、原默认运行组件97项核验，原用户配置7文件SHA未变；i18n2718条缺失/占位符问题0。

证据reports/codex-main-integration-20261101：before/merge/cpu-merge/qml/gpu-merge/ui-regulate/audit/FINAL.json与复现脚本；日志logs同任务；运行目录仍tests/claude-sr-stage-20261028/app。没有重做60Hz性能A/B、实卡采集/5060Ti/RX7000/VRR验收，没有重复全量测试或跑原8K危险组合；Claude的已知source-gap/FSR探针依赖问题边界保持，不冒称全部通过。仅清本轮可再生TEMP，清理逻辑字节和保留路径见cleanup.json；未测物理净回收。

## 2026-11-02 垂直同步容量等待跟随回调抖动（Claude）

用户问60 Hz垂直同步为何仍丢部分帧、有什么办法，看完选项后选“上限3 ms”。分支claude/vsync-jitter-wait-20261102从main 8535791，未提交、未merge、未push/Release。执行记录[VSYNC_JITTER_WAIT_2026-11-02](VSYNC_JITTER_WAIT_2026-11-02.md)，分析reports/claude-vsync-slots-20261102/FINDINGS.md，脚本tmp/claude-vsync-slots-20261102。

分析：现场日志`[capture-callback]`算出回调到达抖动，T911和G50为0.01–0.06 ms，KUHAIMI 4K60 Version（veyra-qml(69).log，4K30）为1.87 ms，所以11-01注入的1.9 ms只代表最后这张卡。先试“预分配刷新格点”，模型显示没有优势，撤回；屏幕刷新率匹配同样无效；只有等待时长能换丢帧，且代价随抖动增加。

改动：`liveSmoothedCapacityWaitMs`增加抖动参数，输出不高于80 fps时等待为`1+3.125×平均偏差`，下限原规则，上限3 ms；自动模式、高刷MFG、内容去重、串流不变。`[live-anchor]`加`capacityWaitMs`，`VEYRA_TEST_FIXED_CAPACITY_WAIT=1`恢复固定等待。

失败与修正：第一次构建失败（error C3493，显式捕获的lambda没有`liveArrival`），因为输出经过管道，后台任务报退出码0，那轮单测跑的是引擎未更新的程序，已作废。补捕获后69.4秒通过，并改为直接核对build.py退出码。

结果（320 Hz）：timing 151项、preview通过；cpu 14项通过，另7项是没传参数的usage退出（名单和上个任务相同）；cpu2 12/0；gpu 20/0；live 5/1，`--source-gap`失败，是已记录的不稳定项，这次没有再查。`wait sanity`注入0.05/0.6/1.9 ms，测得平均偏差0.048/0.514/1.623，等待1.15/2.60/3.00 ms，容量丢帧都是0，面板平均24.15–24.25 ms（固定规则24.33 ms）。320 Hz队列不紧张，这只说明没有副作用。

真实60 Hz（用户切屏，日志核对60.000 Hz；`wait hz60/hz60b/hz60c`共14轮全部退出0，每轮跳过前5秒）：DLSS注入1.9 ms，固定→新，少显示生成帧37.7→5.6/分（各43秒，27→4帧），上屏估算平均30.93→32.20 ms，P95 38.64→39.73，P99 39.97→40.68，最大46.68→41.69。VFG中档注入1.9 ms，5.6→2.8/分（4→2帧，样本少），平均30.78→31.17，P99 39.35→39.73，最大49.30→40.55。DLSS注入0.05 ms（等待1.02→1.15 ms，各9秒），都是0，平均30.05→29.97。上屏时刻是按刷新格点估算的，拟合误差0.16–0.89 ms。中途纠错：第一版统计含DLSS启动段，呈现P95读成34 ms，逐秒看后确认是启动的2–3秒、新旧规则都有，统计改为跳过前5秒。证据reports/claude-vsync-slots-20261102。

未验证：真实采集卡、高抖动实卡、FSR和VFG高档的60 Hz对比。未改真实配置、运行组件、采集卡。


## 2026-11-01 Codex接手Claude垂直同步3 ms等待收尾

完整核对最新Claude会话9720f799-88ff-48b9-847a-6fef8a5f00e5的用户选择、切屏确认、测试完成和额度中断。当前claude/vsync-jitter-wait-20261102基于main8535791，7文件原始SHA/ZIP/补丁保全在archives/codex-vsync-handoff-20261101。没有新功能要求，不改算法；头文件注释撤回不成立的物理上屏收益，保持Claude代码/测试主体。

本节纠正上一节“少显示生成帧”和“上屏估算”口径：14轮日志复算，DLSS高抖动稳态真实/生成提交1284/1257→1285/1281，VFG中档1284/1280→1286/1284；分别是提交差27→4、4→2，不是扫描显示计数。原panel_est.py强制每次提交占不同刷新格点，不能验证被替换帧及真实上屏延迟；+1.27/+0.39 ms和重复帧率/P95/P99等推估不作验收。原captureToPresent*混用相对PTS和host时间无意义。有效软件时刻拟合线→Present均值DLSS21.88→21.97 ms、VFG21.85→21.83 ms，不是物理延迟或零增长保证。原始报告保留，正文和当前入口纠正。

命令：python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-vsync-handoff-20261101 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.1-test --jobs 2 veyra_live_timing_tests veyra_realtime_preview_tests veyra_presentation_pacing_tests veyra_qml_ui；11.8秒退出0。reports/codex-vsync-handoff-20261101/verify.py stage/handoff：两组CPU和DLSS/VFG中档8秒实测窗口通过，真实60Hz、预算跟随至3ms、提供方实际生成、干净退出、无D3D12错误。没有重跑全量或更改显示/用户进程；继承Claude的source-gap失败如实保留。

运行目录仍tests/claude-sr-stage-20261028/app，测试入口tests/claude-fg-60hz-pacing-20261031/启动补帧刷新率测试.cmd。最终配置/运行组件SHA、清理、空间与产物在reports/codex-vsync-handoff-20261101/FINAL.json。未提交、未合并main、未push/Release；真实采集卡与物理显示延迟未验。详见docs/VSYNC_JITTER_WAIT_2026-11-02.md。

收尾核验：原用户配置7文件SHA未变、默认运行组件97项一致；EXE/worker/shader均匹配构建。自动审批两次拒绝本轮可再生TEMP清理，仅返回blocked by policy，无具体理由；未删除任何缓存，保留两个run目录共7617338逻辑字节，见cleanup.json。无物理空间回收声明。


## 2026-11-01 极简播放条音量交互修复

用户反馈难拖、输入后空白与回车不确认。在当前claude/vsync-jitter-wait-20261102保留此前未提交改动，只修QML与对应测试。FullscreenBar固定可激活标志、交互结束归还焦点，外部失焦/隐藏提交；VSlider完整按下位移、端点命中音量专用扩展、空白被动提交与键盘确认/取消/隐藏焦点清理；CineBar扩大音量区域并禁止其手势触发移动窗口；全屏编辑不自动隐藏。

构建产品与QuickTest最终0.7秒通过（并发2，build-20261101-235611）；新旧夹具分开检查后最终132/0/1既有跳过、33.4秒，包含单次快速/两端拖动、模型绑定、Return/Enter、空白、焦点、Esc及原NR控件。i18n2718条缺失/占位符0，diff检查通过。真实Windows极简窗口操作确认100%→21.95%快速拖、63回车→0.63、41点击空白→0.41，未随之移动窗口，测试观测代码已恢复为产品源码。不是声学音量测试；人工未覆盖全屏。详细记录docs/MINIMAL_VOLUME_FIX_2026-11-01.md，回执reports/codex-cinema-volume-20261101/FINAL.json。

失败/纠正：动态窗口flag导致软件渲染内容丢失，已改稳定flag；一次整轮130/2/1，快速拖改用centroid总位移，公共扩大命中影响NR选择则缩到音量opt-in，最终全过。一个原型测试进程核对PID/EXE/私有data-dir后停止；未停用户进程。首次123项使用旧夹具，未计入新测试证明。原补帧代码及原配置/运行组件保护结果、产物空间见FINAL。未提交、未合并main、未push/Release。


## 2026-11-02 全部已完成修复合入main，准备2.1.2测试包

用户授权本地合并及构建2.1.2，不发布。开工核对所有20个本地分支/13个工作树和Claude最新会话文件：所有已完成产品分支tip已是main祖先，仅Lecram两条暂停线和AverMedia诊断存档含独立提交；其他旧脏树保护原状。14未提交文件保全到archives/codex-build-2.1.2-20261102，逐SHA和工作树状态在reports同任务/before.json。提交9e836ab后main从8535791 --ff-only快进，无冲突；前态checkpoint/pre-main-2.1.2-20261102。

版本将项目/显示字符串统一到2.1.2，新增当前打包入口和测试说明；按BUILD_2.1.2_TEST_2026-11-02.md复用既有Release构建、并发2。NVIDIA沿用已核验2.1.1默认组件，AMD另加入已授权本地RX7000子集，自有worker单独编译，禁止把测试替身、用户配置和整套SDK入包。所有阶段结果、失败和最终保护核验继续记reports/codex-build-2.1.2-20261102/FINAL.json。


### 2.1.2本地测试构建交付完成

47b5e84作为已提交产品快照、PE2.1.2.0；build-recording-handoff.py按上节参数构建12目标87.8秒通过。reports/codex-build-2.1.2-20261102/run.py cpu-merge八组通过、verify.py qml132/0/1、run.py gpu-merge VFG调控切换/DLSS请求6X/XeSS2X三组实际生成/正常停止通过（不作为性能A/B）。i18n2718条缺译/占位符0。package-2.1.2.py stage/seal/cold全部通过；NVIDIA1634项/676470641字节ZIP，AMD2260项/716404207字节ZIP，27项RX7000子集单列清单。主EXE7f9ee826…，两包依赖闭包、默认运行组件SHA和PE2.1.2.0通过；应用源码ZIP和prepared网络源码ZIP、校验文件一并交付，4个未变依赖源码档在原位SHA核验。

冷启动为从ZIP实际解压、最小PATH和独立配置，NVIDIA/AMD各退出0、214/222真实提交、无QML/D3D12错误；RTX5070默认效果关闭，不替代AMD实卡。清理cleanup.ps1先校验每个文件在验证副本和保留目录都等于manifest，再删除两个重复验证目录，共2623969881逻辑字节；保留两个可直接运行目录/ZIP/源码和独立启动脚本，未测净物理回收。stage第一次因自生成未跟踪pyc正确拒绝，精确删除该单个缓存后通过，没有绕过干净源码检查。原配置7文件、旧97运行组件、旧工作树和Git refs保护结果及空间清单在FINAL。准确命令、过程与证据见docs/BUILD_2.1.2_TEST_2026-11-02.md和reports/codex-build-2.1.2-20261102/FINAL.json。后续仅文档提交，不改已打包产品；未推送或发布。


## 2026-11-02 用户现场运行只读监测（已按要求停止）

PID61528，用户直接运行2.1.2 NVIDIA包；不激活窗口、不启动额外GPU负载、不改设置/驱动/进程。reports/live-monitor-2.1.2-20261102/watch.py只读日志约120秒（UTC10:37:28–10:39:28）；用户要求停止后仅分析已保存数据，没有重启采样。当前4K60、NR1080p、VFG高2X、自动去重、Reflex增强/VRR请求；软件回调59.99–60.01，7120输入、4076有效真实帧全部提交，采集丢弃/邮箱覆盖新增0；625次候选在计算前跳过、6张生成后过期，容量/限帧丢弃新增0，另89对内容重复过滤。输出采样中位60.5、范围45–90，开头数秒45–52；本段错误/告警0、音频欠载增量0、显存3237–3254MiB。不把正常内容去重或提交FPS当物理显示证据。观察脚本的start.latest字典引用导致末态覆盖，已按独立落盘baseline.json复算增量、保留原记录并修正脚本，未采信错误零增量。简单报告及原始证据在上述reports目录。仅本工作日志追加，产品源码、用户设置和运行进程不改。

用户要求标出时间/日志交Claude：已生成reports/live-monitor-2.1.2-20261102/CLAUDE_HANDOFF.md及per-second-deltas/evidence-anchors。只复核保存数据。更正归因：UTC10:37:28–34低45FPS段，fgSkippedBeforeEval和expired增量均0，fgInvalid/contentDuplicate新增89；后段625次预算跳过与6张过期单列，不能混为同一成因。报告含PST/UTC对应、原始完整日志、固定筛选片段与行号、epoch6→7和呈现revision14→15时间。未恢复监测、未修产品或改用户设置。

## 2026-11-02 自动剔除重复帧容忍零星噪点（Claude）

用户读完上节监测交接后要求排查，结论是去重把重复帧误判成新帧（E:/项目/Veyra/reports/claude-live-monitor-20261102/FINDINGS.md）。随后用户要求按方案重修、尽量精准；VFG自动调控不要自动开启，即原方案第2项不做；PS5开着，用同一个30帧游戏实测。分支claude/dedup-precise-20261102，基于main 30337c8，未提交。上节Codex的未提交记录原样保留。

**实测**：Veyra关闭、采集卡空闲时，用已安装的ffmpeg 8.1.1以dshow原格式NV12 3840x2160@60读取T911。
- 5秒NUT原始片段，296帧，3.7GB，分析后已删：重复帧亮度最多差3级，新帧至少10.6万个分量超界。
- 失败一次：Python从管道读，10秒画面跑了39秒，ffmpeg丢帧，这轮数据作废。改为ffmpeg每秒写一段到SSD、读完即删。
- 90秒共5401个采样、没有丢帧。同一批画面上旧规则误判5次，都是单点差4级、位于HUD白字边缘；容差64时误判0次，每秒恒为30。
- cv2.imwrite不支持中文路径，裁图改用imencode后写文件。

**改动**：
- PictureCompare.h：toleranceFor=分量数/196608（4K NV12为63），超界分量不超过它仍算重复。
- EnhanceGraph：删除64×36的contentDuplicate弃帧，连同相关字段和计数。
- EngineController：[content-dedup]增加repeatMaxOver；[fg-validity]去掉contentDuplicate/both。
- CaptureCardSource：日志文字补上容差。ContentDedup.h：注释同步。时序单测新增8项。
- 失败一次：EngineController.cpp用Python整体替换时被写成LF，已恢复CRLF，git diff只含实际改动的12行。

**构建与回归**：
- build.py：veyra_live_timing_tests 19.8秒；veyra_qml_ui加三个呈现测试33.2秒；均exit 0。
- 时序单测159项全过。
- verify.py stage：运行目录EXE换为fb1e3ad7…，97个运行组件一致。
- cpu：14过，另7个缺参数打印用法退出（同前）。cpu2：12/12。gpu：20/20，无D3D12错误。
- live：5/6，--source-gap失败。gap对照1次：本分支和10-29的旧main基线同样失败，EOS都是1200/1198，属于原有问题。

**实机**：live_run.py把用户2.1.2配置复制到私有目录（原文件不改），VEYRA_VERBOSE_FRAME_LOGS=1，--exit-after 75000。
- 失败一次：第一次依靠自动恢复来源，因用户会话里恢复是关的，没有打开采集卡，作废。之后改为传入用户采集对话框给出的原样URI。
- 旧版（与2.1.2同一EXE，7f9ee826…）稳定段51秒：每秒保留30–31，有2秒到32；误判2；跳过0.58%。
- 新版稳定段57秒：每秒恒30–31；1735张重复帧里86张带超界点（最多30个、差7级），仍判为重复；误判0；跳过0.29%；过期和无效均为0。

**未验证**：其他采集卡、其他分辨率、P010实机、更吵的场景；60帧内容下VFG高档的预算跳帧（本次未改）；主观观感。
记录见docs/DEDUP_PRECISE_2026-11-02.md，回执见E:/项目/Veyra/reports/claude-dedup-precise-20261102/FINAL.json。

## 2026-11-02 去重修复合并main并重新打包2.1.2（Claude）

用户：“那不管他，把修复合并到main，然后重新打包2.1.2吧”（“他”指原有的--source-gap失败）。

**提交与合并**：
- WORKLOG里Codex未提交的监测记录，用hash-object/update-index单独暂存，原样提交为a90b4a9。
- 修复、文档和package-2.1.2.py的--task参数一起提交为358be8c。
- main从30337c8快进到358be8c，活动树回到main。
- 失败一次：为检查语法跑了py_compile，在scripts/acceptance/__pycache__生成了一个pyc。核对路径后只删除该文件，同目录10-21的旧pyc保留；stage的干净源码检查没有受影响。

**构建与检查**（任务claude-build-2.1.2-dedup-20261102）：
- build-recording-handoff.py --reconfigure --label 2.1.2，12个目标61.5秒通过，回执head为358be8c、dirty=false，主EXE 33d8e615…。
- run.py cpu-merge 8/8；verify.py qml 132/0/1；run.py gpu-merge 3/3，无D3D12错误。gpu-merge前只把共享运行目录的veyra_presentation_pacing_tests.exe换成本次构建；用户的测试实例（PID 3384）占着该目录的主程序，没有停止它，GPU短测时它也在运行。

**打包与验证**（package-2.1.2.py --task claude-build-2.1.2-dedup-20261102）：
- stage：20秒，两包通过。
- seal：147秒。NVIDIA ZIP 676472179字节/1634项，AMD ZIP 716405735字节/2260项，源码ZIP 73389714字节。
- cold：55秒。两包各约10.5秒退出0，提交214/222帧，无错误。
- cleanup.ps1核对后删除两个解压副本，合计2623978645逻辑字节；未测净物理回收。
- finalize.py：旧2.1.2包、用户配置7个文件、旧ZIP、2.1.1供体均未变。

**未验证**：AMD实卡推理；新包在真实采集卡上的效果（修复本身已在去重任务里用PS5实测）；用户主观观感。
记录见docs/BUILD_2.1.2_TEST_2026-11-02.md末节，回执见E:/项目/Veyra/reports/claude-build-2.1.2-dedup-20261102/FINAL.json。


## 2026-11-02 2.1.2公开发行准备（用户授权）

以Claude最新去重包为供体，整理2.1.1以来最终功能/修复及迁移步骤；原字节更新交流群7二维码（11月9日前），双二维码各220。正常合并远端独有star-history提交27d04bd。产品358be8c、EXE33d8e615…保持，原测试目录/进程/用户配置不动。发行新目录releases/codex-release-2.1.2-20261102，核验/上传回执reports/codex-release-2.1.2-20261102；发布状态以最终回执为准。


## 2026-11-02 2.1.2公开发行完成与远端核验

2026-11-02，已发布 [2.1.2运行包](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2) 和 [对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2-sources)。Latest 指向 v2.1.2，均为正式发布，非草稿/预发布。发布准备提交 `899590f6864391b89fcb24d1842e88a41addb780`；产品构建提交358be8c，主EXE33d8e615…保持。

- Veyra-2.1.2-NVIDIA-win64-portable.zip: 676597909 bytes，SHA256 `34d91df21a6a6c5928c7e32e2ce4b17fa9deaf484e9e1e4f0aaf441c1ab21142`。
- Veyra-2.1.2-AMD-win64-portable.zip: 716543875 bytes，SHA256 `94dab9547146cc595436158ab64708c7d460a9d20da602d8adae2cc4a2462c92`。

应用源码 SHA256 `950fb21830918ba8e12a4c4ead75f60022c361d8074c830dd38de4ad2c14cb15`；新增网络源码 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`。两个运行包只替换文档/二维码/发行元数据，旧payload新增/变动路径已记录PACKAGING.json。未改程序/QML/shader/运行组件/模型字节。原供体两个ZIP摘要校验并逐文件验证，未触碰原测试目录或用户设置。

执行：`git fetch nrvideo main --no-tags`、正常合并远端star-history；`git archive`生成899590f源码；`python -X utf8 -B scripts/acceptance/release-2.1.2.py`完成ZIP/路径/清单/模型/运行组件身份审计；`git push --atomic nrvideo main refs/tags/v2.1.2 refs/tags/v2.1.2-sources`；`gh release create --draft`、逐资产上传校验服务端摘要，先公开sources（非Latest），再公开运行包（Latest）。外部编排/回执在reports/codex-release-2.1.2-20261102。

远端核验通过：5个资产的GitHub SHA256、字节数和uploaded状态均等于本地；全部下载URL匿名HEAD为200；两张二维码实际匿名下载成功，各width220，新群码SHA256 fe347a447849bd7257ab5f343fa488e54a271ce88c4223c1b0773ccf383bc9ab与用户原图完全相同；README双语及support群码/有效期正确；发布标签解析为上述源码提交；4份固定依赖源码服务端SHA256与声明一致。

发布编排曾遇REST按tag查询草稿返回404，已改用认证release列表取得ID后操作，未误报发布成功或修改二进制。产品源码无变动，沿用Claude同一构建的针对性测试，没有重复占用采集卡。保留最新正式ZIP及对应源码、摘要和必要报告；本轮无解压副本，新增发行文件约1.37GiB，不声称测量了物理回收量。原测试包/用户配置保留。

群公告990字符，保存reports/codex-release-2.1.2-20261102/GROUP_ANNOUNCEMENT.txt，仅交付文字，未向群聊发送。


## 2026-11-02 GitHub交流群更新到8群

用户明确授权：7群已满，替换为8群原图。当前README双语、支持区、2.1.2更新说明与2.1.2/源码两个Release正文同步；有效期仍11月9日前，赞助图与双码各220布局保持。只改图片及当前入口，不重写发布标签或打包资产。执行git diff --check，推送后匿名读取当前文档/新图，对比用户原图SHA256，并核对两Release正文和所有资产id/摘要保持；具体结果及原正文保全见reports/codex-group8-qr-20261102/FINAL.json与before.json。文档更新不运行产品构建/测试。


## 2026-11-05 撤销补帧延迟预算实验

用户明确删除功能并停止研究，按失败方向记录。`git for-each-ref --contains be3f7ff`确认仅独立分支包含实验；保全未提交探针文本后 `git restore --worktree -- tests/integration/LiveLatencyBudgetProbe.cpp`，`git switch main`，`git branch -D codex/fg-latency-budget-20261104`。main基线fedd7fc，产品7个树哈希未改，未merge/push/Release。

受控清理54个实验程序／启动入口／缓存文件，完整绝对路径及SHA256见reports/fg-latency-retirement-20261105/DELETE_PLAN.json与REMOVAL.json。遍历明确跳过reparse point和用户配置目录，保留SDK、模型、shader、正式包、其他工作树及日志；逻辑文件大小不冒充实测释放空间。共享build.ninja移除，下一次须--reconfigure main。

没有找到可重复的最低差而增加预算稳定满帧案例；早期182→240包含首次加载等待，已剔除。本轮只做撤销和文档，不再运行性能试验；验证采用分支引用、代码接口搜索、产品树哈希与删除清单复核。详见[失败方向与终止记录](FG_LATENCY_BUDGET_RETIRED_2026-11-05.md)。

## 2026-11-02 全屏进度条修复

在codex/fullscreen-seek-20261102隔离修复。旧版真实Windows按下7ms后MouseArea取消；进度条遗漏于interacting，焦点被交还主窗口。纳入scrubbing、窗口移动排除、预览mask及完整隐藏条件，取消/禁用时清理120ms延迟seek。构建2.2s/最终0.7s通过；新增用例在旧代码5失败；初版完整139/2/0暴露禁用不触发cancel，修复后141/0/0（35.819s）。真实全屏3次分别提交33.770/5.902/20s，无取消，退出全屏正常；原用户配置保留。首轮人工到EOF重开未算通过，日志保全。源QML观察探针逐字节去除，使用小型2.1.2通用补丁交付，不复制完整包，不修改GitHub资产/运行库/真实配置。命令、回执与验证边界见docs/FULLSCREEN_SEEK_FIX_2026-11-02.md，FINAL与日志在E:/项目/Veyra/reports及logs/codex-fullscreen-seek-20261102。无合并main/push/新发布。


## 2026-11-02 RTX30 VFG实验兼容

用户授权移植，分支codex/vfg-ampere-20261102基于4895549。来源Xchen991/RTX30-Broadcast-FrameGen固定07536ab32a508b7daa714a4a9bda5b56f031bbc3/MIT，移植CUDA查询和内核元数据重定向，替换固定IAT地址/轮询为按名称发现和生命周期租约；私有副本不写原kernel，选中设备和当前CUDA上下文隔离，退场逐项读回指针与页保护；40/50原生无补丁。菜单、节点、硬件能力和四语提示同步，包含上一轮全屏修复。主程序增量和覆盖补丁按FINAL.json，VFG原DLL/模型不新增不改动。

构建最终4.2s通过；合同180、真实模块/62个fatbin/3轮共享租约342、能力75、VFG设置683、repair300、i18n0失败；QtQuick140/0/1。RTX5070原生GPU各质量/倍率/8与10位回归2425项0失败，最终结果和独立GUI 2X60FPS功能检查见FINAL.json。首次repair因子进程库路径缺失启动失败，补充子进程PATH后通过，失败日志保留。无30系实卡推理/画质/速度结论；不以本机50系代替。临时GUI Loader已去除，原7配置文件和5个VFG运行DLL逐摘要保护，未复制完整包。实际命令、日志、来源与局限见docs/VFG_AMPERE_PLAN_2026-11-02.md，E:/项目/Veyra/reports及logs/vfg-ampere-20261102。本轮不合main、不push/发布。

## 2026-11-03 RX7000 NR：多 GPU 时工作进程只看选中的显卡（Claude）

用户：“直接修复吧”。

**依据**：现场 7900 XTX 加 gfx1036 核显，HIP 0 号是核显。ZLUDA b135e23 有两处默认用 0 号设备：
- NVAPI 报的 LUID（Codex 已修）；
- `Context::new` 在 `hipSetDevice` 之前就建了 `alloc_stream`，导致 `cuMemAlloc` 通过核显的流去给独显显存清零。

**工作树**：独立工作树 `worktrees/rdna3-hip-visible-20261103`，分支 `claude/rdna3-hip-visible-20261103`，基于 Codex 的 `77fcd89`。共用施工树（Codex 分支 `codex/rdna3-nvapi-luid-20261103`，Codex 进程在运行）未触碰。

**改动**：
- 新增 `Rdna3HipDevices.h`：探测、按 LUID 匹配、设置 `HIP_VISIBLE_DEVICES`。
- 工作进程在加载 HIP 7 之前做设备选择，并新增 `--hip-probe` 模式。
- 新增 `tests/integration/Rdna3HipDevicesTests.cpp`，以及 CMake 目标。

**命令与结果**：
- `py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task claude-rdna3-hip-visible-20261103 --root <工作树> --label 2.1.2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_amd_nr_worker veyra_amd_nr_test_worker veyra_rdna3_worker_diagnostics_tests veyra_rdna3_hip_devices_tests`
  - 新构建目录，43.7 秒通过。
- `veyra_rdna3_hip_devices_tests`：单元测试 15 项通过，0 失败。
- 加 `--hip C:\Windows\System32\amdhip64_6.dll`：19 项通过，0 失败。
  - 本机 gfx1036 的 LUID 0x16583 与 DXGI 一致。
  - 在进程内设置后，HIP 只剩所选设备。
  - 设置超出范围的序号后，设备数为 0。
- `veyra_amd_nr_worker.exe --hip-probe`：
  - 对 HIP 6：退出码 0，列出 1 块设备。
  - 对缺失的 HIP 7：输出 `load-failed-win32-126`，退出码 2。
- `veyra_rdna3_worker_diagnostics_tests`：14 项通过，0 失败。
- 新旧 worker 的导入 DLL 相同：d3d12、dxgi、kernel32、version。

**打包**：`tmp/claude-rdna3-hip-visible-20261103/package.py`。
- 输出：`test-packages/claude-rdna3-hip-visible-20261103/Veyra-2.1.2-RX7000-NR-multiGPU-fix2.zip`。
- 第一次生成的源码补丁带进了 Codex 的 RTX30 VFG CMake 段落，公开源码缺这些文件，会导致配置失败。改为只保留 RX7000 段落后，重放比对通过。
- 第二次运行报 `PermissionError`：shell 的当前目录停在 verify 目录里。切换目录后重跑成功。

**未做**：
- 没有在 RX7000 实卡上推理；本机没有 HIP 7。
- 没有跑 RDNA3 合同测试：改动不进入合同测试 worker。
- 未提交、未合并、未 push。

## 2026-11-02 英伟达低延迟“开＋增强”高负载提示（Claude）

用户：“那算了，加个提示好了，建议高负载的情况下开而不是开+增强，增强有概率导致掉帧”。

**依据**：现场 RTX 5090 日志（4K60 采集＋NR＋DLSS FG/VFG，见 `E:/项目/Veyra/reports/claude-ll-boost-field-20261103/FINDINGS.md`）。
- 增强档下，驱动在每个源帧读取前的 Reflex Sleep 平均 4–18 ms，最长 27 ms；补帧比应有的少 8–21%。
- “开”档的 Sleep 约 0.003 ms。两档 GPU 耗时相同。

**分支**：
- 分支 `claude/ll-boost-hint-20261102`，独立工作树 `E:/项目/Veyra/worktrees/ll-boost-hint-20261102`，基于 main `fedd7fc`。
- 共用施工树当时在 Codex 的 `codex/vfg-ampere-20261102`，Codex 进程在运行，所以没有切换或改动那棵树，也没有动它的构建目录。

**改动**：
- `qml/Veyra/NvidiaLowLatencyEditor.qml`：
  - 提示改为“减少处理排队。高负载时建议选「开」而不是「开＋增强」：增强有概率导致掉帧，还会提高 GPU 频率、增加功耗。”
  - 该行加 objectName，供测试使用。
- `i18n/catalog.json`：更新同一条目的 zh / zh-TW / en / ja，条目位置不变；格式按 `json.dumps(indent=1)` 校验后写回。
- `tests/qml/QuickSmokeTests.cpp`：模拟 `veyra` 补 4 个属性（默认值与之前未定义时等价）；注册 NvidiaLowLatencyEditor。
- 新增 `tests/qml/quick/tst_nvidia_low_latency.qml`，3 项：
  - 提示引用真实的选项文字。
  - 提示显示在选项下方。
  - 不可用时仍显示“需要 NVIDIA 显卡”。
- `tests/unit/UiLanguageTests.cpp`：新提示的英文译文须精确匹配。

**命令与结果**：
- `py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task claude-ll-boost-hint-20261102 --root <本工作树> --label 2.1.2 veyra_qml_quick_tests veyra_ui_i18n_tests`
  - 新构建目录 `build/claude-ll-boost-hint-20261102`，46.6 秒通过。
- `tmp/claude-ll-boost-hint-20261102/run_tests.py`
  - QtQuick 137/0/1（35.9 秒，含新增 3 项）。
  - i18n 0 失败。
- 离屏渲染 `reports/claude-ll-boost-hint-20261102/hint-render.png`：
  - 第一次中文全是方块：离屏平台没有中文字体。加 `QT_QPA_FONTDIR=C:/Windows/Fonts` 后正常。
  - 第二次选中高亮还没到位就截图了，等 600 ms 后重渲。

**空间**：新增工作树约 90 MB，构建目录约 47 MB。

**未做**：
- 没有构建主程序，也没有在真实界面里看。
- 没有改低延迟或驱动的行为本身。
- 未提交、未合 main、未 push。

## 2026-11-05 用户授权整合全部保留工作到本地 main，并研究 NR 网状调节

存档点8eda262／checkpoint/pre-main-integration-20261105；Claude三工作树先保存差异和文件摘要，提交3593e52（低延迟提示）、19a8680（HIP选择）、ba00ce3（三类超分，不含RIFE），再经5209442／974fa9f／79d1ac9合并。19a8680继承全屏、VFG30、NVENC及RX7000全部6个待合提交。记录冲突两侧内容，CMake保留两个新后端，能力判断同时保留Ampere VFG和流媒体，另增5条针对性断言。旧脏树不覆盖，暂停Lecram／AverMedia诊断不并，预算与RIFE产品接口扫描为空。

实际命令 `python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-main-integration-20261105 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.2-test --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared <19目标>`，134.4秒通过，能力断言增量1.3秒。恢复了正确main配置及patched FFmpeg。reports/main-integration-20261105/stage-and-test.py、gpu-checks.py记录完整测试命令；日志logs/codex-main-integration-20261105，私有运行tests/codex-main-integration-20261105，临时tmp/codex-main-integration-20261105。

验证：SR设置240、VFG设置683、能力80、VFG模块342、HIP单元15/真实HIP6枚举可见性19、worker诊断14、GPU协议54、原生VFG GPU2425、效果链/预设/NR模型/UI合同/i18n通过。QtQuick最终146/0/0；实际界面15项冷／热两轮通过。7组各60帧SR导出无错位；H.264/HEVC各270帧，IDR0/120/240且原始stss1/121/241。首轮测试参数及Qt模块/截图目录部署缺口保留失败，修正夹具后重测，未为夹具失败修改产品。冷启动新VFX模型12.4秒JIT曾触发无进展诊断；热缓存1.3–1.5秒无engine/QML错误，保留首开停顿限制。没有30系／RX7000本地推理或新超分HDR通过声明。上游许可原文字节保留，其新增文件EOF空行的diff-check提示不擅改许可；本轮其余差异check通过。

合并后仅删除已合入且无工作树检出的本地冗余分支，SHA/分支映射和保护检查见reports/main-integration-20261105；不删旧脏树、依赖、正式包和用户文件。NR仅完成三个设计方向研究及真实参数映射建议，未改NR界面。当前事实及最终状态读docs/MAIN_INTEGRATION_2026-11-05.md和回执FINAL.json；未push/Release。

## 2026-11-05 NR网状调节器实施

用户授权按分组网方案开分支，要求原滑条保留、开网后置灰，全部功能统一到网状面板。基线main f1f8298，分支codex/nr-web-editor-20261105。NrLayerEditor只管理所有权，完整参数提取到共享NrParameterEditor；四轴变化、六轴保护、总倍率/本层旁路在NrWebControls；NrWebPad处理拖动/键盘/数值/默认/撤销/取消。既有参数与预设结构不变，只新增UI偏好布尔白名单和只读有效保护map。没有改引擎/Shader/Runtime。

构建命令及五目标见docs/NR_WEB_EDITOR_PLAN_2026-11-05.md；首次--reconfigure121.4秒通过，快照增量1.1秒。reports/nr-web-editor-20261105/check.py记录私有部署和测试：GPU完整首轮163/0/0；加入滚动与跨层数字拖动后软件完整166/0/0，GPU Pad最终12/0/0；UI合同、i18n、NR模型均0失败，16条新增文案三语完整，extract2746条缺译/占位均0。

真实主程序nr-web-ui.qml13项及nr-web-restart.qml通过，开启SF NR CreateFeature18成功，内部792x446/输出1280x720；最后一轮engine/QML错误为空。原滑条同步、两网实际修改、自动真实系数、手动值保留、尺寸/模型额外滑条和重启恢复已验。截图nr-web-ui-final.png已查看。第一次启用SDK约8秒等待时诊断栈在nvapi/nvngx_dlssd，后恢复；保留首轮，未改无关SDK。Quick中的原PlaybackRate快照两条undefined范围警告仍在，实际程序无QML错误。

失败与修正：逗号声明语法、测试误覆盖Item.data、软件等待帧信号、恢复默认的绑定更新顺序、数字滑条跨层后连续live及release过滤、Flickable阈值后持续移动夹具。完整失败与最终命令均保留。产物tests/codex-nr-web-editor-20261105，logs同名、tmp同名；报告reports/nr-web-editor-20261105。运行库以现有测试供体硬链接只读复用，不新增整包/ZIP或复制工程；实际空间按最终回执，不以逻辑文件大小冒充物理占用。用户配置、主线和其他工作树保持，未merge/push/Release。

最终缺组件对照回归首轮165/1：测试快照只有effectCapabilities map、缺实际EffectSupport调用的effectAvailability方法，导致模拟模型控件始终可用。补齐同义只读查询后验证，不为夹具缺口改变产品可用性逻辑；此轮失败原样保留full-parity-final.log。

## 2026-11-05 NR调节方式归回参数组，补齐九项

用户纠偏后从684d906继续原分支，checkpoint/nr-web-before-inline-20261105存档。取消独立块、全局开关及灰色副本；NrLayerEditor回到一份参数树，两组独立list/web偏好。变化四轴与总量滑条、手动完整九轴与列表使用同一规格数组。删除旧NrParameterEditor/NrWebControls和只读自动map，旧全局偏好只作迁移，原自动控制保持。早期六轴/三滑条拆分不再是当前布局。

使用build-recording-handoff.py --task codex-nr-inline-editor-20261105 --root <活动树> --build-dir <共享构建> --label 2.1.2-nr-inline-test --reconfigure --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared构建五目标，73.9s通过。软件QtQuick170/0/0（38.98s）、GPU Pad13/0/0、分组控件11/0/0；UI合同/i18n/NR模型通过，catalog2739条缺译与占位0。真实主程序23项逐参数读回、模式独立、自动手动保留、截图及重启通过，退出0，engine/QML错误0。

用户旧测试PID23760在开工时运行，所以没有替换旧app或停止进程，另建tests/logs/tmp/codex-nr-inline-editor-20261105和reports/nr-inline-editor-20261105。运行库硬链接只读复用，未再复制完整工程/ZIP；配置迁移只复制白名单到新manual-profile，原配置不改。实际完整截图nr-web-ui.png已查看，九项标签均显示。单组截图保留透明背景，展示以整体深色面板为准。完整命令、最终SHA、空间及保护见FINAL.json；本轮不合main、不push/Release，不变更算法或导出协议，不宣称新的实卡性能收益。
## 2026-11-06 选框弹出文字“抖两下”（Claude）

分支 `claude/menu-jitter-20261106`，基线 main `f1f8298`，未提交。

**原因**：软件绘制（2.1.1 起默认）把文字横坐标向下取整到整像素。VMenu 打开的弹簧缩放在终点附近越过 1 三次（约 87、181、266 ms），每次越过都把选项文字左移 1 像素再移回，共两次。GPU 绘制下同一段弹簧是不到半像素的平滑移动，不跳。

**改动**：只在软件绘制下，弹簧第一次到达终点后，缩放和位移就停在终点；GPU 绘制以及 enter/exit 的曲线和时长不变。用户一度误以为弹簧被改掉、叫停，撤回后又确认“修复效果是正确的”，恢复原补丁。

**验证**：
- 菜单 QtQuick 测试：软件 100/150/200% 各 6/0，d3d12 100/150% 各 5/0/1。
- 全套 QtQuick：147/0/1；截图用例单独 3/0。
- 240 fps 录屏：软件原版 3 次都左跳 1 像素来回两次；修复后 3 次都一路靠拢、无回跳。GPU 修复后与原版相同。

**入口与记录**：手动入口 `tests/claude-menu-jitter-20261106/启动选框修复测试.cmd`。执行记录 `docs/MENU_TEXT_JITTER_2026-11-06.md`，回执 `E:/项目/Veyra/reports/claude-menu-jitter-20261106/FINAL.json`。

**未验证**：真实程序只在 100% 缩放下录屏；对话框等同类弹簧缩放没有测，也没有改。

## 2026-11-06 2.1.3整合与测试构建（进行中）

用户授权把Claude与Codex修复合入main并构建2.1.3测试。17工作树状态与改动SHA已保存reports/codex-build-2.1.3-20261106/BEFORE.json；Claude四文件原补丁/ZIP在archives同名目录。新菜单05181bf、NR259ef8f经e1bfbde/3556608整合，只在WORKLOG追加冲突保留双方；版本快照4fde59a，main已快进3e532a9。旧脏树不覆盖，撤销/暂停方向不恢复。
build-recording-handoff.py --task codex-build-2.1.3-20261106 --build-dir <共享构建> --reconfigure --label 2.1.3 --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=<prepared> 共23目标105.6秒通过，日志logs同名/build-20261106-041552，receipt为干净产品快照。后续只改打包脚本/文档，不改产品。
首次package-2.1.3.py stage在签名检查拒绝：当前PowerShell7继承的PSModulePath导致子Windows PowerShell无法加载Security模块；之前从当前shell直接核验两DLL签名均Valid。已按既有stage脚本清除该子进程PSMODULEPATH，不修改全局环境或放宽签名校验，随后重新执行。此时尚未生成app目录。验收最终结果待记录。

2.1.3打包首轮QtQuick发现QtQuick.Layouts/Shapes等模块缺失：新脚本原先排除了整个qml目录，错误地把Qt部署模块也排除。改为仅替换qml/Veyra，新增按正式供体逐项校验Qt模块；对尚未交付的两包只补回经SHA核对的原Qt模块，不重拷整包。第一轮日志/CHECKS-FIRST及payload-first保留。两个设置测试另因夹具提前创建了测试要求的新目录退出2，修正为单独output子目录；未修改产品逻辑或测试断言。

2.1.3修正后QtQuick172/0/0、菜单多DPI、NR GPU13/0+11/0、真实NR界面23项/重启、7组420帧SR对齐、双编码各270帧IDR/stss通过；详见BUILD_2.1.3_TEST_2026-11-06.md实际验证。额外pacing缺参数/输出目录夹具已纠正；冷DLSS初始化首帧15s门槛仍失败，热缓存14.3s生命周期通过、mediaSpeed0.99211；不声称解决冷启动。ACCEPTANCE.json区分首次失败与热结果。GUI观察Loader已逐字节恢复，不修改产品QML。

2.1.3两ZIP封包/逐SHA/CRC、解压最小PATH冷启动均通过（NVIDIA209/AMD207真实帧，12.5/12.61s，退出0、错误为空，RTX5070效果关闭）。SOURCE-FREEZE为b19846a，后续仅验收文档，当前产品与归档一致。最终交付及保护/清理以reports/codex-build-2.1.3-20261106/FINAL.json为准，未push/Release。

自动审批拒绝了验收解压副本递归清理和同批分支引用整理命令（CreateProcess前拒绝，原文仅blocked by policy）。没有执行删除、没有换工具绕过；两份verify副本和两个已合并分支引用仍保留。CLEANUP.json记录路径与逻辑字节，不宣称空间已回收；交付包、源码、main及全部验收结果不受影响。

## 2026-11-06 2.1.3默认中档／Boost红字重构建

用户追加要求超分默认中、低延迟警告红字且只在开＋增强出现、删除旧构建产物后重建。产品b143ebf：kDefaultVideoSrQuality=2，双重默认共用；列表/节点fresh及srQualityFor无既有档位回退一致；既有显式选择保留。NvidiaLowLatencyEditor仅available+Boost显示Theme.err警告，Off/On隐藏，缺硬件普通说明保留。更新既有UI合同及QML交互回归，没有改驱动逻辑/运行库。
新reports/codex-rebuild-2.1.3-20261106/cleanup-old.ps1先生成计划再-Execute，在已核验E盘根范围内只删除SHA匹配的构建文件，不递归删除非空目录、不跨Shell、不停止进程。9422文件逻辑8,000,253,348字节已unlink，旧交付包和验收副本已退休；保留未知/配置/日志、少量测试源码、共享构建缓存、依赖和正式版。净物理回收未测。旧interactive-NVIDIA配置仍在原位。这次有用户明确删除授权并缩小到文件白名单，自动执行通过；上轮拒绝历史保留。
后续新产物用codex-rebuild-2.1.3-20261106；构建与测试结果待FINAL.json，本记录不假定通过。完整计划docs/REBUILD_2.1.3_DEFAULTS_2026-11-06.md。

## 2026-11-06 默认中档/Boost红字重构建验证

## 本轮实际验证（2026-11-06）

产品源码7a2d15d，PE 2.1.3.0。9目标首构建127.6秒；发现预设写入格式对第二次超分默认值写死4，修正PresetStore/PresetLibrary的五处判定为统一常量后，最终增量构建11.8秒。没有改写已有明确保存的档位。

最终设置、效果链、预设库、NR模型、UI合同/i18n及翻译提取检查通过；完整软件QtQuick173/0/0，GPU低延迟提示6/0/0。实际程序新配置基础/第二次SR均为中档，三种RTX版本在没有原档位时均为中档，明确高档值保留；列表和节点逐项验证关/开不显示警告、开＋增强显示红字。截图已查看。保存4/19与低延迟“开”后重启恢复通过，无引擎/QML错误。

保留失败记录：首次旧默认判定导致设置/预设测试失败（一个测试进程退出生成转储），修正后所有相关检查通过；仅本测试的转储经路径证据和SHA核对搬至E盘日志。中间一次完整Quick有5个既有菜单几何断言失败；同源码/最终程序的菜单独立14/0和完整173/0复测通过，未确定其间歇原因，也不宣称本轮修复菜单。真实UI夹具先误用旧interfaceMode键、继而遗漏节点模式首次空图，已纠正夹具并重新用私有配置验证；不改变产品迁就夹具。

上一轮产物逐SHA清理9,422文件，逻辑8,000,253,348字节，不等于实际磁盘净释放。旧个人配置/日志、依赖、历史正式包保留。新包路径test-packages/codex-rebuild-2.1.3-20261106，独立启动器使用新的测试配置。ZIP逐文件核验、最小PATH解压播放的后续结果以本任务FINAL.json为准。NVIDIA/AMD两包本机RTX5070默认效果关的冷启动不代替AMD推理或实卡采集验收。未push/Release。

复现命令：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-rebuild-2.1.3-20261106 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.3 --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_qml_ui veyra_amd_nr_worker veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_video_ai_settings_tests veyra_effect_chain_tests veyra_preset_library_tests veyra_nr_model_settings_tests`。检查脚本reports/codex-rebuild-2.1.3-20261106/check.py、ui_runner.py/defaults-ui.qml/defaults-restart.qml；实际命令、环境、首败/复测和日志路径在ACCEPTANCE.json及logs同任务。封包命令为`python -X utf8 -B scripts/acceptance/package-2.1.3.py seal`及`cold`。

## 封包与清理实际完成

应用源码归档1177696、产品7a2d15d；两ZIP逐文件SHA/CRC与独立解压最小PATH播放通过（NVIDIA=207真实呈现帧, AMD=208真实呈现帧，默认效果关闭，本机RTX5070）。最终包、源代码与网络源码摘要见test-packages/codex-rebuild-2.1.3-20261106/DELIVERY.json。两包无个人配置/日志/测试程序/SDK。临时验证副本逐SHA清理5541文件，新旧交互配置、日志与最终包保留。系统自动生成的本次单测转储经路径与SHA确认已搬至E盘日志。Git仅验收文档在归档后继续更新；未push/Release。完整实际命令、首次失败、修正和日志在本任务reports/logs。

本轮临时副本清理预检首次被Windows PowerShell 5.1按本地编码误读UTF-8无BOM脚本，路径解析失败即退出，未执行删除；只给临时脚本补UTF-8 BOM后预检通过，逐SHA删除5541文件。最终交付文件与源码QML再次逐SHA核对通过，Main.qml无测试Loader残留。

## 2026-11-06 用户授权2.1.3公开发布

目标Likely7/Veyra-NRVideo，发布计划docs/RELEASE_2.1.3_EXECUTION_2026-11-06.md。2.1.2以来提交已逐项核对；原图已查看并原样复制docs/images/2.1.3/sr-comparison.png。保留群8/赞助各220；沿用重构建程序，发行仅更新文档/元数据。报告reports/codex-release-2.1.3-20261106。

## 实际公开与远端核验完成

发行准备0a09d376a7d0a185bdb1d1a54b1b666dc25e7a60，产品7a2d15d9222a98fac69e5343c01b443e1d7060be。main与v2.1.3/v2.1.3-sources已原子推送；两个Release均为公开正式发布，运行版为Latest。7个资产服务端SHA256/大小/状态及匿名下载HEAD200、群8/赞助各width220、超分原图字节、远端README/标签/Latest核验通过；4份固定依赖对应源码摘要和可访问性核验通过。

Veyra-2.1.3-NVIDIA-win64-portable.zip：726570478 bytes，SHA256 `464a46ac1874acf36e43f7d3f53db336c3e7a0c59f156237d8226d94e63f7b7f`。

Veyra-2.1.3-AMD-win64-portable.zip：717577602 bytes，SHA256 `dc1223f0aff0feb397f2c40706a3f608b9e147d5390892d7631295759e17a692`。

应用源码SHA256 `52e73bfa40422238f3fe35f7d3fbb0188c2ff759671cb6bda90a782824161d5b`。实际执行scripts/acceptance/release-2.1.3.py；git push --atomic nrvideo main refs/tags/v2.1.3 refs/tags/v2.1.3-sources；reports同任务publish.py body/drafts/sources/packages/publish/verify。AMD首次上传被原300秒脚本上限中断，服务端无残留；延长至1800秒重试，既有NVIDIA资产按摘要跳过。每个上传先核验远端摘要，再公开；源码先公开，运行版后设Latest。产品字节与供体ZIP逐文件一致，不重复占用采集卡；新产物仅在E盘。全部ZIP清单与SOURCE校验通过，无专有二进制/SDK混入源码Git，无用户配置进入运行包。群公告810字符，文件reports同任务GROUP_ANNOUNCEMENT.txt，未代发群消息。

## 2026-11-06 RTX3080三种RTX超分跳回DLSS日志诊断


用户日志35447行，SHA256 2975caad86b74be9ce09be500176a700bbbcc345db1e6eea7b6fd58df8c91df3；RTX3080 10GB，驱动32.0.16.2101。用户明确新目录解压，不归因覆盖残留。

日志第996行选择高码率，997行却accepted quality=0；第20228行从FSR选标准，20229行同样accepted quality=0。其他RTX点击没有提交对应2/17/21，未看到RTX模型创建失败。DLSS/FSR正常提交和创建。注意日志Z时间为UTC11月7日，对应客户端太平洋时间11月6日；原证据保持UTC。

源码与已发布NVIDIA ZIP核对，ProPage/NodePage均使用srQualityFor(string family,tier)；旧2.1.2 ProPage则Number(id)。本地RTX5070私有应用关闭所有效果、不打开媒体，实际菜单点击RTX三种与DLSS/FSR分别得到2/17/21/0/5，全部通过。只在私有ProPage换回旧Number(id)处理器，同样真实菜单点击得到0/0/0/0/5，与用户日志高度吻合。正式包无qmlc/jsc文件。此对照不是用户实际文件内容的证明，也不是RTX3080推理验收。

当前定位为模型ID在选择链路被错误转换的强证据，不能把责任归于GPU算力、驱动或缺DLL；下一步核对用户实际加载的qml/Veyra/ProPage.qml、NodePage.qml及EXE SHA256，确认文件和加载来源。未改产品、不发覆盖补丁、不推送或发布；不要求反复换驱动/重跑模型。证据reports/sr-menu-investigation-20261106/FINDINGS.json，命令ui_runner.py配menu-real.qml/menu-legacy.qml，实际日志logs同任务。

首个QtTest夹具when:windowShown在独立应用中未触发，已停止唯一私有路径测试进程并改Timer显式调用测试方法；两次有效检查均exit0，无engine/QML错误。未碰用户运行进程、配置和原日志。仅作为受控对照把旧处理器放入私有QML，已恢复原字节。

本轮删除SHA白名单隔离测试副本的命令被自动审批以blocked by policy拒绝，未执行；保留tests/sr-menu-investigation-20261106/app，不改shell/工具绕过，配置和日志保留。证据CLEANUP.json。产品代码和已发布包均未修改。

## 2026-11-06 2.1.3 选不了三种 RTX 视频超分：每个构建使用自己的界面编译缓存（Claude）

**状态**：分支 `claude/qmlcache-20261106`，基线 main `edd901e`，未提交。

**排查**：见 `E:/项目/Veyra/reports/claude-sr-qmlcache-20261106/FINDINGS.md`。
- 发布 ZIP 把文件时间统一成 1980-01-01，而 Qt 界面编译缓存只按“路径＋时间”判断是否过期。
- 所以 2.1.3 覆盖 2.1.2 后，沿用 2.1.2 编译的 ProPage，`Number(id)` 把三种 RTX 模型变成 DLSS。

**修复**：新增 `QmlDiskCache`。
- 启动时按 `qml/Veyra` 的内容摘要，把 `QML_DISK_CACHE_PATH` 设为 `<缓存>/qmlcache-<摘要>`。
- 外部明确设置的路径或关闭缓存的设置保留。
- 系统代码页无法表示路径时关闭缓存。
- 日志新增 `[qml-cache]` 一行。

**验证**：
- 构建 301.0 秒通过；`veyra_qml_cache_tests` 通过。
- 真实程序在“2.1.2 → 2.1.3 原地覆盖”的安装里：
  - 未修复程序：宽 150，提交 0/0/0；
  - 修复版：首次重新编译、宽 210，提交 17/2/21；第二次复用缓存；
  - 同大小、同时间地改动 ProPage 后，立即换目录并生效；改回后复用原目录。

**记录**：`docs/QML_CACHE_PER_BUILD_2026-11-06.md`，回执 `E:/项目/Veyra/reports/claude-qmlcache-20261106/FINAL.json`。

**未验证**：未在用户 RTX 3080 上验证，代码页回退分支未实测，未重跑全套 QtQuick。


## 2026-11-06 Issue #88：RX 9060 XT 开 AMD NR 第一帧崩溃，改为 HIP 子进程自检（Claude）

**状态**：分支 `claude/issue88-hip-preflight-20261106`，基线 main `edd901e`（代码与 2.1.3 `7a2d15d` 相同），未提交，未在 issue 上回复。

**证据**（报告人 zip）：14 次崩溃都在 `amdhip64_7.dll+0x4A40A0`，都是读地址 0x390。
- 自写 minidump 读取脚本显示，崩溃线程是 HIP 内部线程；同一时刻，播放线程停在 lmxxf 网络初始化的 `hipStreamCreate`（`LmxxfNrRuntime.dll+0xA4A2E`，前后操作名 hipInit/device/stream）。
- HIP 和 COMGR 都来自驱动的 System32。
- 上游 lmxxf issue 18：另一台 RX 9060 XT 在 Magpie 里能正常跑同一套内核。Veyra 进程默认处于 Realtime GPU 调度等级，是最可疑的差异，未证实。
- 本机核显 + HIP 6 在四种进程状态下创建流都正常，不能代表 RDNA4。

**修复**：新增 `AmdHipPreflight`。
- `LmxxfNrBackend::open` 在 Create 之前，让本程序以 `--amd-hip-preflight` 子进程模式先在同一块卡上执行 D3D12、调度等级、hipInit、按 LUID 选卡、hipSetDevice、hipStreamCreate、显存清零。
- 当前等级失败时依次试 High、Normal，第一个通过的等级通过新的 `setGpuPriorityComputeCap` 应用到本进程。
- 全部失败则拒绝 AMD NR，播放继续。

**验证**：
- 构建 295.5 秒，增量 9.6 秒。
- 新单测在本机核显上全部通过：真实调度等级 5→4→2→5；正常、只在 Realtime 崩溃、每档都崩溃、缓存、缺库。
- 原 AMD 合成图 128/0、lmxxf ABI 66/0。
- 真实 exe 子进程模式无钩子退出码 0，加钩子 0xC0000005；普通启动冒烟正常。

**交付**：测试补丁 `test-packages/claude-issue88-20261106/Veyra-2.1.3-issue88-test.zip`。

**记录**：`docs/ISSUE88_AMD_HIP_PREFLIGHT_2026-11-06.md`，报告 `E:/项目/Veyra/reports/claude-issue88-20261106/`。

**未验证**：RDNA4/HIP 7 实卡结果要等报告人运行；本机走不到 lmxxf 真实路径。

# Claude两项修复合并与组合验收（2026-11-06）

用户要求合并Claude最新修复到main。开工main edd901e，存档checkpoint/pre-claude-fixes-merge-20261106。工作树仍为E:/项目/Veyra/worktrees/screen-recording-fix-20261022。

- claude/qmlcache-20261106：将原未提交7文件保存为a66f7e9，merge a625787。QML内容摘要隔离编译缓存；保留外部环境设置，不删除旧缓存。
- claude/issue88-hip-preflight-20261106：原未提交10文件保存为b15152b，merge f3b103d。AMD lmxxf建立前运行独立HIP自检，按通过的等级限制调度，全部失败拒绝开启NR。RX9060XT现场根因仍未证明为Realtime，属于崩溃前置防护，不宣称已在该卡治愈。
- CMake/main.cpp自动合并成功；唯一WORKLOG冲突按原始共同前缀保留两边完整追加内容。原两个工作树干净，无SDK/DLL/模型入Git，不恢复暂停Lecram或撤销预算/RIFE。外部NR优化只是调研，没有产品代码可合。

组合构建f3b103d，8目标86.8秒通过，标签2.1.3-integration-test，复用build/claude-screen-recording-20261022；不是公开正式包。实际命令在logs/codex-claude-merge-20261106/build-20261106-204917/receipt.json，入口build-recording-handoff.py --reconfigure --jobs 2，目标qml_ui/qml_cache_tests/amd_hip_preflight_tests/lmxxf_nr_tests/amd_nr_graph_tests/qml_quick_tests/ui_contract_tests/ui_i18n_tests。

验收：QML缓存单测通过；HIP自检本机核显/HIP6真实子进程及调度5→4→2→5、单档崩溃降级、全部崩溃拒绝、缓存和缺库路径通过；UI合同、i18n/提取通过；完整软件Quick173/0/0。AMD合成图128/0（HDR108、D3D12调试层0错误）、lmxxf ABI66/0，不是RDNA4推理。

合并主程序实际--amd-hip-preflight：正常exit0/done ok，受控stream崩溃exit0xC0000005，未启动Qt界面。实际普通程序使用隔离配置、关闭所有效果且不打开视频，开启QML编译缓存进行真实下拉点击，首次和再次均为RTX标准2/高码率17/流媒体21，DLSS0、FSR5；无engine/QML错误。再次启动的ProPage缓存mtime与SHA完全不变，证实复用。私有Main探针已恢复源码原字节。命令check.py、amd-regression.py gpu --task codex-claude-merge-20261106、ui_runner.py配menu-real.qml；日志和JSON在reports/logs同任务。

本轮首个缓存单测调用漏传必需的新scratch目录，exit2 usage，已补参数单独复测通过，保留CHECKS.json首败与QML-CACHE-CORRECTED.json，不重跑已通过无关检查。未碰用户运行进程/配置，未发布新包、未push；当前GitHub仍2.1.3。现有测试副本保留作本轮可用组合构建，不声称删除或回收。真实RX9060XT/HIP7推理仍需现场验收。

此前Codex私有UI测试设置QML_DISABLE_DISK_CACHE=1，验证了ID映射却漏掉升级缓存条件。Claude在保留缓存的真实跨版路径复现了旧ProPage执行Number(id)并产生0/0/0。今后界面升级验收至少包含开启缓存的首次/再次启动及同路径跨版升级，不以禁用缓存的孤立通过替代。


## 2026-11-06 项目产物安全清理

删除122372文件，逻辑53.72 GiB，E盘实测可用净增33.73 GiB；项目路径累计152.89→99.28 GiB。34关键文件/正式与源码ZIP SHA不变，19工作树原HEAD/修改清单不变，依赖/Junction/用户配置/当前build与app保留。只清编译中间文件、旧测试部署与未发布包、私有缓存、旧性能读回；保留修改与用户数据候选315项，首次预检20个Profile命名obj保守排除。单文件1.4.0旧下载ZIP删除被自动审批blocked by policy拒绝，未执行、未绕过，仍保留。详情docs/PROJECT_CLEANUP_2026-11-06.md和reports/codex-project-cleanup-20261106/FINAL.json，命令及逐文件证据在同任务。产品代码/正式包不改，未push/发布。

## 2026-11-07 NR 推理复用（自动推理复用，Claude，未提交）

**来由**：用户不复制、不移植 SAOG0721/Magpie 代码，选了自研方案 A，要求开关名为“自动推理复用”，另提供手动 1/2、1/3、1/4。

- 分支 `claude/nr-reuse-20261107`，基于 main 0079ed9；
- 工作树 `worktrees/claude-nr-reuse-20261107`，构建目录 `build/claude-nr-reuse-20261107`；
- 没有动共用施工树和当前可用构建。

**行为**：

- 只用于实时预览。复用帧不跑 NR，输出“本帧源 + 上一推理帧的 NR 变化”，变化按光流搬运。
- 变化拆成两部分：邻域均值按 3×3 邻域是否匹配决定是否搬；细节只在本像素细节也一致时才搬。对不上、出界或在保护区内时显示原图。
- 复用帧之后一定重新推理。那一帧给 NR 和时域稳定器的运动换成串接后的运动。
- 手动 1/2、1/3、1/4 表示每 2/3/4 帧复用 1 帧。
- 自动由 AutoNrReuseController 选档：平均增强耗时对比 80% 帧间隔，超预算 0.5 秒升档；预测降档仍低于 0.85 倍预算并持续 3 秒才降档；最多每 2 帧复用 1 帧。
- 开关切换会重建图，档位之间实时切换。
- 暂停时若最后一帧是复用帧，补做一次完整推理重绘。
- 导出、图片、预热图、最近图缓存、COMPUTE 准入和自动 NR 尺寸池都不收复用配置。

**验证**：

- 新单测 33/0，新 GPU 测试 7/0（调试层 0）；
- QtQuick 172/0/1；
- 设置、预设、合同、翻译、抗闪烁、稳定器 GPU 回归通过；设置事务和 AMD 合成图 128/0 在运行目录内通过。
- 真实程序：
  - 1 层 4K60，1/2 时平均增强耗时 4.49 → 3.11 ms；
  - 4 层超载，自动约 2 秒升到每 4 帧复用 1 帧，15.24 → 11.64 ms，跳帧停止；
  - 实时切档和暂停重绘通过。
- 画质（对比每帧推理）：720p 真实平移，复用帧平均差 0.70/255、PSNR 45.7 dB；合成测试卡上差异集中在随机噪点块和移动细线。

**记录**：`docs/NR_INFERENCE_REUSE_2026-11-07.md`，回执 `E:/项目/Veyra/reports/claude-nr-reuse-20261107/FINAL.json`。手动入口 `tests/claude-nr-reuse-20261107/启动推理复用测试.cmd`，独立配置。

**范围**：未打包、未提交、未合 main、未 push。只在本机 RTX 5070 验证，其他显卡、HDR、节点模式、采集卡未验证。

## 2026-11-07 NR 推理复用界面调整、提交 fps 的 1% low、色温冷端修正（Claude，未提交）

同一工作树 `worktrees/claude-nr-reuse-20261107`，按用户截图反馈修改：

- **NR 推理复用**：
  - 去掉“已复用 N 帧”；
  - 卡片改名“NR 自动推理复用”；
  - “输出稳定器 · 抗闪烁”“NR 自动推理复用”“NR 全局保护区域”挪进“NR 画面增强”，与 NR 层并列；
  - 复用帧给 NR 阶段记零长度计时样本，底部阶段耗时的平均随复用实时下降（4 层 4K60：NR 第4层 3.50 → 1.77 ms，总 16.24 → 8.36 ms），P95 仍是单次推理；自动档改用 P95 估算单次推理成本。
- **图标**：两张卡的 `shield` 图标原本不在图标库，一直空白，已补 Lucide shield 并重新生成 IconData。
- **帧率卡**：“显示 fps”从未接入测量、永远未测，已删除。RTSS 也是按 Present 计帧，等同于提交 fps。
  - 现在提交 fps 是主数字，旁边显示 1% low，算法为最近 10 秒 Present 间隔的第 99 百分位；三处界面统一；
  - 实测“60.0 提交 fps | 39.5 1% low”。
- **色温**：普朗克曲线 2222–4000 K 段 y 常数项写错（−0.0317，应为 −0.1675），−100 时蓝通道 ×23、在约 −70 处突跳。已修正，并把冷端按迈尔德与暖端镜像（−100 = 4333 K），暖端不变；真实画面 −100 偏冷但不爆。

**验证**：
- 新单测：NR 复用 33/0、播放计时 164（含 1% low 5 项）、色彩表 54（含色温 3 项）；
- 色彩 GPU 通过，slot 回归在运行目录内 12/12，QtQuick 172/0/1，翻译 0 缺失。

**记录**：`docs/NR_INFERENCE_REUSE_2026-11-07.md`、`docs/FPS_LOW1_AND_COLOR_TEMPERATURE_2026-11-07.md`，回执 `E:/项目/Veyra/reports/claude-nr-reuse-20261107/FINAL.json`。

**范围**：未提交、未合 main、未 push；只在本机 RTX 5070 验证，没有与 RTSS 读数逐帧对照。
