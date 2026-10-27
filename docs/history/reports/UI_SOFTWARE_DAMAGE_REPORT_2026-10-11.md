# 软件 UI 残影与超分图标裁剪修复记录

最终生产改动只有 main.cpp、VIcon.qml、ProPage.qml。OBS / RTSS 软件 UI 中，相同图标路径先光栅化成缓存 SVG Image，普通 Image 节点处理父级裁剪；GPU UI 保留原 Shape。虚线 NR 添加边框同样处理。旧无效环境变量及误导日志删除，CMake、引擎、NR、补帧、GPU 优先级、运行库不改；VTelemetryValue 的 33ms 策略字节不变。

## 原因及撤回

Claude 仅删除旧变量并加入两个私有头，未完成替代实现。Qt6Quick.dll 实际没有读取 QSG_SOFTWARE_RENDERER_FORCE_PARTIAL_UPDATES，因此以前的“局部更新已关闭”日志不是事实。

[Qt 6.8.3 软件 Shape](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quickshapes/qquickshapesoftwarerenderer.cpp) 在空 clip 时跳过 setClipRegion 仍 drawPath；[软件 RenderNode](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/adaptations/software/qsgsoftwarerenderablenode.cpp) 对空 damage 的处理与普通 Image 不同。实际完整重绘仍把超分星形和箭头画到固定头部，证明全窗重绘不能解决裁剪。

已撤回完整/条件重绘及软件 Shape layer 原型。完整重绘原型虽然 NR 没变慢，一次配对 CPU 0.197661 → 0.325101 CPU 秒/秒，增加约64.5%，仍有图标越界；layer 还出现位置异常。原型 performance.json、withdrawn-adaptive-repaint.patch 留作失败记录，不能当最终证据。最终没有私有 QtQuick 依赖、绘制钩子、新 Timer/动画、update/requestUpdate 或每帧整窗刷屏。SVG 插件是原包已有依赖。

## 实际滚动和裁剪

scripts/acceptance/ui-software-damage-test.py pixels-expanded：真实 Main/ProPage、1280×800逻辑尺寸、150% DPI，3个NR层/首层展开，80次1.25px滚动至100px。独立私有EXE只加测试驱动；QScreen读已呈现窗口，截图没有触发渲染。滚动后与同位置完整重绘参考比较，另用固定头部与初始图像比较，避免将“重绘参考本身也越界”误当成功。

| 软件路径 | 滚动区域及边缘残留像素（最大通道差>10） | 完整重绘后固定头部改变像素 |
| --- | ---: | ---: |
| 原 OBS QML | 1237 | 209 |
| 修复 OBS QML | 0 | 0 |
| 修复 RTSS 选择路径 | 0 | 0 |

37个可见图标全部 Image.Ready；人工核对超分/NR/HDR图标、箭头及工具栏位置，未见异常。RTSS整窗差62像素来自独立GPU状态区域，相关裁剪区域为0。空闲3秒只有3/6次自然状态更新，未形成持续高频绘制。RTSS测试验证兼容选择路径，没有重新注入RTSS DLL、没有重做OBS实际抓流。记录：reports同名/pixels-expanded.json、clipping.json；PNG与每个probe.json在logs同名/pixels-*-raster2。

## GTA 1080p NR 性能保护

顺序执行 performance-final，用户原始 GTA 4K30 视频，原版NVIDIA NR、单层内部1080p、强度1/风格0，画面调控/SR/FG关闭，普通GPU调度实际读取class2。RTX5070/616.56、本机320Hz显示环境。每例40秒有效播放、墙钟53–56秒；每例29个前台有效一秒样本，NR为一秒均值的中位数。CPU是进程CPU秒/墙钟秒（一个CPU核的时间口径），不是整机百分比或纯UI耗时。

| 路径 | NR GPU ms | NR变化 | UI提交次数/秒 | 进程CPU秒/秒 |
| --- | --- | --- | --- | --- |
| 普通 GPU UI | 5.919221 → 5.898998 | -0.34% | 35 → 35 | 0.265282 → 0.270664 (+2.03%) |
| OBS 软件 UI | 5.863204 → 5.835594 | -0.47% | 38 → 37.5 | 0.258953 → 0.223809 (-13.57%) |

两种路径视频提交FPS均30，不是物理显示刷新率。普通GPU用原包EXE+QML对照当前EXE+QML；软件路径使用同一当前EXE，仅切换原/新VIcon与ProPage，隔离本次QML修复。UI没有恢复到320Hz；本次短测未看到NR回退、软件CPU没有增长。小幅NR差异不能当算法收益；软件CPU的本次差值也不能推广为所有机器都降低同样比例。没有以提高优先级或减少NR画质掩盖问题。

原始证据 reports同名/performance-final.json；logs同名/perf-final-* 的命令、日志、29个样本和GPU调度读取完整保存。原版NR SHA e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e；统计QML SHA e88541114240fad417f7d1f08bde0b8236964ff7e9005c8fc8214743c006958b。

## 构建、保护和限制

ui-software-damage-build.py：实际CMake配置通过；仅重编本次变更main.cpp，重链接已验不可变引擎/依赖对象，不冒称全工程clean重编。构建9秒，独立probe构建8秒，旧缓存/库输入SHA不变。正式EXE SHA bed99662e2be2dbde65fd41fd946cb5c4a627b42f50d305e0a47127e4356273a。生产EXE不含测试驱动。

初次SVG回归发现sourceSize.height引用同组sourceSize.width造成绑定循环，已改为两个独立DPR表达式；失败logs/pixels-fixed-obs-raster保留，最终raster2没有绑定循环。更早expanded像素测试因时间点不一致导致参考位置不同，保留pixels-expanded-invalid-timing.json，不计通过；最终改为滚动完成回调后截图并核对contentY/contentHeight一致。

1626文件原包每次前后SHA核对通过；测试使用私有app/data-dir，测试Main注入已原子恢复。旧53工作树/496未提交文件与main保护守卫另行收尾核对。没有改DLL/SDK/模型/驱动或真实用户配置。所有产物E盘，只有一份以硬链接复用不可变原包的私有运行目录；没有新用户包、main合并、push或Release。对应验证源SHA、构建回执/不可变输入和证据SHA在reports同名/validation.json，最终commit/bundle/守卫见final.json。

本机短测和生产页面复现修复已闭环；反馈者完整overlay环境、不同DPI/主题/驱动、长时间运行及全部播放入口仍需现场回归，不声称这些都已经验收。NV显存问题不属于本次修复。


本轮临时缓存清理被自动审批拒绝：目标E:/项目/Veyra/tmp/ui-smear-handoff-20261011的PowerShell递归删除返回blocked by policy，命令未执行。560文件/80127256逻辑字节保留，未绕过重试，不计为已释放磁盘；cleanup.json如实记录。修复、构建和验收结果不受影响。
