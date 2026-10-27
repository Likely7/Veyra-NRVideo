# PS5 目标模式施工进度

## 当前结论（2026-09-12，节点二）

用户在收到《羊蹄山之魂》实测请求后反馈：“可以了，我测试了没问题”。记录为本轮用户实机验收通过，不推断为蓝牙/多设备/仅观看账号共存逐项通过。以下节点一的待办是当时状态；本节与末尾节点二记录优先。未发布、未推送。

基线计划提交325b9e9，tag checkpoint/ps5-full-repair-2026-09-12。用户指定《羊蹄山之魂》实测。目标仍active，未发布。

## 节点一实现

- AppShell timer独立编号1/2/3：遥测、动画、手柄。endTransition不再关闭手柄；SetTimer失败记录。
- RemotePlaySessionSource真实AU/decoded计数及一秒速率窗口，UI分别显示接收/解码，窗口未满显示采样中。decoded mailbox覆盖与入队丢弃分开。
- SDL gyro(rad/s)/accel(g)、双指ID/位置/抬指；Chiaki orientation tracker用于姿态，finite/时间断点检查。当前仍为8ms状态采样，极短触点边沿、传感器时间序列与校准待继续收口。
- enable_dualsense启用，rumble/trigger/motion-reset/raw haptics回调经有界feedback到输入owner；SDL普通震动、DualSense扳机report和四通道触觉分别输出。3kHz双通道触觉重排为四通道（前两声道静音），SDL转换设备频率；仅唯一手柄和唯一四通道Wireless Controller/DualSense端点自动路由，缓存100ms。多设备映射、中文端点名和蓝牙能力仍待完善。
- PS5面板加入仅观看选项，重连生效；不读取电脑设备，不发有效输入，保留协议。主机账号与本地手柄能否共存仍待实机验证。
- PS5独立displayReferred709意图：保留码流BT709标记，使用理想黑位BT1886 EOTF进linear，再走原sRGB呈现。其他来源默认保持旧行为。依据libplacebo pl_transfer_from_av将BT709映射BT1886，明确EOTF != OETF：https://raw.githubusercontent.com/haasn/libplacebo/master/src/include/libplacebo/utils/libav_internal.h 。GPU数值通过不证明用户发灰已全部解决。

## 实际证据

所有日志在项目logs目录，单次测试不超过300秒。

- build-product.cmd：ps5-ui-input-build.log、ps5-full-input-build.log、ps5-controller-test-build.log通过。feedback首建漏ControllerFeedback前置声明失败；补齐后ps5-feedback-build-retry.log通过。
- scripts/remoteplay/test-ui.ps1：真实20次模式动画完成及20次全屏切换、面板两次开关及本地错误配对拒绝；ps5-controller-final-ui/。没有以空白UI测试冒充真实串流。
- run-short-test.ps1，30秒，boundary --virtual：真实SDL虚拟设备双指生命周期、sensor单位、失焦归零/rumble释放、feedback容量通过；ps5-controller-virtual.stdout.log。没有把虚拟输入发送给PS5。
- boundary默认及H264/H265 source回归通过：ps5-controller-boundary/source.stdout.log。
- native CTest69/69、0.48秒：ps5-controller-ctest.log。
- Yuy2ColorTests --remote-yuv：真实D3D12和Presenter读回，Limited/Full乘BT601/709灰阶/色块，最大误差1个8bit码值，present误差0，4/4通过；ps5-color-gpu2.stdout.log。原YUY2回归通过：ps5-existing-color-gpu.stdout.log。读回只在测试中。
- delivery通过47.45秒：logs/delivery/fb3cd22c85c9418ca7c2a37bb43e8e27/result.json，包含真实NR/NVOF/播放/导出。
- exe SHA256：750548CD479ADD7346E8D4BA683A579F8A843352F0545B48BDE23A618572961E。此后仅颜色测试夹具增加色块，产品exe未变。
- RemotePlay OFF增量23步通过：ps5-controller-off-build.log；命令脚本out/remoteplay/audit-20260911/build-off-check.cmd。

## 当前仍要完成

真实游戏反馈、仅观看主机直连、USB/蓝牙拔插与完整效果路由、gyro静止偏移校准、触摸短边沿与传感器事件采样、设备能力显示、同源视觉对照、暂停与网络恢复。不能声称全部手柄功能实机通过。用户已收到测试请求，需打开新测试版供测试；没有发布。

UI已通过但完整控制器仍需上述收口。后续修改不要基于本页把未验收项误标完成。外部SDK/runtime不入Git。

## 节点二：输入事件、校准与交付存档

- SDL事件保留双指down/move/up，16项有界触点队列；同次UI采样内的按下/抬起分次传出。动作与触摸按键独立。
- gyro/accel使用SDL事件时间与最多16项批次；Chiaki跳过重复时间戳，防止同一快照重复积分。时间断点重置姿态。单位仍为rad/s与g。
- PS5面板增加陀螺仪校准：返回播放器后开始计时，120个稳定样本估计静止偏移；运动样本清空积累，10秒超时保留原值。换设备清理校准。能力状态显示传感器、触摸板、扳机路径和真实触觉端点打开状态；仅观看不再提示电脑手柄生效。
- UI至解码owner不再只覆盖最新ControllerState：16项有界队列保留边沿和传感器批次，4ms发送机会，100ms过期上限。失焦/断开使用独立inputActive状态立即清空排队动作；正常抬指不被误判为失焦。溢出先释放再发最新状态，记录警告。
- 修复手柄重连时重复初始化SDL音频子系统导致引用计数累加。
- 最终校准超时修正在用户实测版本之后，仅改变校准计时起点；最终二进制另经自动回归，不冒充用户重新测试过该二进制。

### 本节点实际执行

命令脚本均在out/remoteplay/audit-20260911（忽略目录）；测试使用scripts/run-short-test.ps1的30秒单项上限。

- build-product.cmd -> logs/ps5-input-accepted-build.log：成功。此前ps5-input-events/queue/calibration-build发生LNK1104，因为正在运行的veyra.exe被锁；正常关闭已结束测试的窗口后链接成功，没有强杀。
- build-native-source.cmd -> logs/ps5-input-accepted-native-build.log：20/20；build-off-check.cmd -> logs/ps5-input-accepted-off-build.log：成功。
- boundary默认/--virtual -> logs/ps5-input-accepted-boundary.stdout.log、ps5-input-accepted-virtual.stdout.log：exit0。覆盖输入跨线程边沿、失焦清队、过期/容量、双指短触、传感器单位及静止校准、反馈容量；虚拟输入不连接PS5。
- ctest --test-dir out/remoteplay/audit-20260911/native-source-repair --output-on-failure --timeout 30 -> logs/ps5-input-accepted-ctest.log：69/69，0.50秒。
- veyra_remoteplay_source_tests（single、reorder.h264、hevc三个夹具）-> logs/ps5-input-accepted-source.stdout.log：exit0。
- scripts/remoteplay/test-ui.ps1 -> logs/ps5-input-accepted-ui：20次模式动画及20次全屏切换、面板开关和无效配对拒绝通过，截图复查布局正常。
- 最终out/remoteplay/product-repair/veyra.exe SHA256：BCF9A2E98BD0E5553FE9A9772FEAC7F4226360A41127B4A573C3B5E4ACF3B86A。
- 本节点未改颜色/GPU增强。节点一真实D3D12颜色夹具与47.45秒delivery证据仍单独保留，没有重复运行或冒充当前实机逐项验证。

### 验证边界

用户确认本次测试没问题。本轮修复交付收尾；不把未报告的场景列为已通过。蓝牙完整触觉、多手柄与多音频端点、中文端点命名、仅观看同账号/第二账号共存、网络断续/休眠恢复和同源视觉定量对照仍需对应设备/场景验证。当前触觉只自动选择唯一手柄及唯一匹配名称的四声道端点；歧义不自动路由，不能承诺多设备或蓝牙与USB等价。后续唯一优先任务：若用户在其他连接方式复现问题，按设备及日志补对应能力适配；不重新改已验收链路，不自动发布。
