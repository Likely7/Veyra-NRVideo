# PS5 实机反馈修复计划（2026-09-12）

## 目标与边界

用户已实测确认局域网连接、USB基础手柄和已测试的NR/SR/FG组合可用，主观延迟优于其采集卡；这不是全部格式/全部异常路径的验收。本轮用户要求先排查、写方案，不发布、不自动进入代码施工。最新要求把陀螺仪、完整触摸板及DualSense效果提升为必须处理的游戏功能，取代旧文档“高级手柄不在本轮范围”的交付边界。保留用户优先选择手柄直连PS5的方向，但不能以此免除电脑端必要输入支持或伪称同账号共存已通过。

基线9c683fc，工作树开始时干净。本次仅更新文档；不停止当前串流、不改PSN账号/代理/防火墙、不索取租赁账号密码。源码SDK/runtime隔离规则继续；重大施工节点本地Git存档，单次测试<=300秒。

## 已确定的代码问题

### 1. 动画与手柄重复使用WM_TIMER ID 2（最高优先级）

apps/veyra/ui/AppShell.cpp：
- 第139行switchMode为模式动画SetTimer(mainWindow,2,16)。
- 第323行RemotePlay连接为手柄SetTimer(mainWindow,2,8)。
- 第355行手柄wp==2分支提前return，后面的第357行动画wp==2分支在RemotePlay构建中不可达；甚至没有串流时也可能关闭动画timer。
- 第60行endTransition无条件KillTimer(mainWindow,2)。全屏第77行、WM_SIZE第286行、DPI更新都调用它，所以这些操作会同时停止手柄轮询。
- 第392行状态区重绘以!transition.running为条件；动画无法正常采样结束，会连带停止该刷新。

以上是确定冲突，能解释用户三个相关症状的共同原因。上一轮只提“前台焦点判断”不完整；焦点作为第二层检查保留，不得再当唯一原因。截图中的按钮背景/残影是否全部由该冲突引起仍需视觉复测。

修复：集中声明互不重复的Telemetry/Transition/Controller timer ID；每个timer独立启动/停止；endTransition只能清理动画；不能通过常驻无界轮询或全窗口高速重绘掩盖问题。检查SetTimer返回值。覆盖没有RemotePlay会话时普通播放器动画，RemotePlay ON/OFF构建都要编译。

### 2. PS5输入FPS使用了采集卡统计

EngineController.cpp第70行RemotePlay标记capture=true以复用live调度；第857行仍从captureStats.callbackFps等填snapshot；AppShell.cpp第390行capture分支直接显示captureFps。RemotePlay有自己的source owner，未接入这份采集卡回调计数，输入0.0不能靠重绘修好。

修复：为不同live来源提供统一但来源明确的诊断快照，使用真实网络视频AU/解码帧计数分别计算接收/解码速率，处理和有效生成继续使用现有GPU完成指标。统计窗口不足显示“采样中”，不能用目标60替代测量。保留decoded mailbox丢弃、调度跳过与网络丢失的区别。不能把网络接收、软件处理和端到端延迟混为一个数。

### 3. DualSense目前仅基础输入，没有完整游戏功能

ControllerInput.cpp只读普通按钮/摇杆/扳机和Touchpad按钮；没有启用/读取gyro或accelerometer，没有读取触点。ChiakiBackend.cpp submitController只映射buttons/axes/triggers，touch与motion保持idle。事件处理default忽略手柄反馈，没有将主机回传的震动/扳机/触觉送到设备。SDL_FlushEvents全丢事件不适合未来要求保留传感器时间序列和触点边沿的实现。

这是缺失实现，不是用户USB线或某游戏不支持。

## 发灰：已知链路与待证伪假设

真实日志logs/veyra-app.log约17:11–17:13 UTC记录range=1 assumed=false、matrix=2 assumed=false、transfer=2 assumed=false，即Limited/BT709/BT709。RemotePlaySource.cpp先提供fallback，解码后resolveFrameColor覆盖，EnhanceGraph.cpp再次解析。YuvToLinearRgb.hlsl包含16–235/224范围转换与BT709逆曲线；PresentBlit.hlsl按常量选择sRGB输出编码。由此不能断言“忘记Limited展开”，也不能证明实际图像颜色正确。

排查顺序：
1. 基线：同一PS5静止暗部/灰阶/白色UI画面；记录H264/H265、Windows HDR/显示器模式、软件参数，先全关NR/SR/FG。对照chiaki-ng和Veyra时保持同一编码/分辨率/显示器，避免两个串流会话同时争用。PS5 HDMI输出另受电视HDR/游戏模式影响，不能作为唯一像素基准。
2. 审计原始AVFrame颜色字段、格式/位深/linesize、上传平面、GPU常量、实际present输出格式/色彩空间、alpha与合成路径。日志明确“码流信令”和“fallback假定”；逐段定位黑位、白位与中灰首次偏离处。
3. 构造Limited及Full的灰阶/色块H264/H265夹具，验证黑白端点、近黑层次、中灰、BT601/709矩阵、U/V顺序和色度采样。检查RGB/YUV420到上传纹理是否有第二次范围转换。
4. 重点检查BT709逆OETF→线性→sRGB编码的显示意图。这并非天然恒等变换，会改变中间调；需要明确视频显示EOTF（含BT1886/gamma与显示环境）和增强工作空间的契约。对照成熟播放器的显示结果后决定修复，不能直接全局把BT709标记改为sRGB。
5. 检查普通/直播兼容、窗口/全屏的swapchain色彩空间与alpha；视频区域必须不透明、空闲纯黑，不应混入毛玻璃/背景颜色。
6. 基础通过后依次NR/SR/FG及组合，检查Parity encode/decode、SR输出、FG原帧/生成帧的色彩一致性和重复编码。

禁止用加饱和度/压黑/提高对比度作为默认补丁。必要只在隔离诊断里抓少量中间帧并存ignored目录；正常播放不得引入GPU回读或每帧fence等待。若修公共颜色层，文件、图片、采集、导出都需针对性回归，不能只修PS5。

## 手柄两条路线

### A. 电脑端完整DualSense（必做）

- 扩展产品ControllerState为明确的触点生命周期（双指ID/down/move/up）及带时间的gyro/accel/orientation；按固定chiaki controller定义匹配单位和坐标系，不能把传感器原值直接塞进不同单位字段。
- 用SDL3能力检测与传感器启用API；触点用SDL_GetGamepadTouchpadFinger，0..1坐标按上游DualSense约定映射和夹取。点击与滑动独立，抬指必须发送，不用随机重建触点ID。
- gyro/accel进行坐标转换、静止偏移校准，按上游motion/orientation实现验证采样dt、融合、归一化、重连reset；不做“陀螺仪模拟右摇杆”冒充原生动作输入。
- owner线程保持协议调用序列；UI输入/专用输入owner只发布受控快照或有界事件。GPU欠速不能阻塞控制。必要事件不得被FlushEvents无条件吃掉。
- 主机反馈分类接入：普通rumble、自适应扳机effect、DualSense细腻触觉。普通SDL rumble不是完整haptics。核对固定上游Windows实现、SDL3接口/HID报告及触觉音频端点，再选择复用与适配方式；USB优先，蓝牙单独列能力和测试，不自动承诺等价。
- 实现独立受限反馈队列和设备匹配；不能把触觉PCM混入用户扬声器。设备拔出、停止、失焦/控制权释放时清零按键/触点/震动/扳机，防卡键与残留阻力；明确失焦策略与窗口所有权，禁止全屏误判。
- 实测至少一个触摸板滑动/双指用例、一个gyro动作游戏、一个触觉/自适应扳机用例；游戏内容由用户操作，不能用计数增加替代真实效果。

### B. 手柄直连PS5、Veyra仅接收音画（优先验证用户偏好）

增加“电脑手柄控制/仅观看”明确选项；仅观看关闭电脑设备接管和有效输入转发，切换前释放已按输入，但保留协议所需保活。不能宣称有通用“video-only绕过账号占用”的协议开关。

先验证同账号串流与本地手柄共存；如果主机系统不允许，再列出独立串流账号/本地游戏账号方案与实际限制，经用户选择后操作。不可承诺租赁账号无需任何账号条件即可实现，不改授权、不接管登录。直连方案若成功，触觉/触摸板/陀螺仪由PS5原生处理；仍须验证游戏输入、音画、暂停/重连及账号切换是否会停止串流。

## 分阶段实施与验收

| 节点 | 工作 | 必须验证 | 本地Git节点 |
| --- | --- | --- | --- |
| 1 | 分离timer、修PS5诊断、模式重绘及全屏控制 | 连续20次日常/专业切换、20次全屏/窗口切换；resize/DPI/最小化恢复；timer互不停止，统计窗口更新，原播放器动画不回退；实际USB输入不断 | UI/input lifecycle |
| 2 | 颜色证据采集与最小根因修复 | 黑白灰阶和色块误差有基准；同码流对照；无增强与增强分开；不影响其他来源与导出 | color correctness |
| 3 | gyro+双指触摸完整链路、仅观看验证 | 数据方向/单位/边沿/断开释放测试，实际游戏可用；仅观看是否可行如实记录 | full input |
| 4 | 震动、扳机、触觉及USB/蓝牙能力 | 真设备反馈、正确设备路由、不混扬声器、不阻塞声音和视频；拔出重连与停止清零 | controller feedback |
| 5 | 综合恢复与文档 | 用户增强组合、UI操作、网络断续、休眠唤醒重连；用户实机逐项确认 | local acceptance |

离线测试每次<=300秒，UI典型短测30–60秒；色彩修改相关shader测试、ON/OFF构建和必要delivery，成功后不无目的重复整套GPU测试。线程生命周期失败需要日志/错误码，不得静默假成功。查找主机修复已有真实192.168.6.232回应证据，不重复推翻。

## 交付口径与后续

完成标准包含用户新要求的完整手柄路径，不能以“基础连接成功”宣布整体完成。图像发灰目前是用户观察且有可疑曲线链路，未取得根因结论；timer冲突是静态代码确定的缺陷。当前未做新的设备控制/图像抓取或产品修改。硬解/公网/HDR不是此轮先决条件；软件解码性能满意时先保颜色和游戏功能。Account ID内置获取流程另保留待办，不能为获得租赁账号信息要求密码。

## 2026-09-12 执行结论

本计划已进入修复并完成本轮交付，非仅规划。timer、真实速率、PS5显示曲线、双指/运动输入、反馈、校准与仅观看实现及测试见[施工记录](../reports/REMOTEPLAY_REPAIR_PROGRESS_2026-09-12.md)。用户反馈“可以了，我测试了没问题”；硬件覆盖边界按施工记录保留，不能扩展为所有设备全部验收。旧段落中的“尚未修改/待开始”仅表示排查时点。没有发布。

## 参考与复用边界

- 本地固定chiaki提交0e16950165f06e5c3291537c2eeba6e852be7120的gui/src/streamsession.cpp、controller实现及lib/include/chiaki/controller.h/session.h。复用时保留AGPL/OpenSSL exception及归因，不照抄Qt窗口/第二套播放循环。
- https://wiki.libsdl.org/SDL3/SDL_GetGamepadSensorData
- https://wiki.libsdl.org/SDL3/SDL_GetGamepadTouchpadFinger
- https://streetpea.github.io/chiaki-ng/setup/controlling/

在线文档用于API核对，不证明本机设备/固件能力已通过。最终代码、测试命令、日志、失败及剩余边界必须同步WORKLOG和本文件。
