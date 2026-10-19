# 采集格式识别与旧版入口修复

日期：2026-10-17。用户授权：修复前次 OBS 对比发现的问题，并在采集页面增加旧版格式选择入口。

基线：公开 main `d68d6be9c37127350698a1d4d47fc01b36b29475`，继承窗口游戏模式修复 `1295d6b`；本任务在隔离分支 `codex/capture-format-fixes-20261017` 实施。没有修改原 main、运行库、模型、驱动或真实用户配置，没有发布或生成用户安装包。

## 改动与结果

1. `GetNumberOfCapabilities` 未实现、列表为空或全部格式块不可解析时，使用采集 pin 的 `EnumMediaTypes`。明确的设备访问错误不会伪装成成功。部分能力读取失败保留可用项，并显示阶段、HRESULT 和列表可能不完整的提示。未知帧率保持未知，不填造 59.94/60。
2. 读取 `VIDEO_STREAM_CONFIG_CAPS` 的尺寸上下限、步进和帧间隔范围，保留原始离散类型及 `IAMVideoControl` 帧率。范围派生项明确标为“范围候选（连接时验证）”，不是声称这些模式已实卡验证。列出常用合法尺寸/帧率及边界，额外尺寸可用“自定义分辨率 → 检查并选择”请求；不枚举范围中的无穷组合。
3. 帧率菜单仅在标准帧率附近吸收 1 个 100ns 整数取整误差。同一组收集全部像素格式，而持久化和实际连接仍使用各自精确 interval、subtype、format type、尺寸和扫描标识。59.94 与 60、逐行与隔行不合并，移除旧菜单二次按显示值去重造成的丢项。
4. 配置时重新发现能力，用精确 key 找回原生模板，构造具体媒体类型，核对 `SetFormat`、`GetFormat` 和实际连接。尺寸、帧率、像素格式或扫描方式被驱动替换会失败并说明原因，不使用最近似模式冒充成功。`SetFormat=E_NOTIMPL` 允许尝试精确连接，但必须通过实际连接验证。
5. 优先明确的 video capture pin；只有一个未分类视频输出时允许兼容；预览 pin、输入 pin、多个未分类输出不会被盲选。连接沿用选中的 pin。保留 VideoInfo/VideoInfo2 和隔行信息。
6. 采集页面增加 **旧版格式选择 → 打开旧版格式列表**，在一个可搜索、虚拟化列表中组合显示分辨率、帧率和像素格式，包括常用帧率开关隐藏的项。两个入口共用同一个精确 key、保存和恢复逻辑；不支持的布局仍不能连接。查询或设备切换时关闭旧列表，避免选择过时数据。

范围候选由所属 COM 线程持有的原始模板克隆，不修改驱动返回的模板；重建 sample size、stride 对应字节数、裁剪框和 VideoInfo2 宽高比。原始模板不穿过 UI 线程或存入配置。失败的自定义尺寸请求保留原有效选择。能力数、格式块和总内存有界，达到上限明确标记列表不完整。

没有引入 OBS 的采集运行时、排队、缓冲、解码、音频或渲染实现。现有 DirectShow 低延迟 native/compressed 路径、BMD 原生后端、回调、增强、调度和 UI 33ms/250ms 节奏保持。旧 RGB 兼容路径也核对上游实际类型。没有延迟性能改善的测量结论。

## OBS 对照来源

- OBS 32.2.2：`ba2f32bdf791005443988a4955e963663e16b1ed`。
- 对应 libdshowcapture：`8878638324393815512f802640b0d5ce940161f1`；`src/dshow-enum.cpp`、`src/dshow-base.cpp`。
- 不照搬 OBS 回退时填 59.94、静默最近似模式替换等行为；维持 Veyra 的精确身份和 VideoInfo2 扫描区分。
- 首次对比的复现：166667 与 166666 两种帧间隔分别提供 NV12/YUY2，旧版三联默认菜单只能访问一个像素格式；现在同一显示组内两者均可访问，各自精确 key 不变。

## 验证及限制

- 原生 Linux C++20：`CaptureCapabilityTests`，25 个检查；尺寸范围/步进、保存 key、未知帧率、59.94/60/p/i 分离、精确像素选择，以及 16,000 条记录的菜单生成。
- Windows 目标编译、链接并在 Wine 10 运行：`DirectShowFormatDiscoveryTests`，24 个检查；未实现/空能力回退、权限失败、部分失败及其诊断阶段、VideoInfo2 范围构造、实际类型替换拒绝、capture/preview/未分类 pin。驱动为 COM 测试替身，未访问真实设备。
- 同样编译、链接并在 Wine 运行现有 `CaptureFormatSelectionTests`，0 failures。
- Qt Quick Test / Qt 6.8.3 software/offscreen：新增 `tst_capture_legacy.qml` 4 项通过，搜索、精确选择、关闭/重开、禁用布局和空结果；16,000 行只实例化可见委托，独立 UI 探针看到 17 个列表子项。现有 `tst_menu_layout.qml` 9 项通过。
- `CaptureCardSource.cpp` 与 `QmlPlayerBridge.cpp` 的 Windows x86-64 COFF 对象编译通过。使用 MinGW 14、真实 Win32/DirectShow、Qt 与 FFmpeg 公开头文件；NGX 公开头文件仅作桥接语法检查的外部依赖。MinGW 对原有 MSVC `__declspec(uuid)` 声明有已记录警告；此检查不代替 MSVC 链接。
- `DialogHost.qml`、`CaptureLegacyPicker.qml` 和新增 QML 测试的 qmlcachegen 字节码/basic-block 检查；三语 i18n 占位符与缺失检查；`git diff --check`。

本机为 Linux，无 Windows GPU/真实采集卡和项目完整的 MSVC/厂商运行环境，因此**未完成完整 Windows 程序构建或实卡端到端验证**，不能宣称反馈用户的硬件已恢复。上述 Wine 运行属于程序逻辑测试。临时工具、公开 SDK 头文件和 Wine 全在仓库外，不进入产品源码或发布包。原始回执保存在 `/workspace/scratch/capture-format-fix-checks/`。

## Windows 后续验收

在项目原有 Windows 构建环境构建 `veyra_qml_ui`、`veyra_capture_format_selection_tests`、`veyra_capture_capability_tests`、`veyra_dshow_format_discovery_tests`、`veyra_qml_quick_tests`，运行相关测试后，用反馈采集卡核对：

1. 关闭 OBS，刷新 Veyra，比较同一驱动在 OBS 中的分辨率、59.94/60、像素格式与隔行项；旧版列表应可找到被常用帧率过滤的精确模式。
2. 三联菜单和旧列表分别选择 NV12/YUY2/MJPEG/其他实际支持项，连接后对照日志的 requested 与 connected；驱动不接受的范围候选必须明确失败，不能变成另一个模式。
3. 保存/重启/继续上次/断线重连，确认按 key 恢复；合法自定义尺寸可验证，非法步进保留原选择。
4. 设备被占用、部分枚举失败、查询中切换设备或关闭对话框时，不出现旧列表覆盖新设备或静默空列表。
5. 正常预览、音频、NR/SR/FG 及已有低延迟链路做定向回归。格式枚举仅在打开/刷新/配置时进行，没有每帧增加枚举操作。
