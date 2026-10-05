# R0：最终候选与正常功能回归（2026-10-05）

范围为性能分支的当前可执行工作，不合并、推送或发布。依据最新用户指令，不做GPU竞争、显存压力、TDR或设备移除；保留已有压力记录但不重跑。测试使用本机RTX5070/616.56，不能外推AMD/Intel、其他RTX、真实采集卡、主机或物理屏幕延迟。

节点账本已经有证据与处置：5a检测/填黑拒绝；1a/1b/1c、2b、SR缓存扩展、4a、NVOF预取候选拒绝/明确回退；2a/2c单NR/2d/3a普通默认/限定3c/5b NVENC/5c保留待最终回归；4b是既有可选配置说明；3b可选默认不选，已完成真实UI/原生/完整导出对照，画质未获主观批准。下文前几轮产品身份与“待执行”语句保留为历史过程；当前观察及处置以文末记录为准。

本次收尾按以下顺序记录：

1. 当前产品与合同/Xbox/Qt Quick/easing/i18n/qml-data/效果链/preset/音频倍速/字幕/导出worker重新构建；每个测试最多300秒，scratch/TEMP全在E盘本轮目录。
2. 当前产品实际Qt启动各页、功能、布局、能力灰态、软件兼容模式，真实导出队列/lifecycle/取消/MP4/MKV。OBS游戏采集与实际RTSS只在可执行且不改变用户常驻配置的条件下验；未测不能称通过。
3. 普通播放收尾矩阵，沿用固定M1、S1/S2/S3/S4/S5配置及同驱动/运行库，三轮保存全数据；封存A和最终B的实际身份分别报告。Auto及先粗后细画质变化单独报告，不混入默认提速。原生全帧与导出一致性已覆盖默认off和Auto边界，不以性能数值代替画面检查。
4. 保留10秒附近NR CPU调用长尾、既有约2.8秒提交间隙等未解决现象，比较最终正常运行是否有新增回归；没有可靠根因不把局部降低GPU耗时称为软件丝滑或后台掉帧根治。
5. 翻译/源码边界/运行库原字节/工作树保护/最终源码存档、可运行候选路径及完成账本。

尚未运行的检查不能提前标通过。可运行测试失败先保留证据并修复；不可通过的软件候选要回退。需要用户真实来源或主观画质的缺口单独列明。

## 早期R0产品及已执行的功能回归（按阶段记录，最新身份见文末）

该轮产品源码是 `e8f0bd1082293c88a3ca1db851667162e4cef7a9`，当时后续提交只补文档/测试；该轮 `B2d/veyra_qml_ui.exe` SHA256 `8c2276d68d15c2e4466befcf367439b7efc42c68defd5ef93182398c9e8d8a03`。实际NR原件 SHA256 `f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc`，Lecram 310.8.3.0 / HashMismatch；不改字节、不冒充官方签名。Qt/FFmpeg/其他增强组件来自封存完整NVIDIA依赖。所有测试独立目录，末尾校验EXE/DLL/QML未改，未写入用户profile。

| 检查 | 实际结果与证据 |
|---|---|
| 当前构建 | `build-r0-contracts-v1`44步成功；增加Auto ID6后的效果链测试v2两步、Qt Quick夹具v3/v4重建成功。产品EXE没有因此重写引擎 |
| 定向合同15项 | `R0-contracts-v2`其中14项通过：repair、Xbox、1008个链组合/原生导出/图片边界、preset Auto往返、availability、UI合同、VFG设置、Auto控制器、三语i18n、qml-data、easing、音频倍速、字幕文本/合成。Qt Quick在`R0-contracts-v5`独立通过39项、0失败/0跳过。Xbox是软件合同测试，未新增主机实测 |
| 实际Qt功能/布局 | `R0-product-ui-v1-summary`GPU及`--obs-game-capture`软件绘制各功能/布局，共4/4通过。真实内嵌字幕逗号/三行、倍速1.3/1.8/0.25/4/1媒体时间、非法值/取消、全屏菜单遮挡、AMD能力灰态；720/1280/1600宽度专业模式布局、全屏菜单保持/锁定隐藏/退出恢复及最大化恢复。无ERROR/FATAL/ReferenceError/TypeError |
| 严格页面启动 | `nr-r0-smoke.py B2d R0-qml-smoke-v4`实际home/minimal/pro/node/export/settings/capture-dialog/playback共8项通过，0跳过；3500ms每项，无测试Loader。记录`logs/perf-nr-20261004/R0-qml-smoke-v4/run-20261004T214158154Z-77eec4cd/result.json` |
| 实际RTSS | `R0-rtss-ordinary-v1`本机安装原件，低/大OSD两套profile各普通播放/单NR参数/resize-pause-seek三项，共6通过。实际hook、共享内存OSD、原生目标窗口与视频/UI截图，背景alpha=255，无重启循环；cleanup逐字节恢复全部用户Profiles并退出本轮helper。没有额外GPU竞争/高倍率/4K压力 |
| 当前导出worker | `nr-export-worker.py B2d R0-export-worker-v1`真实lifecycle、取消边界、队列、HEVC MP4/MKV共5/5通过；队列四个完整输出文件SHA相同。工作线程EXE SHA256 `ced1ca191f975a0bb661c2889774aa9386bb911c3317e36afff9cb62a61d46fc`。原生Auto导出240全解码帧另在3b证据中一致 |

上述原始证据根目录都是`E:/项目/Veyra/logs/perf-nr-20261004/`，逐案例保存命令/实际EXE/运行日志/结果，不合并失败原记录。普通播放33组已完成；OBS实际游戏采集尚未运行，不能把软件绘制启动当作OBS已抓到视频。

## 回归夹具失败与处理

- `R0-contracts-v1`效果链硬编码864组合在增加第七策略后已过时，真实组合数1008；只修期望数量。VFG测试缺scratch参数、easing缺offscreen plugin，补齐隔离目录/Qt测试依赖；只停止挂住的本轮easing PID44288，未动其他进程。
- Qt Quick原断言只允许6档；保留旧六档编号及标签，追加Auto ID6/灰态检查。注册的inline NR卡使用引擎创建context，显式注入UI-only snapshot，真实bridge覆盖仍由实际Qt进程承担。补齐snapshot.logUi后菜单关闭正常；四卡点击前等500ms accordion动画完成，v5通过。初版失败v1–v4全保留。Quick测试仍有既有独立ExportPage/PlaybackRate夹具初始undefined、focus警告；未改产品去迁就夹具，实际产品回归无对应错误。
- `R0-qml-smoke-v1`子Windows PowerShell继承了桌面bundled PowerShell的PSModulePath而找不到Get-FileHash；wrapper显式导入该子进程自己的Utility/Management模块。v2引用的参数名被当位置参数、v3缺要求的qml-tests/QtTest/offscreen依赖；按现存readonly入口合同补齐独立stage，v4真正执行8项通过。未修改验收脚本、全局环境或降低必测项。

## 普通播放收尾：33/33通过

`nr-r0-normal.py B2d R0-normal-v1`串行执行同源M1、固定1280×800、实际GPU进程普通档，各S1/S2-720/S3/S4/S5-existing封存A/当前B各3轮；S4另测同B关闭3c队列分工各3轮，共33个普通50秒进程，全部通过且sourceSkipped=0。无竞争程序；Auto默认off。最后NR的滚动P95观察中位与全增强合并区间分别报告，不冒充全帧总体P95或物理显示延迟。

| 配置 | A最后NR / B最后NR ms | A增强 / B增强 ms | A / B成功软件Present P99 ms |
|---|---|---|---|
| S1 单层原生1080 | 6.129 / 6.159 | 7.002 / 7.086 | 34.044 / 33.999 |
| S2 单层720（尺寸66.7%） | 3.670 / 3.669 | 4.273 / 4.274 | 34.066 / 34.032 |
| S3 单NR1080上限+SR4K | 6.221 / 6.189 | 10.310 / 10.237 | 34.054 / 34.046 |
| S4 双NR1080上限+SR4K+DLSS2X | 6.431 / 5.989 | 19.481 / 18.795 | 17.236 / 17.233 |
| S5 既有480/720/1080三层 | 6.272 / 6.289 | 12.796 / 12.817 | 34.038 / 34.017 |

S4完整候选对基线增强区间下降3.52%；同一B的DIRECT增强19.299ms，队列分工对其下降2.61%。其余微小差异没有稳定收益，不能冒充NR模型提速。成功Present P99几乎没变；全矩阵最长110.209ms，图提交仍约109.793ms。33组未复现此前2.8秒断档不等于根治；所有运行稳态UI active=false，不能替代点击桌面前后完整焦点对照。封存日志`R0-normal-v1-summary/{artifacts.json,completed.json,comparison.json,per-run.csv}`及逐组原始证据。

## 退出重建：阻塞交付的真实崩溃

`nr-r0-close.py B2d R0-close-rebuild-v1`普通双NR+SR4K+DLSS2X播放，请求FSR3.1，200ms后Qt.quit，第一例访问违规退出3221225477（0xC0000005）。Qt确已退出，但后台继续NGX创建，向已销毁HWND创建交换链得到0x80070005，随后清理时崩溃。`nr-r0-close.py A R0-close-baseline-v1 fsr3`封存基线也同样崩溃，证明是既有退出边界缺陷，不能作为优化收益或新增回归隐瞒。两次player/console/result/minidump原件在同名日志目录保留。

源码根因：PresentSink::shutdown未清空非拥有queue_；VideoPresenter随后释放该队列。下次initialize在交换链成功后才赋新queue_，中途失败导致shutdown向悬空旧queue_调用Signal。EngineController初始化及失败回滚缺停止检查，退出仍尝试向失效窗口建图。先封存当前回归工具/数据说明，再最小修复PresentSink清理及初始化失败、EngineController长调用返回后的取消检查；不修改NR算法、运行库或稳态调度。候选此时不可交付；所有退出场景和真实OBS检查尚待修复后验证。

## OBS执行起点（历史记录）

OBS按官方32.1.2源码的portable配置及obs-websocket RPC1另写隔离驱动；只复制只读安装payload、临时认证服务，只选择本轮唯一标题的Veyra窗口，禁用桌面/麦克风输入和网络推流。仅待普通计时结束后执行正常录制、暂停恢复/缩放/全屏功能；未执行前无通过结论。

### 已执行与回归阻塞

实际OBS32.1.2启动：v1 API已连接但profile未ready，返回207；驱动按明确207有界重试，未降低画面断言。`R0-obs-capture-v3 compat`真实软件兼容模式六场景通过，实际NR、源码记录、六截图、MKV齐全；查看全屏及退回窗口原图确有M1视频。无网络推流、无个人音频输入，用户ini/json SHA未变，每轮临时服务停用、只退出本轮OBS/播放器。

GPU界面模式两次当前候选（v2/v4）均在fullscreen抓到Qt暗背景、没有视频，非通过；封存A两次（R0-obs-baseline-v1/v2）均通过且已查看真实全屏视频。关闭paused present复用仍失败；当前EXE配基线QML仍失败；封存退出修复前e8f0bd1也失败，排除本次退出修复。阶段存档B2d失败、核心复用B2a通过、暂停空闲B5c在resize失败。阶段对照指向5c呈现空闲改动，尚不能用阶段名字替代根因。

这一阶段曾计划恢复5c暂停呈现的旧语义检验相关性，不能以阶段名字作为根因。后续恢复和严格基线的实际结果见下节；所有原始player/OBS日志、截图、录像、artifacts/cleanup在同名E盘目录，OBS用户配置均原字节保持。

## OBS严格复核与无效候选撤回

旧判定裁图包含侧栏/底部统计，曾把窗口内无视频当作“6/6通过”；检查原PNG后更正。严格视频区域改为(220,180,660,430)。封存A原窗口playing/resize/windowed也会缺视频，不能沿用旧summary声称完整GPU窗口捕获支持。

暂停呈现恢复、关闭NR残差缓存都不能消除黑屏；该诊断恢复候选已拒绝，并恢复原已验证5c实现。随后250ms延后resize、一次DXGI_PRESENT_TEST、Qt帧锁、窗口extent代理和真正RHI buffer extent五候选分别存档；有的曾单轮通过，但严格重复仍失败，全部明确撤回。最终9403521恢复432f7ff生产文件，新增ObsQtFrameGate头/帧锁和GuiPrivate依赖删除，没有把不可靠的采集实验带入产品。

`nr-r0-obs.py A R0-obs-baseline-strict-r1 gpu --baseline-qml`使用原封存A EXE/QML、同M1/1280×800/普通单NR，第一轮fullscreen同样失败（视频std1.2199/1.2199/1.6957）；已查看原PNG，确是Qt背景而无视频，cleanup四项全部true。原A两轮全屏有视频也是真实记录；新失败证明既有GPU UI多交换链游戏采集**会不稳定**。不能再说“基线始终通过/优化导致黑屏”，也不能宣称问题已修复。OBS32.1.2源码的单进程全局交换链选择与此现象一致，但源码推断不等于全部根因已证明。

该限制列为未解决的既有采集问题；完整窗口游戏采集回归继续走软件现有`--obs-game-capture`兼容路径，GPU模式失败保持单独记录。后续最终构建、功能和录像结果分别记入下节，不用兼容模式通过冒充GPU模式通过。

## 最终生产身份与正常回归

最终生产改动提交`9403521cc4d0b24390a6c5891cef555c00de295f`，其后是测试/文档。`build-r0-final-v4`构建成功，UI SHA256 `06fc3703b0007df8467a7dd08e1e7acba25d21d539fb44ba42292d32e7b256e3`，显示版本`2.0.3-perf-B2d`；实际NR/驱动仍为上文固定Lecram/616.56。新增OBS gate/GuiPrivate依赖已移除，最终四个生产文件与432f7ff一致。不得沿用早期8c2276…的EXE作为最终身份。

| 当前构建检查 | 结果 / 原始记录 |
|---|---|
| 重建中退出 | `R0-close-final-v2-summary`7/7：FSR3.1/XeSS/VFG/SF-v2/尺寸/层数/停止，全部正常退出，无交换链错误/漏参数；含真实长创建返回后取消 |
| 合同/回归 | `R0-contracts-final-v6`16/16：repair、实际D3D12 PresentSink三轮failed-open/reopen重复关闭（debugErrors=0）、Xbox软件合同、1008链组合/原生导出/图片边界、preset/能力/UI/VFG/Auto/i18n/qml-data/easing/Quick/音频倍速/字幕文本合成；Quick39通过、0失败/跳过 |
| 实际产品界面 | `R0-product-ui-final-v2-summary`GPU与OBS软件各功能/布局，4/4；字幕逗号/三行、自定义及非法倍速、720/1280/1600布局、全屏菜单/锁定/最大化恢复、能力灰态，无产品QML错误 |
| 实际导出worker | `R0-export-worker-final-v2-summary`5/5 lifecycle/保存取消边界/顺序队列/HEVC MP4/MKV；队列四个完整文件SHA同。worker SHA256 `7486047c854937778ed27729b6951453127cd8c22e1e96c78493b8e359721871` |
| 原生/Auto-off | `R0-native-final-v2-summary`10/10、470完整原生RGBA/PTS/history。Auto池与独立同尺寸参考一致，默认off五配置200图与B4b基线CSV完全相同，debug0；两120帧Auto/固定原生NVENC封装文件SHA同，无池创建 |
| 全文件解码与画质 | `R0-native-final-v2-quality/review.json`完整240解码MD5/PTS/时长/音轨/尺寸差异0，12配对图PSNR/SSIM与早期3b一致，100%切回逐像素相同；低档改画面、主观未批准 |
| 无测试Loader的页面启动 | `R0-qml-smoke-final-v5/run-20261005T003229757Z-6da97867/result.json`8/8、0跳过，实际home/minimal/pro/node/export/settings/capture-dialog/playback各3500ms正常退出 |
| 实际OBS兼容采集/录制 | `R0-obs-compat-final-v3`正常播放/暂停/恢复/resize/全屏/退窗六场景有真实视频、录制中resize后50ms退出0；用户OBS配置SHA不变、临时服务停用、仅本轮OBS/播放器退出。`record-review-scenes-v2/review.json`783帧全解码与ffprobe计数同，六对应录像取帧有视频、全屏连续图实际变化；已查看全屏/退窗原录像图。MKV封存SHA `f2b5f3cf1bc537995737b317c13eea0e6671940137fcaf89a2bb3f3f698dd51a` |

这些是软件和本机正常负载回归，未扩大到Xbox长稳、AMD推理、实卡、其他RTX/驱动或物理显示。RTSS早期R0的6正常场景证据已记录真实原件/profile恢复；退出取消和撤回的OBS实验未改该功能，未冒充它由新EXE重跑。最后正常33组属于e8f0bd1那轮稳态实现，新生产差异为退出边界修复；下面新6组验证最终同样配置的真实焦点状态，不能混合两批不同时间的滚动读数宣传额外NR收益。

## 真实前台/后台/再前台对照

`nr-r0-focus.py B2d R0-focus-normal-v4 --resume`最终6/6：M1、S4、50秒、1280×800，A/B、B/A、A/B三轮交错；一个本轮拥有的空GDI小窗用于失焦，无额外GPU工作。每轮18次总激活均保存Win32 API返回/前台PID，与真实Qt active一致。只连接/解除本轮测试的输入队列，不改系统焦点锁、不发键鼠输入/关闭用户窗口，cleanup本轮helper结束、原焦点恢复true、用户设置不变。

| 三轮中位 | A软件提交FPS | B软件提交FPS | A软件Present P99 ms | B软件Present P99 ms |
|---|---:|---:|---:|---:|
| 前台13–23秒 | 60 | 60 | 17.2609 | 17.1828 |
| 后台28–38秒 | 60 | 60 | 17.2227 | 17.2788 |
| 再前台43–48秒 | 60 | 60 | 17.2402 | 17.1764 |

各段每轮600/600/300个实际提交区间、10/10/5个真实Qt样本，sourceSkipped全0，当前六组没有严重失焦掉帧；B后台最长18.0828ms。此结论只限普通空窗口失焦，本机没有物理显示事件，不证明用户涉及其他窗口负载的现象已根治；33组约110ms CPU提交长尾仍保留。

夹具v1/v2激活失败、v3错用不存在的[pacing-submit]格式、v4初次错把无返回值的matrix.run当receipt，全部失败数据保留。两输入队列实际连接后v3激活已成功；格式改为既有[submit]。v4第一A完整播放/事件/焦点均有效，只汇总失败；resume严格核对passed/S4/50秒/EXE和源SHA/真实前台事件，复用这组不可变数据后重做分析并继续其余5组，不以重试直到好看替换性能样本。前台流程依据微软[SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)，始终按实际前台PID判定。

录像夹具也保留两次失败：v2在StopRecord回复就记MKV hash，muxer尚未结束，完整后处理发现变化；修正为OBS进程停止后封存，独立保留StopReply hash。v3完整解码后固定2秒帧仍为空，因为捕获尚未完成初始化；六完成截图实际在4.229/8.492/12.752/16.984/21.307/25.519秒。CPU reviewer按该轮OBS Writing file日志与原截图mtime对应取帧（近似阶段对齐，非物理时间测量），不删初始帧、不替换录像；全783帧仍纳入完整解码。新review全屏内三取帧RGB变化均值约5.85–7.51，确认真实录像在推进。旧黑帧、失败目录和receipt全保留，不冒充原review通过。

## 存档、交付与未验证边界

所有节点before/candidate/accepted/rejected源码标签、增量bundle/patch/receipt在`E:/项目/Veyra/archives/perf-nr-20261004/`，失败证据不覆盖。3b可选、UI、退出修复已有最终accepted标签；有画面反例或整次负收益的生产候选均明确撤回。自查了核心借用/最近实例/预热的单设备与关闭顺序、尺寸/运行库key、队列迁移失效及PresentSink失败打开清理；不是独立Reviewer验收。

完整本地NVIDIA包由`nr-final-package.py`从同一最终EXE/受控QML/不可变组件生成：普通ZIP、逐文件manifest、独立源码ZIP、干净解压SHA及退出回归身份；`nr-final-audit.py`另验PE imports/delay-imports、FSR分包例外、VFG闭包、驱动/SDK排除、源码隔离、Windows-only PATH下GPU/软件启动及全载荷保持。真正执行结果和源码快照commit写入E盘独立交付回执，未产生前不提前标通过。

方案全部本机优化节点已有实验及处置，不代表草案每个数值目标达成：8K整次3.91%未达10%，1a等正确性反例直接拒绝，Auto及先粗后细主观画质未批、额外显存等代价不隐藏。外部C Magpie参考计时、真实M3/M4/M5/M6/主机30/40/60、AMD/Intel/其他RTX/616.92、TDR/真实设备恢复、物理显示/端到端延迟未验。压力按用户最新要求不再执行。严重后台掉帧及偶发约2.8s/110ms长尾、GPU UI OBS游戏采集不可靠仍是未解决项；本轮保留局部优化不能宣传这些问题全部修复。
