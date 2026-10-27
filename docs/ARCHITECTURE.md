# 界面代码与职责边界

本轮是保持行为的结构整理，没有替换处理算法、配置格式或公有QML接口。引擎、管线、采集/解码、音频、运行库和着色器未改。

## C++：稳定的QML入口，按领域分开的实现

`include/veyra/ui/QmlPlayerBridge.h`继续定义原有属性、方法与信号。`src/ui/QmlPlayerBridge.cpp`只保留启动、生命周期与轮询接线（约640行）；`src/ui/QmlPlayerBridge_p.h`集中保存原有私有状态、内部共享声明与无状态文本转换，不是新公有API。

| 实现文件（src/ui/bridge） | 职责 |
|---|---|
| Hardware.cpp | 适配器、能力检测与缓存、可用后端选择 |
| Capture.cpp | 采集格式/音频设置与异步查询、屏幕采集与游戏模式接线 |
| RemotePlay.cpp | PS5/PC/Xbox串流与手柄控制入口 |
| Playback.cpp | 文件/网络播放、源打开和播放命令 |
| Enhancement.cpp | NR/SR/AA/美颜等增强参数映射与校验 |
| FrameGeneration.cpp | 补帧后端、倍率、光流、显示同步/VRR选择 |
| Colour.cpp | 调色参数表、曲线、LUT与色彩look |
| Subtitles.cpp | 字幕加载/样式与音频同步；GDI+初始化仍只有一个实例 |
| Recording.cpp | 实时录制的界面控制与状态发布 |
| Export.cpp | 图片/视频导出设置、任务与状态 |
| Chain.cpp | 处理链/节点编辑与预设操作 |
| Preferences.cpp | 偏好、快捷键、组件信息及扩展的历史/导出行界面入口 |
| Status.cpp | GPU/阶段耗时、负载与运行状态 |
| View.cpp | 保护区域、对照与画面布局选择 |

每个文件是独立编译单元，通过显式CMake清单参与构建。带状态的硬件缓存、GDI+初始化和生命周期所有权没有复制到头文件；共享状态的布局和方法体保持。后续改功能应先找到对应领域文件，再决定是否需要进一步拆私有状态/服务，不要又把实现堆回启动文件。

## QML：对话框独立，页面复用小组件

- `DialogHost.qml`（约134行）只做路由、遮罩和统一宿主；`DialogLayer.qml`保持原有尺寸、动画和原生视频遮挡合同。
- 网络/采集/PS5/PC/Xbox/屏幕入口各在具名SourceDialog中；字幕、音频、保存及管理预设各自一个组件。通过明确的`dialogHost`以及采集页`legacyPicker`依赖共享宿主，不改对话框动作或控件名字。
- `DialogSection/Group/Note/TextField`是统一的外观小组件，`DialogHost.VTextField`兼容名保留。
- `ProPage.qml`从3213行降到约2144行。音频、显示面板分别为`ProAudioPanel`、`ProDisplayPanel`，HDR覆盖观测仍受页面/页签/卡片可见性约束。
- 状态条、性能球、呈现选择及加层按钮各有Pro前缀的复用组件；旧`ProPage.PerfOrb/PerfOrbs/StatusSummary/PresentationRows`等入口保留兼容包装，节点页继续复用。

## 翻译与验证

翻译提取器覆盖`src/ui`子目录及桥接私有状态头；2681条翻译及其三语言内容保持一致。结构迁移验证逐方法比较搬迁前后的681个桥接方法体，QML文案和objectName多重集合保持。

结构一致不能替代运行验证。本轮另外进行产品构建、16组合同/单元检查、117项QtQuick、四次真实渲染器/偏好启动，以及软件/GPU模式的对话框、专业页签、预设恢复和播放检查。实际结果与失败处理见[执行记录](PROJECT_MAINTENANCE_EXECUTION_2026-10-27.md)。
