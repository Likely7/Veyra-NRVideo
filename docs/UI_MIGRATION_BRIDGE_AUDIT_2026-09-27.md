# QML Bridge 只读审计（2026-09-27）

## 审计身份

- 分支：`codex/ui-qml-migration-20260925`
- HEAD：`babebbb`
- 范围：`include/veyra/ui/QmlPlayerBridge.h`、`src/ui/QmlPlayerBridge.cpp`、`apps/veyra-qml/main.cpp`、`qml/Veyra/**`
- 方法：只读检索 Q_PROPERTY、Q_INVOKABLE、信号和 QML 消费点；没有修改 engine、pipeline、source、sink、media、shader 或 CMake。

这份审计只回答“新界面现在能调用什么、缺什么”。它不把 bridge 当前已经存在的后端能力重新定义成 UI 迁移工作，也不把缺口变成后端施工任务。

## 已有真实接口

| UI 表面 | 当前 bridge / QML 入口 | 证据与边界 |
|---|---|---|
| 播放与片源 | `openPath`、`openUrl`、播放/暂停、停止、`seekTo`、`seekBy`、逐帧、截图、拖放、最近文件 | `QmlPlayerBridge.h:473-507`、`Main.qml:216-223`；命令通过 facade/engine 异步执行，UI 不直接拿帧或 GPU 资源 |
| 片源状态 | `statusText`、位置/时长、尺寸、源 FPS、旋转、宽高比、缩略图 generation | `QmlPlayerBridge.h:121-145,331-352`；未知值由 bridge 保持未知，不在 QML 猜测 |
| 原生视频窗口 | QML entry 创建 native video window，bridge 接收 HWND | `QmlPlayerBridge.h:461-471`、`apps/veyra-qml/main.cpp`；视频不改成 QML scene graph |
| NR/SR/FG 与低延迟 | 设置属性、FG 后端/倍数/严格模式/低队列、效果启用状态和状态读数 | `QmlPlayerBridge.h:62-67,164-187`；只消费现有 `EnhancementSettings`/facade，不能借 UI 迁移改处理顺序 |
| 颜色页面 | 颜色开关、参数目录、分组、按名称读写/重置 | `QmlPlayerBridge.h:249-257,457-459`；页面只展示 engine 已暴露的参数 |
| 效果链/节点 | chain、catalog、校验结果、节点模式；增删、移动、启用、位置编辑 | `QmlPlayerBridge.h:189-197,409-414,512-521`；validator 拒绝时显示 notice，不绕过约束 |
| 预设 | 应用、保存、复制、改名、删除、设默认、导出预设选择 | `QmlPlayerBridge.h:200-212,241-247,523-531`；预设库是已有分支能力，UI 只调用，不继续扩展数据模型 |
| 采集卡 | 设备枚举、设备选择、Force SDR、垂直翻转、开始入口 | `QmlPlayerBridge.h:94-107`、`DialogHost.qml:167-219`；格式/色彩/缓冲等未暴露的字段不伪造 |
| 屏幕捕获 | 目标枚举、选择、刷新、开始入口 | `QmlPlayerBridge.h:103-107`、`DialogHost.qml:272-311`；当前使用已有整块目标捕获能力 |
| PS5 串流入口 | host、PIN、状态和打开对话框 | `QmlPlayerBridge.h:114-116,304,320-323`；对话框只消费现有入口 |
| 音频 | 音轨枚举/选择、音量、静音、音频偏移 | `QmlPlayerBridge.h:181-184,205-206,406-407`、`DialogHost.qml:331-429`；输出设备仍跟随系统默认 |
| 导出 | 选择路径、单文件/队列入口、暂停/取消、进度、计数、ETA、编码策略、trim、SR target | `QmlPlayerBridge.h:214-240,499-507`；QML 不改变导出执行器语义 |
| 诊断与组件 | 诊断报告、复制、组件清单、状态警告 | `QmlPlayerBridge.h:88-92,537-541`、`SettingsPage.qml:270-336`；显示真实报告，不手写“已加载” |

## 已确认的缺口

以下项目是当前事实，不是本轮要补的后端任务：

1. 字幕轨、外部字幕、字体/字号/描边/背景/位置/延时没有 engine/bridge 状态。`CineBar.qml:355-372` 和 `DialogHost.qml:314-328` 明确禁用并提示“尚未接入”。
2. 输出设备选择和立体声下混没有 bridge 接口，当前跟随系统默认设备。`DialogHost.qml:393-429` 已明确标注。
3. PS5 的编码、请求码率、DualSense 转发、陀螺仪校准和外网串流细节没有 UI 接口。`DialogHost.qml:245-269` 只保留现有 host/PIN。
4. 屏幕捕获的鼠标指针、裁剪、填满窗口、帧率上限和捕获方式选择没有 UI 接口。`DialogHost.qml:288-311` 只使用已有 target 枚举。
5. `resumeCaptureSession()` 当前只发出“采集会话恢复尚未接入”提示，没有重新打开动作。`src/ui/QmlPlayerBridge.cpp:963-980`；首页 continue 行不能计为恢复通过。
6. `openProjectPage()` 当前只发出“打开项目页面尚未接入”提示。`src/ui/QmlPlayerBridge.cpp:434-437`；关于页按钮不能计为真实页面导航。
7. 节点预设目前可以保存和管理，但导出执行器遇到节点预设会提示“节点预设尚未接入导出执行器”。`src/ui/QmlPlayerBridge.cpp:1277-1283`；不能把列表预设的导出结果外推到节点预设。
8. `displayFpsKnown` 固定为 false，`displayFps` 返回 0。`src/ui/QmlPlayerBridge.cpp:191-195`、`ProPage.qml:312-313`；“未测”是正确结果，不得用提交 FPS 或源 FPS 冒充显示刷新率。

## U1 结论

- U1 只读盘点：**通过**。已有 bridge 能力和缺口已登记。
- 当前不需要为完成 QML 页面去改 engine/pipeline/source/sink/media/shader/CMake。
- QML 发现缺口时的行为固定为：显示真实的未接入状态、保存日志、停止该交互点；不得添加假 setter、默认值或第二份状态。
- 如果后续发现 bridge 自身存在 UI 线程、信号生命周期或序列化错误，只允许在 QML bridge 文件内修复，并在修复记录中证明调用目标和 engine 行为未变。

## 后续验收约束

1. 每轮先运行 `scripts/acceptance/ui-migration-scope-guard.ps1`；失败即停。
2. U2-U4 只使用 `VEYRA_BUILD_QML_UI=ON`、`veyra_qml_ui.exe` 和同一份 QML staging。
3. 任何缺口不得通过修改冻结链路“补齐”；需要新增能力必须另开后端计划和授权。
4. `displayFps`、采集恢复、节点预设导出和项目页四项必须在验收报告中保持未通过/未接入的事实，不得写成 2.0.0 UI 已覆盖。
