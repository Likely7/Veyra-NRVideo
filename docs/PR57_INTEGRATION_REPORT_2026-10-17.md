# PR57 Windows接入与验收报告（2026-10-17）

用户要求合入云端Agent的PR57并完成本机测试。本机验收已完成，并已正常推送main；GitHub确认PR57已合并，回执见本报告末尾。原云端提交1295d6bc6c9077738e230da9dc1d4f551be3b29e、09ac54bd31ceb28f613f38beab6bbb05b486efdb通过no-ff合并9c7b342保留。整合基线d68d6be9c37127350698a1d4d47fc01b36b29475，隔离分支codex/pr57-capture-window-20261017，最终实测产品代码1f283dc9ccbd9461124372b5dfb53c4737184aac。未重新制作或修改已发布2.0.9包/标签，没有新增Release。

## 接入内容与本机补齐

- 窗口游戏模式在会话、全屏、热键监听就绪后接管输入，不再等待首页动画或增强引擎首帧；稳定消息窗口接收退出/面板热键，原生窗口句柄更换后重新绑定，退出先恢复输入。
- 原始对齐模式保持精确鼠标穿透；填充、拉伸、固定比例、原始像素等缩放显示保持源窗口前台，挡住坐标会错误的鼠标穿透。源尺寸变化同步SourceInfo、Engine快照及DAR，维持已有300ms缩放防抖、250ms几何心跳、33ms/250ms界面更新节奏与看门狗。
- DirectShow明确选择采集视频pin，GetStreamCaps明确不支持/空表/无可解析格式时回退EnumMediaTypes，保留真实驱动失败；展开驱动区间与步长，提供自定义尺寸检查。范围组合明确标为候选，连接时验证，不能把枚举候选称为实卡全部支持。
- 精确媒体身份包含子类型、尺寸、帧间隔、格式与扫描方式；验证SetFormat/GetFormat/连接后格式，不自动改成相邻模式。显示层只合并标准帧率附近±1个100ns tick，59.94/60及逐行/隔行仍区分，精确key不改。
- 三个菜单缓存、查询排队及旧设备回执隔离；新增可搜索、可复用delegate的旧版完整列表，两种入口共用精确key。
- **额外实测修复**：连续点击缩放模式时，播放器的全局双击Handler会误退出全屏。游戏输入生效时同时禁用播放器单击/双击Handler，实际快速双击回归通过。
- **额外兼容修复**：旧版列表保留MJPEG红色及“不建议使用”；搜索框、列表项及滚动条显式采用Qt Basic控件，消除Windows native style不允许自定义background的运行错误，没有修改全局主题。
- **额外静态源修复**：静止窗口缩放后，Recreate丢弃现有帧，系统可能不再送新帧。保留原300ms防抖；仅在重建后另300ms仍无帧时恢复一次WGC会话和pool，沿用指针/无边框设置。首次只重建session而复用关闭后的pool在本机报0x8000FFFF，已修为新pool与session一起创建。动态来源不额外恢复；不重绘用户目标，不造帧/造PTS，不增加逐帧等待或UI轮询。独立C++静态源及完整QML静态/动态源均取得真实新尺寸帧、DAR与reset标记。

## 真实验收与边界

| 验证 | 结果与证据 |
| --- | --- |
| MSVC原生构建 | 应用等24个相关目标通过。首次独立全编译131.1s；最终代码增量21.0s，回执freshCMakeBuild=false，不把增量叫干净全编译。 |
| 相关16程序 | 全部通过；游戏策略44项、能力区间25项、fake COM发现24项、精确格式、采集颜色合同、384布局、效果链、事务、几何、数据、i18n、NR设置、HDR设置、实时呈现及3轮sink生命周期。 |
| Qt Quick | 82通过，0失败/跳过；含真实CaptureLegacyPicker搜索、禁用项、精确选择、MJPEG红色及提示。 |
| 多语言 | extract.py --check：2448条、繁体/英文/日文缺失0、占位错误0；采集页四语言实际截图及控件边界通过，人工查看英文截图。 |
| 原生输入 | 实际低层键盘钩子、键重复/修饰键/所有权、GUI忙1.5s、面板、全屏/Qt flags、句柄替换、热键退出及原生样式恢复通过。 |
| 完整应用窗口 | 正常D3D12/列表、OBS软件UI/节点及可控125%缩放通过；实际独立进程Raw Input目标、鼠标落点100/100、面板、改键、源移动/resize及重入通过。另五种aspect、缩放快速双击、竖屏同步、反复退出/重入在动态与静态两种目标共四场景通过。125%是测试环境缩放，不代表真实混合DPI多屏全部验收。 |
| 本机采集卡 | MCS4K-T911。1136条实际驱动模板/范围候选在“显示全部可用帧率”打开后可由三级菜单到达；完整列表搜索及共享选择通过。NV12/YUY2的1080p60各两轮6秒采样，中间实际重连；连接59.999880fps，回调分别60.153700/60.132939与60.072959/60.062973fps，PTS单调。 |
| 保存及恢复 | 10个实际应用场景：编辑/旧列表、四语言布局、重启恢复、排队刷新、实卡连接、关闭自动恢复后的手动继续、自动恢复、缺失格式/音频/设备拒绝且不覆盖原选择。实际采集1080p30、音频关闭，翻转/SDR/色彩/缓存/入口设置均保存恢复。 |
| WGC真实图/时序 | 池上限、最小化/恢复、横竖/静态resize、DAR/reset、实际NR和DLSS4x各>100次执行、engine resize快照与停机排空均通过。**整套屏幕测试并未全绿，见下段。** |

**保留的既有失败**：当前HDR桌面的RGB绝对阈值断言失败。用原2.0.9原生测试EXE同环境对照，旧5个颜色样例失败，当前增加竖屏样例后6个失败；两边所有对应像素完全一致：红(255,110,115)、绿(96,255,122)、蓝(78,97,255)。没有降低阈值、改颜色链路或隐藏失败。最终native验收回执明确fullScreenSuitePassed=false、knownBaselineOnly=true、prChecksPassed=true。本次窗口/输入/精确格式改动及新增静态恢复通过，不宣称既有HDR颜色问题已修复。

没有反馈者BMD实卡，本机T911成功不能扩成“BMD黑屏已解决”、所有驱动/所有候选组合均可连接。没有端到端光学延迟或新性能提升百分比测量；输入接管先于首帧仅证明控制时序。补帧模型、运行库、采集mailbox、增强图处理顺序和UI绘制频率没有调整。旧delivery.ps1明确只接受legacy Win32，不套用到此次QML验收；执行上述相关原生/Qt/完整应用测试。

## 命令、记录和保护

产物根为E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/pr57-capture-window-20261017。QA程序在tests/.../app，原用户配置不参与；注入的测试QML每次finally恢复并逐文件核对。全部私有测试进程结束，没有自动拉起交付播放器。

主要命令：py -3.11 -X utf8 -B -I <tmp>/build.py；restage.py；native-final.py；ui-window.py [--static]；ui-legacy.py；ui-capture.py edit/layout/restore/queued-refresh/connect/resume/auto/missing-format/missing-audio/missing-device；card.py；checks.py；verify.py。实卡程序：veyra_capture_tests --rate-test capture:0:9:-1 60及capture:0:8:-1 60；WGC：veyra_screen_capture_tests --engine。源码检查：scripts/i18n/extract.py --check、git diff --check。每个测试进程受300秒内期限保护，没有并发GPU竞争的结果用于最终通过证据。

最终证据（完整命令、exit、日志及SHA见各receipt）：
- build-20261017-215831/receipt.json；checks-20261017-220627/receipt.json及quick.txt。
- native-20261017-215901/final-receipt.json；screen-baseline-20261017-214242/receipt.json。早期颜色失败、静态失败及0x8000FFFF尝试均保留。
- window-20261017-215938、window-static-20261017-220005、ui-220541；四语言ui-layout-20261017-220243。
- reports/.../{capture-ui-receipt,card,runtime-protection,verification}.json汇总其余最终路径。

保留早期夹具错误：两个旧测试第一次缺少CLI参数；首页提前接管输入后的原生viewport需等待异步落位；测试调用stop而非stopPlayback；未打开全部帧率就断言完整遍历；隔离profile尚未建立第一次连接就调用继续。这些均修正并回归，原失败不删。一次退出时Qt报告位置差1px：仅允许同尺寸、位置各差≤1px的明确平台舍入警告，实际视频矩形仍严格相等；最终四场景无该警告或其他QML错误。前期存在有重叠的未通过探索运行，不用于性能/验收结论，最终各GPU测试串行。

存档checkpoint/pre-pr57-capture-window-20261017；source-before.bundle verify通过，SHA78826150658331c101903a3125aa1c2a8a59a60719ea293bd0467e4585b50d88。84个原工作区、96原分支及509未提交文件逐项保持；只允许授权main推进。1524个原运行组件与QA副本逐文件相同，SF310.9.16和原版NR身份不变，无SDK/DLL/模型加入Git。原分支和已发布2.0.9不删不改。最终source-after.bundle与远端合并状态在archives/reports下记录。

## 远端合并回执

2026-10-17 22:09:26（UTC+8），GitHub确认PR57状态MERGED、非Draft。云端head仍为09ac54bd31ceb28f613f38beab6bbb05b486efdb，mergeCommit为9c7b342a9f53697e2ec3663c71e1202e12477550；main经快进、正常非force推送至7e2157d3a427e361fb8a71eb4b6dff1c3f09edbe，包含云端提交、本机适配和验收报告。云端源分支保留。其后的本回执提交只更新报告/WORKLOG，不改变已验收产品源码或现有Release。完整命令回执E:/项目/Veyra/logs/pr57-capture-window-20261017/main-integration/commands.json，最终远端SHA与备份校验见reports/pr57-capture-window-20261017/final.json。
