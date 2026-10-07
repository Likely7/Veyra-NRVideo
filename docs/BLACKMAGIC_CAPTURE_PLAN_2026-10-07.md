# Blackmagic / DeckLink DirectShow 黑屏兼容

2026-10-07，用户要求兼容日志中的两个采集条目，并补充截图名称为 `Blackmagic WDM Capture`、`Decklink Video Capture`；用户无法确认物理卡数。两个名称是两条驱动入口，不据名称合并设备、不宣称两张卡。

从已验 VFG 候选 cc22e88 在 `E:/项目/Veyra/worktrees/blackmagic-capture-20261007` 隔离施工，保留其 VFG 优化。Desktop 脏工作区及其它 31 工作树不改。独立不可变 start.json、完整 Git bundle、原始日志/截图副本分别存放 `E:/项目/Veyra/archives/blackmagic-capture-20261007`、`logs/blackmagic-capture-20261007/input`。先执行本轮 blackmagic-control.py，不修改旧 guard/baseline。构建、测试、临时、候选包统一 E 盘同任务目录。无 Agent、竞争压力、驱动/用户配置修改、merge/push/Release/关机授权。

## 事实和未证实项

- 用户日志来自 2.0.4 / RTX5070Ti / 驱动576.88，五次连接均有连续原始帧、增强和送显记录。ARGB32 720p50、UYVY 720×486/29.97、HDYC1080p30、v2101080p30均被尝试。
- HDYC 被识别为未知格式，绕到系统 RGB32 转换。原生直连只连接视频输出与 Veyra sink，没有按 OBS 为 WDM 设备寻找/连接上游 crossbar。UI 无驱动属性入口，默认格式只按性能排序取第一项，不能代表实际信号制式。
- 五次连接社区 NR 均发生 BAD00002 / SEH C0000005，现有回退继续播放；此为独立故障记录，不以更换用户运行库掩盖采集问题。
- 收到帧/Present 返回成功不能证明像素非黑。日志没有像素统计，也没有 Blackmagic SDK 的信号状态。实际型号、HDMI/SDI、输入制式未知；不能直接断言 root cause。

## 最小闭环

1. 从既已登记的 obsproject/libdshowcapture 固定提交 c13d4b7b0c66979396ba0a9060c9aafc15bb7b22（LGPL2.1+）移植 WDM medium 查找与上游 crossbar 连接。对已连接 pin 不重复连接；没有 crossbar 的普通 UVC 保持直通；找到但连不上显式报错。保留硬件已设路由，绝不随意切 HDMI/SDI。
2. HDYC 复用已有 UYVY GPU 解包并按 subtype 提供 BT709 缺省，明确元数据与用户覆盖仍优先。保留尺寸/stride/方向/颜色防错及 CPU 诊断回退。v210 保持既有系统转换，不伪称原生10bit支持。
3. 提供设备/输入选择器的系统属性页入口，用户主动打开；采集使用中禁止争抢同一设备，提示先断开。关页后重新查询能力；优先显示驱动当前格式（仅 Blackmagic），仍保留已记住的用户选择。注明59.94与60、逐行与隔行必须匹配；不自动遍历打开所有模式。

补充已发现的格式身份缺口：旧 VideoInfo2 key 没有 dwInterlaceFlags，p30/i60 和不同场序可能碰撞。新 key 保存扫描标志；旧 key 仅唯一匹配时迁移，歧义必须重选。驱动属性页的实例内 SetFormat 变更须携带精确 key，避免关页释放 filter 后丢弃用户所选模式。驱动 GetFormat 仅是配置默认，不能冒充实时信号检测。
4. Blackmagic 输入端只对已在 CPU 的原始样本做低频有界网格统计，不能新增 GPU 回读。持续黑采样仅提示检查输入/制式，不将黑场内容等同无信号；日志区分源像素与后续呈现。

## 验收和交付

- 构建本轮 EXE 与既有采集颜色/格式/GPU目标；单进程测试≤300秒、构建≤900秒。
- 检查 HDYC VideoInfo/2、padding、短样本、矩阵缺省与明确覆盖、上下翻转、native sink 交付；真实 GPU 色卡输出非黑且等同 UYVY BT709，不能只检查 exit0。
- crossbar 合成接口覆盖 medium 匹配/方向/已有连接/失败回传和属性页缺失；原生 USB 卡回归若本机可用，真实卡与合成证据分开记录。
- 检查 QML/驱动对话入口、黑采样提示及恢复、保存格式优先、普通卡无额外开卡/制式变更。保留其它算法/运行库原字节。
- 交付独立本地测试候选、对应源码和简明实卡步骤。Blackmagic 真卡不在本机，不把 mock/USB/RTX 通过外推该卡已解决。

## 参考

- https://github.com/obsproject/libdshowcapture/tree/c13d4b7b0c66979396ba0a9060c9aafc15bb7b22/source ：device.cpp FindCrossbar/ConnectPins；dshow-base.cpp medium 和直连帮助函数。
- https://sdk-doc.blackmagicdesign.com/decklink-sdk/section1.html ：Desktop Video 的 DirectShow/WDM 两类过滤器与设备配置工具。
- https://documents.blackmagicdesign.com/UserManuals/DesktopVideoManual.pdf ：物理输入/信号制式设置。
- https://learn.microsoft.com/en-us/windows/win32/directshow/displaying-a-filters-property-pages ：标准 ISpecifyPropertyPages/OleCreatePropertyFrame。
