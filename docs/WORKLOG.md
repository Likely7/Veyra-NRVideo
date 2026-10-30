# 当前工作日志

完整旧日志：[截至本轮整理前的记录](history/worklog/WORKLOG_before_maintenance_2026-10-27.md)。当前状态见[CURRENT_STATUS](CURRENT_STATUS.md)，历史授权见[历史索引](history/INDEX.md)。

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
