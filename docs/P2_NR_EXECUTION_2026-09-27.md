# P2：NR 多层与旧链兼容执行记录

## 当前结果：P2非防闪切片已验收，防闪延期（2026-09-27）

- 本轮交付：1–4层NR各自显示/启停/编辑/复制/删除；模型6项、残差5项及实验参数入口可达；每层内部尺寸独立，新建默认1080p。六档沿用1.4.4枚举，不放大小输入，不改变最终输出尺寸。肤质参数只允许未指定(-1)或自定义0–2，不再产生非法(-1,0)值。
- 尺寸已进入每层settings、EffectChain、GraphDescription、NR实际纹理/NGX调用、尺寸重建及motion适配，不是把全局控件重复显示。光流仍共享计算；运行时选择和NR/SR顺序仍按既有全层合同。未修改本轮防闪shader、解码、音频、采集或Present链路。
- 保存合同：PresetStore v23保存多层参数/启停/尺寸并保留多调色；旧flat配置保留明确全局nrPolicy；PresetLibrary v3保存逐节点尺寸并保留v2颜色。旧library v1/v2没有保存的尺寸只能默认1080p，不能声称恢复不存在的数据。复制继承原层尺寸。
- 最新独立候选：`E:/项目/Veyra/tests/p2-nr-ui-resolution-20260927/final-candidate/veyra_qml_ui.exe`；EXE SHA256=`967e2f0cf3e18f2dbd010f99dba5f2d4e81f39eca2dd0e559368e2a16de7f8d7`。用户正在试用的temporal候选、其数据目录和原桌面checkout未替换。

### 本轮实际证据

所有编号来自`E:/项目/Veyra/logs/p2-nr-ui-resolution-20260927/commands.json`，失败记录保留，不把单项成功扩展为全部画质/硬件验收。

| 证据 | 结果与边界 |
| --- | --- |
| 002/006/010/021 构建 | UI、QuickTest、EffectChain/两类预设测试及GPU probe构建通过；021为最终增量构建 |
| 003–005 数据单元测试 | 六档映射、默认值、四层尺寸、尺寸重建、旧配置迁移、复制保存及4K离线完整尺寸合同通过 |
| 014–016 三组真实GPU | 四层混合尺寸、NR先行、SR先行均8帧；4独立参数/代理/输出，32次NR Evaluate，共享光流、逐次纹理尺寸及同帧层间零差异断言通过，debugErrors=0 |
| 018/024 Qt Quick | 生产NrLayerCard控件、fixture数据；鼠标下拉/滑条、全部参数入口、独立索引和展开状态：11 passed / 0 failed；这部分不冒称引擎测试 |
| 020 真实播放器 | 独立数据目录，真实鼠标修改第1层且选中索引故意指向第4层，确保不串层；实际尺寸选择、复制/删除/旁路、保存及第二进程重启恢复通过。`real-player-v2/result.json`为success=true、main_restored=true |
| 025/026 离线GPU尺寸 | exportJob/stillImage各8帧，四层实际内部尺寸保持完整1920×1080；不是完整NVENC文件导出或4K实机导出验收 |
| 027 单层结构回归 | 8帧/8次Evaluate、共享光流、实际纹理尺寸、debugErrors=0；不替代单层性能或视觉验收 |

### 本轮失败归因与收口限制

- 最初构建请求了不存在的`veyra_export_worker` target，纠正目标列表；Qt测试类型注册与布局屏障问题在测试侧修复，保留原断言，使用`waitForPolish`等待真实布局；真实播放器脚本首轮因CRLF导致测试import未注入，脚本修正后通过，产品Main.qml没有修改。
- 023干净staging控件测试失败查明是构建目录残留旧ProPage：CMake只在UI target链接后的POST_BUILD复制QML，单改QML不会触发复制；stage再从build/qml拷贝。按源码同步本轮build及final候选后，024原测试通过，未放宽断言；旧新SHA见`final-qml-sync.json`。本轮未改CMake或stage脚本。后续每次staging必须显式同步源码QML并逐文件验hash，不能仅凭增量build成功认为界面最新。
- 真实播放器退出日志仍有`core-host: destroying leaked parameter block at shutdown`；旧`p2-nr-temporal-repair-20260927/qml-interaction-v2/engine.log:183,279`已有同文警告。本轮未修复该所有权诊断，不声称零warning，也不能仅据该文案推定新增GPU泄漏。
- **防闪按用户要求延期，不修改其算法、不重置原两次修复计数、不宣称画质通过。节点无法点入、列表/节点隔离与节点执行仍属P3前置未完成；17屏/42动效完整还原属P5未验。** 本轮测试不代表长稳、其他显卡、实屏HDR或4层60fps通过，完整P2/2.0.0不标全完成。
- 非防闪目标到此收口，后续不自动重开循环、不自动推进P3；无commit/tag/merge/push/Release或删除旧UI。下方保留本轮开工与此前阶段记录；其中“未实现/只登记”不再代表当前非防闪功能状态。

## 本轮开工范围（历史记录）

- 用户本轮明确开启目标完成P2剩余项，防闪延期。本节覆盖下方历史“仅登记尺寸接口缺口、不施工”的状态；只为本切片必要接口解除冻结，不进入P3/R5.4/R5.5。
- 顺序：逐层尺寸与预设合同→分层完整参数界面→定向构建/数据与GPU/控件交互回归→独立候选和证据收口。不修改正在手测的candidate-v2；防闪代码/既有两次修复计数冻结。
- 旧guard通过后保存25个许可路径的写前文件及新的独立baseline：`E:/项目/Veyra/archives/p2-nr-ui-resolution-20260927/scope-baseline.json`，SHA256=`7a3f811468e76b545a6e88284f14953e4a7da75e71f757c3ebfe9e9d39e781ce`。沿用原guard实现，不改旧baseline或guard，不放开source/decode/audio/FG/shaders。
- 产物限定 `E:/项目/Veyra/{tests,logs,tmp,archives}/p2-nr-ui-resolution-20260927`；复用 `E:/项目/Veyra/build/p2-nr-20260927`，测试≤300秒/构建≤900秒，源码前后身份与候选分别记录。

## 2026-09-27 用户追加：NR处理分辨率放入每个实例参数

- 用户要求：处理分辨率并入单个 NR 的参数，默认 1080p 内部处理，可选范围与 1.4.4 一致；列表各层及后续节点参数面板使用同一合同。这是本轮分层整改的必需项，不归为纯视觉润色。
- 已核对标签 `v1.4.4` 的 `apps/veyra/SettingsWindow.cpp:1309`：选项为“1080p NR · 实时默认”“原生NR · 高性能成本”“480p NR”“720p NR”“900p NR”“1440p NR”。展示可按 480/720/900/1080/1440/原生排列，必须保持旧枚举映射：Realtime=0、Native=1、P480=2、P720=3、P900=4、P1440=5；不得按新显示顺序直接写旧枚举。
- 标签 `v1.4.4` 的 `include/veyra/pipeline/ResolutionPlan.h:12–15,31–52` 定义真实语义：1080p 是内部尺寸上限（1920×1080 包围框），保留比例且不因选择档位而强制放大小输入；其他 p 档同理。原生指对应链位置的未降采样尺寸，不固定冒充 4K。NR 内部尺寸与最终输出/SR目标尺寸分开显示和保存。
- 每个新建 NR 层默认 Realtime/1080p；修改只影响选中层。复制继承被复制层的档位，不重置为默认；切换选层、删除邻层、保存/重开和列表/节点状态恢复不能串层。已有明确配置保留；旧预设只有全局 nrPolicy 时须显式迁移到原有层，没有保存值才使用 1080p 默认，不静默覆盖用户的原生或其他档位。
- 沿用 1.4.4 的预览/导出边界：预览内部降采样不得偷偷限制图片处理及最终视频导出的完整处理尺寸，不改变输出分辨率；导出例外要在控件说明中写清楚。
- **现状与接口缺口（未实现）**：当前 `EnhancementSettings::nrPolicy` 是全局字段，`ChainNrParams`/`NrLayerSettings` 没有每层 size policy；`describeStages()` 只产出一组 `desc.nrWidth/nrHeight`，`describeNrLayers()` 未传递每层尺寸。QML bridge 也没有对应的逐层分辨率属性。仅把全局控件摆进每层卡片会制造假独立，禁止这样交付。当前只登记该缺口，按 UI-only 的接口边界停止产品写入，不擅自修改引擎/管线或扩大旧 guard。
- 最小实现切片应覆盖：每层参数→bridge→链描述→各 NR 实例尺寸→实际纹理/历史资源→预设迁移。修改尺寸须正确重建及 reset 受影响的实例和依赖历史，同时保留其他层的参数；不借此更换播放/解码/呈现链路。实际实施前依当时有效范围建立独立增量 baseline，旧保护记录不改写。
- 验收：新建层默认1080p、六档及原生映射、四层不同档位的真实内部尺寸/输出尺寸、鼠标选层与修改、复制删除不串层、保存重开/旧预设迁移、低延迟前后顺序和导出完整尺寸。UI文本变化或直调 setter 不等于真实分辨率已改变；未经实际执行不打勾。
- 本次仅完成旧版核对和需求/缺口入档，未修改产品代码、构建或替换手测候选，不声称该控件已可用。

## 2026-09-27 用户补充：节点入口与列表修复必须一起对齐

- 用户反馈节点面板点不进去。该项是功能可达性缺口，不归为 P5 纯视觉问题，也不能因为节点执行属于 P3 而漏记。
- 静态接线：`ProPage.qml` 的节点按钮调用本页 `switchDialog.open()`，确认后才请求换页；按钮没有显式禁用。此处 Controls `Dialog` 未像既有 `DialogHost` 面板一样声明 `videoCover`，而 `apps/veyra-qml/main.cpp` 的原生视频避让依赖该标记，因此存在被原生视频遮挡的接线缺口。尚未通过运行时点击/几何确认它就是本次“点不进去”的唯一原因。
- 未闭环：`Main.qml` 换页只同步 `currentPage`，`NodePage.qml` 直接消费 `veyra.chain`；`setNodeMode` 当前只改 mode 并通知，没有在该切换路径上保存恢复两套模式状态。不能把换页或弹窗文案当成独立节点执行链已经实现。
- P2 列表修复必须核对节点面板可共用的参数定义、范围、默认值、启用状态和选层语义。四层各自可辨认；复制/删除后的选中对象不串层。不另做一套节点 NR 参数，不把数组下标当跨删除/模式切换的持久身份。新增身份/模式快照接口若缺失，登记 P3，不在当前改执行器。
- P3 增加入口与模式隔离前置关：真实鼠标打开确认框（有视频/无视频）、取消不变、确认进入正确页面、选节点打开对应参数、返回列表恢复原设置；失败明确反馈且不串改列表。独立模式与真实执行合同具备后才能标节点可用，禁止只放开入口制造假完成。
- 本轮只更新方案和记录；未改产品代码、重建、关闭或替换用户手测候选。节点入口尚未修复，节点执行未验收；不据此启动 P3/R5.4/R5.5。

日期：2026-09-27。状态：用户已解除迁移必要修复的范围限制；防闪shader已获授权并完成两次有反证的最小修复。**暗部异常负值已消除，75秒四层对照的明显暗墙网纹不再出现；125秒亮粒子/渐变仍有细轮廓，P2画质未通过，Goal不标完成，不推进P3。** 旧“等待接线/shader授权”均为历史，不再请示相同授权。

## 用户手动试用补录：P2不只剩防闪（2026-09-27，最新）

- 用户确认四层叠加能看到效果；这是本机手动反馈，不替代独立参数、全部路径或画质验收。用户截图 `C:/Users/123/AppData/Local/Temp/codex-clipboard-f05f0457-83bf-456e-ba78-9a1e25ee6686.png` 显示“第4层”共用参数面板，只有“模型强度”可见，没有每层独立展示，也没有模型参数/实验分组入口。
- 只读核查：`ProPage.qml` 目前仅实例化一个NR编辑Accordion，绑定`selectedNrLayer`；不是四个独立层编辑卡。局部明暗、局部结构、肤质及防闪/低延迟的控件代码仍存在，不能说这些功能已经从引擎删除。直接使用的两个`VSubGroup`缺少`Layout.fillWidth`，且组件没有`implicitWidth`，是分组不可见的具体布局嫌疑；尚未取得运行时几何或修后验证，不宣称根因已确证。
- 本轮已核对候选`ProPage.qml`、`VAccordion.qml`、`VSlider.qml`与隔离区源码相同。此前17步测试是Timer直调真实bridge+engine，不能证明用户能看见、用鼠标到达每个控件，不能代替逐层手动交互验收。
- **更正剩余项**：除防闪画质外，P2还欠NR分层的清晰识别与独立编辑入口、既有NR参数的可见可操作性，以及真实界面选层→修改→切层→复制/删除/启停→检查仅对应层变化的闭环。这些是P2功能可用性，不可统统留到P5视觉精修。参数完整性须对照已批准设计和旧功能清单，不新增未经验证的模型能力。
- 样式、17屏与42动效的全面还原仍归P5；节点真实排序归P3，不混入本次P2。10/30分钟长稳、其他显卡与实屏HDR仍列为未验边界；已有短测不作长期或跨硬件承诺。
- 本轮只修正文档状态，没有修改产品/QML/shader、没有新建构建或GPU样本，没有关闭正在运行的手动测试窗口或更改用户配置。防闪同因两次修复的计数不变；P2不标完成，不自动推进P3。

## 防闪必要修复：已封存的实际结果

- 工作区及分支不变。新不可变baseline：`E:/项目/Veyra/archives/p2-nr-temporal-repair-20260927/scope-baseline.json`，SHA256 `b2636ea6d185bbf666d2bae47287944a84d5e7245ccb663a0ef5d1868750c509`。未改旧baseline或guard。授权“解除，为了新版的迁移，需要的合理部分都可以解除，不需要再问我”已落实于AGENTS与新白名单。
- 本轮产品改动仅`shaders/NrTemporal.hlsl`，测试改动仅`tests/integration/NrTemporalGpuTests.cpp`。原图、解码、音频、采集、FG、NR强度、80ms历史权重、默认开关和层间调度未改；没有全局裁掉signed HDR。
- **第一项缺陷**：旧的负历史残差重新加到更暗的当前base，能将原本非负的SDR通道变成负数。005 GPU反例三项失败且debugErrors=0；007同一反例转绿。对非负base/raw使用与现有阴影安全合成同类的连续软限制，并保存真正输出对应的残差。合法负base/raw保持signed。v1的异常负值消失，但网纹仍在，未判完成。
- **第二项缺陷**：在线性暗部使用绝对guide容差，会将四倍亮度差仍作为可复用观察。012新增独立GPU反例失败；014转绿。只将guide距离改为有符号、HDR有界映射后的sRGB感知域；工作纹理/残差/输出仍在线性域，未改颜色链。稳定暗部仍平滑、变化暗部拒绝旧历史、高亮及signed数据保留均有断言。
- 上述证明的是两个具体数值/匹配缺陷，**不是剩余所有轮廓伪影的根因已全部确定**。固定上游提交`3841698348bfb246623d4acf791984c8b68a577b`的时域源码已核对，未另行移植新模块。

所有下列编号均来自`E:/项目/Veyra/logs/p2-nr-temporal-repair-20260927/commands.json`，不得与之前目录编号混用。

| 编号 | 实际证据 | 结论与边界 |
|---|---|---|
| 001、005–007 | 已捕获像素越零定位；旧shader红例、首次修复绿例 | 修复异常负通道，不等于视觉通过 |
| 009–010 | v1的75秒四层40帧及对照 | 负值归零但网纹残留，失败保留 |
| 011–014 | 感知guide反例先红后绿；旧保护/reset/signed/HDR测试仍过 | 第二次且本轮最后一次产品修复 |
| 016–020、027 | 75秒/125秒 × 单层/四层，各40帧；复用原关闭防闪对照 | base逐字节一致，首帧及四层中点reset精确，160帧均未制造负通道；数值平滑不作为画质分数 |
| 021–024 | 四层羽化、带PQ/BT.2020标记的合成HDR、SR→NR与NR→SR | 羽化严格GPU参照逐字节一致；层间同帧参照、共享光流、评估计数和debugErrors=0 |
| 025–026、029 | 静态/运动文字各40帧及原开关样本对照 | 输入/reset精确、像素有限非负；不是实拍自然素材或全面画质认证 |
| 028 | 真实QML bridge+engine 17步交互，35.396秒 | 通过，Main.qml还原哈希一致；不是鼠标/设计稿/动效验收 |

### 当前候选及未通过项

- 新候选：`E:/项目/Veyra/tests/p2-nr-temporal-repair-20260927/candidate-v2/`。QML EXE SHA256 `1a850fe4590129edb17879c454c4ca694331dbd9be5d5cf7b0bd9464ceee42c0`；`shaders/NrTemporal.dxil` SHA256 `f6fe501ca236eea3a7fdc5a086072750d1ebd528c0b6810f2d73bca7e8cc28fb`。**EXE与旧候选相同，实际改变在外置shader，不能只拿EXE哈希判断版本。** v1与旧final-candidate均未覆盖。
- 实际查看`spot-check-v2-75-4.png`、`remaining-gameplay-v2-125-1.png`及`remaining-gameplay-v2-125-4.png`：暗墙明显改善；亮粒子内部及渐变仍有细轮廓，多层更明显。视觉门槛仍失败，未出P2。
- 本轮已执行两次有证据修复，停止继续试调。不用降低NR强度、关闭防闪、改容差或重复整套矩阵制造通过。下一有效动作仅是利用已有125秒输入与历史定位具体残留轮廓，提出不同且可证伪的机制；在新的严格反例前不继续产品修改。授权不是阻塞，不再逐文件重复请示；不得将本段变成自动重复测试队列。
- GPU测试、构建及runner均串行。29条ledger记录无超时，最长35.396秒；005/012是预期红例。002的LF批处理产生假exit0，003实际跑到旧测试，**两项不计验收**；004规范CRLF后确认重新编译。008 staging只带应用，首次探针启动报WinError2、未执行；补复制指定已构建探针后009才执行。保留这些工具层失败，不藏为产品pass。
- 产物：`E:/项目/Veyra/{logs,tests,tmp,archives}/p2-nr-temporal-repair-20260927`；复用`build/p2-nr-20260927`，runner的TEMP/TMP仍仅对子进程指向E盘。源码增量与结果封存于本阶段`archives/.../verified-partial/`。未commit/tag/merge/push/Release，未动main、桌面checkout、1.4.4、设计稿或用户配置。
- RTX30/40及其他GPU、10/30分钟长稳、实屏HDR和17屏/42动效仍未验；core-host残留parameter block警告的历史边界不变。本轮不得推广为播放器底层全部无问题。

## 接线后结果与停点（防闪修复前的历史证据）

固定接线baseline v2：`E:/项目/Veyra/archives/p2-nr-wiring-20260927/scope-baseline-v2.json`，SHA256 `ad44036873581823f1eb77bef2465ca9051dca1dcf70199b6e9858cdf27c493b`。旧baseline、P1及P2部分成果不覆盖；所有以下编号来自 `E:/项目/Veyra/logs/p2-nr-wiring-20260927/commands.json`，与更早 `logs/p2-nr-20260927` 的编号分开。

| 范围 | 证据与实际结果 | 不代表什么 |
|---|---|---|
| settings/链/图接线 | 001–011：固定4层、count=0兼容旧单层，2/3/4层真实映射；不同运行库/非法混合顺序拒绝。逐层live参数/reset与history revision生效 | 不是P3任意节点执行器 |
| QML列表 | 013–016、035–037、070/072：添加/选层/参数/复制/删除/启停/拒绝回滚/seek/reset/原图命令17步，以及保护6步通过；修正nr类型id、重建reset、保护setter被fromChain覆盖 | Timer驱动真实bridge+engine；不是鼠标、17屏、42动效或设计1:1 |
| 全栈保护 | 018负例、019–025修复：最后保护必须对NR前基底及最终层统一作用，不可只保护最后层增量；无保护旧输出hash不变 | 未改变单层旧路径或着色器 |
| 保护羽化 | 061严格GPU参照首帧失败、debugErrors=0；仅把全栈保护第8常量固定为1，避免重裁signed输出。064八帧逐字节等于独立GPU参照，debugErrors=0 | CPU简化lerp不是bit-exact oracle；旧失败不抹除 |
| 生命周期 | 031：1/2/3/4层、1920/1280/960尺寸、九轮真实创建/3帧/reset/shutdown；最后三轮释放显存spread 4,767,744 bytes，小于事先64MiB门槛 | 四层live约3.23GB，释放约158MB；不能证明长期无泄漏 |
| 原图/关闭 | 065/066和`final-original-reference.json`：开增强与全关的sourceReference逐字节一致；全关输出=原图；开启NR+调色的处理输出确实不同，均8帧 | GPU原图资源语义，不是屏幕截图比对 |
| 调色/SR顺序 | 055–057、067：保护中心保留调色前/NR前约定基底；真实SR→NR及NR→SR，各4K工作图/1080NR，评估计数与层间精确对照通过 | 不重跑P1全套，不宣称低延迟必然更快 |
| HDR | 059拒绝丢失PQ标记的FFV1 fixture是正确防错；068新建Main10 HEVC并ffprobe确认PQ/BT.2020，069八帧保护对照通过，NR实际32次，debugErrors=0 | 合成数值fixture，不是自然HDR/屏幕HDR/所有显卡验收 |
| 实际素材防闪 | 038–053是网页文字/字幕，不是实拍。074–081补75秒游戏纹理/镜头移动、125秒粒子/强亮度变化；同源1/4层、开/关各40帧、首帧及四层中点reset一致 | 数值诊断不是画质分数；视觉检查发现伪影，故本项未通过 |

### 保留的失败与真实归因

- 026/027生命周期探针编译错误是测试使用错误的`source::FramePacket`命名空间；028改成实际`pipeline::FramePacket`后编译成功。029将累计`nrEvaluateCount`误当单轮数，改测试用每轮差值；030/031通过，未改产品计数。日志仍有core-host shutdown销毁残留parameter block警告，不隐瞒、不据此直接断言永久泄漏。
- 032真实QML保护反证exit23；035保护修复通过。061为严格像素负例，064为同条件绿色证据；都保留。旧`pixel-audit.json`的CPU羽化断言仍标失败，不放宽half-ULP阈值，改用相同已冻结shader、独立描述符/输出的严格GPU参照。
- 071最终QML失败：引擎08:32:36.917Z已Applied revision=16、NR=0，QML08:32:36.926Z仍读缓存`nrActive=true`。`applying()`查facade、`nrActive`查定时快照，测试跨时点比较。只修外部测试等待应用后的新snapshot；072保留17步全部原断言和150秒上限，33.925秒通过。失败不是借口删除断言，也未为此改产品。

### 防闪质量：明确未出关

`gameplay-quality.json`的同帧配对诊断（on / off的运动补偿稳定区域残差波动）：75秒单层0.000522 / 0.001407，四层0.000922 / 0.001794；125秒单层0.003077 / 0.008156，四层0.006810 / 0.012794。所有40帧齐全、finite，开关源基底与reset帧一致；单层原NR输出及光流也逐字节一致。说明开关确实做了时域处理，不是降低NR强度或吞帧。

但实际查看`gameplay-review-75-1.png`、`gameplay-review-75-4.png`、`gameplay-review-125-4.png`发现暗墙及亮粒子上出现新网纹/轮廓，四层更明显；不能将低残差波动直接标“防闪画质通过”，也不能宣称无拖影、四层一定更好。`NrTemporal.hlsl`和`NrTemporalPass.cpp`与固定开工基线及main `df41580`均无diff，因此不是本轮新改的时域实现；没有运行旧1.4.4二进制做该画质对照，不把源码相同冒称旧包实测。当前只定位到开启时域后的输出差异，根因未确证，不猜测模型路由或硬件原因。

当前shader仍在不可变guard白名单之外。此路径停止继续等价重测；保留像素/光流/失败候选。后续仅允许针对该问题做一次有根据的最小诊断/修复，若需改`shaders/NrTemporal.hlsl`先解决冻结范围，另建明确范围记录但绝不修改旧baseline洗白。默认关闭防闪的既有策略不变；不把问题移到P3/P6后假称P2完成。其他GPU、连续10/30分钟稳定、实屏HDR和完整UI设计/动效仍未验。SR/HDR/color通用QML setter可能有flat→chain覆盖风险，未修未验，不顺手扩到P4。

### 当前继续执行：用户已解除必要修复范围（2026-09-27）

用户已明确解除防闪shader及原定迁移必要依赖的冻结，不再重复询问计划内最小修复。下文“等待shader确认”是先前停点，不再是当前阻塞。旧v2 baseline与两个封存检查点不变；新阶段用`E:/项目/Veyra/archives/p2-nr-temporal-repair-20260927/scope-baseline.json`和记录的固定SHA执行，新增具体文件仅`shaders/NrTemporal.hlsl`，AGENTS更新后重新冻结。

本轮闭环：从现有捕获像素定位能解释网纹的机制 → 增加对应严格GPU负例 → 最小shader修复与同条件绿例 → 原两段游戏素材的有限1/4层开关对照及必要保护/HDR/真实QML回归。不得以调低强度、默认关闭、防闪失效、放宽oracle或仅数值波动下降冒充修复；不重跑全部P1/性能矩阵。P2其余原有强制验收仍保留。当前尚未修复，不提前写通过。新产物只在E盘`{logs,tests,tmp,archives}/p2-nr-temporal-repair-20260927`，复用P2增量build，绝不覆盖旧候选。

### 082：冻结范围内的单样本离线诊断（非修复）

- 上轮封存已实际完成：`review-checkpoint/checkpoint.json` SHA256 `4bb35df48cb4b1e1c6e72490f50b668a7ffba78271c85d4590078eaed9878fdf`，19个相对P1源码增量、1055份源码清单、415项保护身份检查；封存通过不等于P2通过。
- 自动续跑不是新的shader解冻授权。本轮只读冻结的`NrTemporal.hlsl`及host，查看已有75秒四层对照图；执行一次`temporal-offline-replay.py`，使用已有75秒单层的前12帧，不启动播放器、不重新解码、不生成新GPU样本。命令082耗时2.283秒、exit0、前后guard通过；既有基底/原始NR/光流的开关组逐字节身份一致。
- CPU按现有shader公式复算：首帧完全相等；帧1–11支持区域的平均绝对误差约`1.02e-5`至`1.56e-5`，同期防闪相对关闭输出的平均变化约`0.00102`至`0.00255`。结果支持优先检查现有时域公式，而不是无依据重写QML接线或NGX实例；但非首帧逐分量完全相等比例仅约46%–58%，单点最大误差最高0.00934，**不是严格像素oracle，更不是根因或画质通过证明**。CPU/GPU浮点执行、半精度存储及裁剪外历史的边界均在报告里披露，不能把差异一概认定为浮点误差。
- 报告：`E:/项目/Veyra/logs/p2-nr-wiring-20260927/temporal-offline-replay-75-stack1.json`；外部脚本在同任务tests目录。源码shader、host、候选与默认防闪设置未修改。已经完成这一轮有根据的离线诊断，不再以调整复算精度或重复相同样本耗尽额度。若要尝试shader修复，先取得最小冻结范围确认；P2仍未完成，P3不启动。
- 上轮检查点不覆盖、不重制。本轮仅两份文档及离线诊断证据另存`E:/项目/Veyra/archives/p2-nr-wiring-20260927/temporal-readonly-review/`，以其中实际`checkpoint.json`为核验依据。

### 当前候选、命令与恢复

- 062构建成功，063独立staging；当前真实QML：`E:/项目/Veyra/tests/p2-nr-wiring-20260927/final-candidate/veyra_qml_ui.exe`，SHA256 `1a850fe4590129edb17879c454c4ca694331dbd9be5d5cf7b0bd9464ceee42c0`。不是正式发行包或P2验收完成品。注入QML测试后Main.qml已恢复且hash核对。
- 运行模板：`python -B scripts/acceptance/p0-p1-r53-control.py run --baseline <上述v2路径> --sha256 <上述SHA> --seconds <不超过300> --case <唯一名> --revision <证据版本> --output E:/项目/Veyra/logs/p2-nr-wiring-20260927 --cwd <明确工作目录> -- <命令>`；构建另加`--kind build --seconds 900`。具体每项完整命令、日志、返回码、前后guard都在ledger及对应`*.result.json`。
- 全部产物在E盘`build/p2-nr-20260927`及`{tests,logs,tmp,archives}/p2-nr-wiring-20260927`。保留旧失败PQ文件、羽化CPU失败及原始像素，不为整理删除证据。未新建中间发行包或解压副本；必要诊断候选保留。
- 本轮增量恢复点：`E:/项目/Veyra/archives/p2-nr-wiring-20260927/review-checkpoint/`；保护及源文件封存结果见`logs/p2-nr-wiring-20260927/closeout-verification.json`。旧partial核验文件实际为`logs/p2-nr-20260927/closeout-verification.json`，不在旧checkpoint目录内。未commit/tag/merge/push/Release。

## 授权、起点与保护

- 最新授权：仅做总方案 P2；此前 1080p240 问题暂缓，不归因于电脑性能，不继续该专项。
- 源码工作区复用 `E:/项目/Veyra/worktrees/p0-p1-r53-20260927`，分支 `codex/p2-nr-20260927`；HEAD 保持 `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`。
- 开工核对 P1 最终快照全部 1054 份源码一致，63 项 dirty/untracked 全部保留。P2 起点记录：`E:/项目/Veyra/archives/p2-nr-20260927/entry.json`。
- P1 可恢复完整快照：`E:/项目/Veyra/archives/p0-p1-r53-20260927-131215/final-p0-p1-r53/`，checkpoint SHA256 `93ed34bee8545709c310582f2c723baf038023eede2a19c452b47db38bcafc80`。不覆盖快照、不伪称已提交/tag。
- 独立 P2 baseline 和其 SHA256 固化在外部 archives；复用不改动的 `scripts/acceptance/p0-p1-r53-control.py` 的通用 guard/限时执行逻辑。旧 P0/P1 baseline 仍保留并固定旧分支，不修改它来适配本次阶段转换。
- 原桌面 checkout、main、1.4.4 包、设计稿、已验 P1 可执行文件、运行库和用户配置禁止修改。NR 白名单外文件冻结；共享图若需要修改，先证实 NR 缺陷，不能改 P1 调色语义。

## 最小闭环与出关矩阵

1. 资源生命周期：借用/自有输入关闭、失败/重建，1/2/3/4 层资源、参数和历史隔离；一个 adapter/多 Feature，光流共享，不重造引擎。
2. 已有 NR 图：单层旧默认与 NR→SR 路径兼容；2/3/4 层参数、开关、复制/删除、reset、尺寸和显存；确认已有黑帧修复无回归。
3. 防闪：相同静态、运动、细节和切场景序列，开关和多层比较；实际 GPU 历史权重、upstream revision 与 reset；不能用降强度/吞帧制造改善。合成测试与真实 NGX 画质分开记。
4. 语义与回归：最后 NR 输出/保护区域、不启用 NR、总增强关闭、原图旁路、调色/HDR；真实断点 reset，不恢复 1.4.4 已修的普通预览跳帧/重复呈现/采集 drop 无条件 reset。
5. 仅对本次改动跑必要回归和真 QML 入口检查；复用已验证 P1 性能证据，仅当影响旧路径时补同条件测试。多层成本如实报告，不能承诺四层等速。

每项记录具体命令、可执行身份、退出码、输出与未测边界。RTX5070 不能证明 30/40 系；短测不能代替 10/30 分钟长稳；测试设备重建不能称整机 TDR；Win32/底层 probe 不能算新 QML 视觉验收。未满足强制项不关闭目标。

## 限时与停止合同

- 单测试 ≤300 秒，构建 ≤900 秒；同一原因一次诊断、最多两次有依据修复，禁止原样重试。20 分钟没有新证据停止该路径。
- 不并发 GPU 测试/构建/ledger runner；禁止子 Agent；不自动推进 P3–P8 或发布。
- 日志、测试、临时目录：`E:/项目/Veyra/{logs,tests,tmp}/p2-nr-20260927/`；guard runner 自身临时子目录沿用 E 盘 `tmp/p0-p1-r53-20260927/`，仅影响子进程，不改全局 TEMP/TMP。
- 新构建单独配置至 `E:/项目/Veyra/build/p2-nr-20260927`，不覆盖 P1 已验可执行文件。

## 执行记录

- 开工：P0/P1 guard pass，原 checkout 1457 文件、隔离 1054 文件受检。全量比较 P1 最终源码快照一致后转 P2 分支，无代码改动。进程检查未发现 veyra/veyra_qml_ui；PowerShell 无匹配导致整条命令返回 1，不是分支操作失败。
- 下一步：运行现有最小 NR 测试；借用输入 close 的 ComPtr 引用疑点须先复现，不能先称 GPU 泄漏。
- 基线：`scope-baseline.json` SHA256 `e66a8c95fcec590fced24ada038263288f9246b0a4eb8f3931597c827f40083b`；1055 文件受保护。AGENTS 和旧 guard 已冻结。
- 命令 001：构建脚本生成时转义错误，未进行编译；002 修正 UTF-8/CRLF 生成后初始构建通过（68.8 秒）。本项是执行工具错误，不是产品缺陷。
- 命令 003：`NrTemporalGpuTests` 实际复现借用输入 close 后不为空；四层 24 纹理隔离/重建及原 GPU 防闪测试通过，debug error 0。`NrInstance::close()` 改为无条件释放自己的 ComPtr 引用；005 同一回归通过。没有宣称永久泄漏或底层播放器整体损坏。
- 命令 006：新增真实 NGX 层间同帧 oracle，两层首帧比较 1,843,200 字节，1,611,565 字节不同，exit 1、debug error 0。根因：先执行所有 NR，再执行所有残差合成；第二层提前读了尚未写好的上游结果。修复仅把中间层残差/防闪移到下层 encode 前；单层最后合成位置不变、不加 CPU 等待、不改调色/采集/解码/播放调度。修后验证待执行。

## 严格复验补录（006 的初始 oracle 不作为最终反证）

- 007–011：两项测试自身缺陷已明确修正。其一，同尺寸下采样并不保证浮点采样逐字节恒等，因此期望值改用同一 `NrDownsample.dxil` 从已完成上层输出生成，仍要求 FP16 **零字节差异**，没有放宽阈值；其二，诊断读回曾错误假设资源固定处于 NON_PIXEL_SHADER_RESOURCE，现读取图内 tracker 的实际状态并在读回后恢复。没有借此修改播放器 barrier 或 shader。
- 012–017：2/3/4 层 × 防闪开/关共六组，每组 8 帧，frame 0/4 reset。每层输入中心 640×360、1,843,200 字节精确比较全部零差异；参数块/proxy/neural output 地址互异；NR evaluate 为 16/24/32 次，NVOF 各为 6 次、失败 0，D3D12 debug error 0。
- 018–035：P2 起点与修后候选的九个单层场景（passthrough、nr、nr-temporal、nr-style2、nr-residual、nr-protect、sr-nr、nr-sr、color-nr）各 8 帧逐帧 hash 完全一致。证据：`E:/项目/Veyra/logs/p2-nr-20260927/single-layer-equivalence.json`。这里的起点是 P1 已修版本，不是直接运行官方 1.4.4。
- 防闪开关在同一合成运动片上的输出确有变化，reset 首帧开关结果一致；帧差下降只说明该短样本的数值现象，**不等于自然素材的拖影/细节质量验收**。证据：同目录 `temporal-observation.json`。
- 036/037：将同一份最新测试 object 链接到封存的 P1 原顺序 pipeline 库。严格 oracle 在两层首帧即检出 **1,612,446 / 1,843,200 字节不同**，exit 1（预期反证失败）、debug error 0；证明修正后的测试确能抓住旧实现缺陷。没有覆盖旧库或 P1 可执行文件。证据：`037-old-order-counterexample.stdout.log`、`old-order-library-identity.json`。

## 接线追加授权（2026-09-27，当前执行状态）

用户已明确回复“允许，这种原本设定中的都允许”，解除下述 NR 列表多层接线的范围阻塞。本阶段沿用 P2 目标，不重开、不缩小，也不自动推进 P3。先核验旧 baseline、十份已验源码和候选身份，再建立独立 `archives/p2-nr-wiring-20260927/scope-baseline.json`；旧 baseline 和 `verified-partial` 永久保留。仅新增设置/链映射/图描述及直接测试的具体文件白名单，尽量复用 QML 桥，不改设计、不扩展处理链。

执行顺序：固定容量设置和列表映射 → 真实映射路径的单元/GPU 红绿验证 → 必要的真实 QML 回归及剩余 P2 证据。已通过的单层九场景和 P1 全套不无条件重跑；接线改变到的路径才补测。新产物使用 `E:/项目/Veyra/{logs,tests,tmp,archives}/p2-nr-wiring-20260927`，复用独立 P2 build 增量构建，绝不覆盖已验候选。P2 仍未完成。

## 当前接口缺口与剩余验收（追加授权前的历史记录）

- 只读核查发现 `EffectChain::fromChain()` 仅把第一层 NR 映射回 settings，`GraphDescription` 未把多层 NR 参数完整传给图；QML 桥使用该路径。底层 probe 多层通过不能算 QML 多层功能已完成。原先把接线统一留到 P3，会阻塞 P2 自身的**列表多层**验收：这是方案依赖缺口，不是测试失败可以豁免。engine/bridge 不在当前固定白名单，禁止静默越界。
- 已完成直接依赖的调色/P1 保全回归、当前真实 QML 候选构建与入口回归。尚待多层参数修改/upstream revision、启停/复制删除/尺寸及显存趋势、完整自然素材质量矩阵和组合语义。实例纹理重建通过不能替代产品图重建或界面复制删除。
- P2 未完成，不关闭目标、不自动推进 P3。下一步先取得**仅 NR 列表多层接线**的最小范围确认，再为新增白名单单独固化新检查点和合同；当前 baseline 不修改、不洗白。P3 的节点排序/编辑执行闭环仍单独保留，不提前实施。

## 本轮直接依赖回归与候选

| 证据编号 | 实际执行结果 | 边界 |
|---|---|---|
| 038 | 独立 P2 构建通过，7.988 秒；包含真实 QML 和直接依赖测试 | 不改 CMake，不覆盖 P1 构建 |
| 039 / 040 | NR 借用引用/纹理隔离/时域 GPU 测试、EffectChain 单元测试通过 | 单元模型通过不代表多层已传入产品图 |
| 041 | 调色 GPU 合同通过 | P1 的直接依赖回归，不重跑全部性能 |
| 042 | 同进程 Ampere→Original→Community→Ampere NR 切换、实际截图、关闭增强及 GPU drain 通过 | 只证明本机已批准运行库可执行，不证明 RTX30/40 硬件兼容或长期稳定 |
| 043 / 044 | 独立 QML staging 及三项 QML 测试通过 | 不使用旧 Win32 外壳冒充 QML |
| 045 | 八个 QML 入口全部通过；真实文件播放日志 realPresented=346、generatedPresented=0 | 效果关闭短播放；不是多层交互、FG、17屏设计或42动效验收 |
| 046 | 复用封存参考的 R5.3 严格全图比较通过 | 不覆盖参考，不重制基线 |

当前 QML：`E:/项目/Veyra/tests/p2-nr-20260927/qml-p2/veyra_qml_ui.exe`，SHA256 `a7f268ff3d56c5f3677b4f1ec11ef8a7f68a833512450d190160217a817f521c`。后端候选位于同级 `fixed/`；均不是正式发行包，不能把它们作为 2.0.0 完成品。

全部新执行命令及返回码在 `E:/项目/Veyra/logs/p2-nr-20260927/commands.json`；各命令前后 guard 均通过。源码相对 P2 起点而非 HEAD 记账，避免把已保留的 63 项既有修改算成本轮新增。可恢复增量保存在 `E:/项目/Veyra/archives/p2-nr-20260927/verified-partial/`；最终完整性以该目录 checkpoint 和 `closeout-verification.json` 为准，不冒称 Git commit/tag。
