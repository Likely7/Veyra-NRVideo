# Veyra Agent 执行规则

更新：2026-11-18。用户本轮明确授权整合八项社区 PR 并做好兼容后更新 GitHub main；此前 Claude/Codex 功能整合一并保留。旧规则全文在[归档](docs/history/rules/AGENTS_before_main_2026-11-18.md)，其中带日期的施工树、版本与授权属于当时记录，不能覆盖用户后续指令。

## 入口与当前状态

- 唯一主线源码：`E:/项目/Veyra/worktrees/main-claude-anr-20261113`，分支`main`。桌面旧工程保留共用`.git`与未提交工作，不能按旧副本删除。
- 当前构建/验证任务：`codex-pr-integration-20261118`（Windows build 子目录 `app`），目录分别为`E:/项目/Veyra/{build,tests,logs,reports,tmp}/codex-pr-integration-20261118`。主线已包含完整处理后虚拟摄像机及Claude协议2/有序送帧/OBS节拍修复；实际验证结果以[本轮记录](docs/PR_INTEGRATION_2026-11-18.md)及外部FINAL回执为准。
- 当前应用版本仍为2.1.9，不因合并自动改版本或重发资产。11月17日虚拟摄像机附加包的既有发布记录见WORKLOG；本轮允许上述源码整合推送 GitHub main，不含新 Release/代发消息或关机。
- 先读[当前状态](docs/CURRENT_STATUS.md)、[开发验证](docs/DEVELOPMENT.md)、任务对应计划/记录及[WORKLOG](docs/WORKLOG.md)，按需查[历史索引](docs/history/INDEX.md)。不要重新执行旧Loop、STOP、控制哈希或旧UI-only scope guard来阻塞后续已授权工作。

## 工作纪律

1. 不说谎，不迎合；把事实、推断、历史结果、本次测试和用户反馈分开。提交FPS不是物理刷新率或屏幕端到端延迟，未测硬件不宣称通过。交付前做对抗式自审。
2. 开工检查git状态、工作树、分支、进程和适用规则。不覆盖未提交内容，不停止用户进程，不抢采集卡/主机连接。保留已确认的用户设置。
3. 单Agent；构建并发最多2、单次≤900秒，每个测试≤300秒。按改动运行有意义的相关检查，失败保留证据。
4. 测试使用私有data-dir/log/缓存。TEMP/TMP只对子进程设置；不改全局环境、真实配置、驱动、凭据、运行库或模型。
5. 以最小职责边界修复，不顺手重写全工程。结构整理保留接口、配置编号/格式、默认值、信号和处理行为。UI统计33ms/状态250ms刷新、GPU默认高、VFG自动调控默认关等既定决定不擅自改变。
6. 不发送跨会话/邮件/群消息，不评论/关闭issue，不push/Release或关机，除非用户当前明确要求。系统组件更新交由Windows管理员确认，不自动批准UAC或更改系统授权策略。

## 路径、保全与清理

- 所有新构建、测试、下载、诊断、临时、打包和解压产物在`E:/项目/Veyra/{build,tests,logs,reports,tmp,downloads,deps,test-packages,releases,verify,archives,worktrees}`对应任务中。源码/受控脚本文档进源码树；不要向桌面、Downloads、C盘或系统Temp新增研发产物。
- 依赖实体：`E:/项目/Veyra/deps/core`；Qt：`E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64`。`C:/veyra-deps`及`deps/core/qt-veyra`仅兼容Junction，保留入口，不重建C盘依赖；其他根见`E:/项目/Veyra/deps/DEPENDENCY_ROOTS.json`。
- 正式包/对应源码保留2.0.7及以后已发布原件。2.1.9原ZIP与虚拟摄像机附加ZIP分别保留；不要把测试manifest写进发行供体。
- 编辑硬链接stage必须新建文件后原子替换，禁止原地写穿供体。手工stage必须同步本次build的全部着色器并独立核验SHA。
- 清理先验证绝对路径、父链reparse、进程、文件身份和恢复清单。分支归档保留tip映射及通过verify的bundle；只移除已合main、干净、无本地专用文件/连接且未在用的工作树。不强制删除脏树、唯一素材/配置、依赖、共用Git或许可对应源码。
- Lecram两线继续暂停，AverMedia未完成诊断及原生音频驱动原型保留隔离，不因“整理分支”恢复产品功能。旧脏树的已收录快照不重复合并。
- 已安装摄像机的`C:/Program Files/VeyraVirtualCamera`是实际系统组件安装位置，不能当研发垃圾删除；更换协议需明确更新组件，不把旧模块黑屏误判为新引擎失败。

## 产品与实现合同

- Windows x64、C++20、D3D12、Qt/QML，直接NGX，不另接Streamline。播放/采集/图片与视频导出共享增强逻辑、颜色、Guidance和reset。
- 采集卡和普通视频只有最终像素。估算光流/深度不等于游戏原生输入；A/B插帧需要B真正到达，不能把采集固有延迟当免费未来帧。
- 保留既有默认图与已授权的实时NR→SR路径，FG在效果之后，OSD/字幕遵守现有叠加合同。导出、暂停单帧和图片不得套用实时预览丢帧策略。
- 所有输入显式解析range/matrix/transfer，统一工作纹理；sink只做一次输出转换。HDR代理增强和原生HDR模型推理分开表述。
- seek/转场/PTS断点/capture drop/resize/切源/暂停恢复/device lost显式reset；队列与历史有界。不伪造PTS、静音或放宽告警掩盖欠速。
- 正常增强/视频导出不得新增像素回读、逐pass CPU fence等待或无界队列。虚拟摄像机是已明确授权的兼容输出边界：开启时才运行GPU NV12转换/有界异步回读及共享队列，不使渲染线程等待消费者；摄像机视频与音频单独处理，XeSS显示端生成帧不冒充图内输出。
- Guidance遵守current→previous、post-SR workingExtent像素单位，低置信度衰减；depth/confidence格式、资源状态和NGX参数沿用既有合同。
- RAII与逆序释放、受限绝对路径加载、HRESULT/NGX错误、尺寸/格式及GPU计时日志保持。Smooth Motion由用户自行管理，不改驱动，不把其生成帧冒充软件录制输出。

## 来源、运行组件与交付

- 专有SDK、DLL、模型、头文件、LIB、抓帧、凭据与配置不进源码Git/LFS/资源/base64。不得修改、重签名、伪装或从游戏/驱动缓存提取二进制。
- 用户可自行替换DLL；软件不以固定hash或签名锁拒绝替换，但默认发行组件必须按2.1.9及本轮明确批准的来源/manifest/许可证逐文件审计。不自动扩大公开分发范围；ANR Beta资产仍排除公开包。
- 保留真实patched FFmpeg及对应源码、补丁与许可：PS5 H.264 slices256、dav1d和压缩PGS所需zlib，不退回未打补丁底包。
- 成熟且许可证兼容的开源实现优先适配；保留来源仓库、固定提交、许可证、逐文件修改说明。OBS/libdshowcapture摄像机来源在THIRD_PARTY_NOTICES与UPSTREAM.json。ReShade/RenoDX add-on不进主线或运行包；含Chiaki组合遵守AGPLv3及其OpenSSL例外。
- 用户确认分发的BGM只在运行包media目录，不进源码Git或源码ZIP。保留其独立notice与manifest。
- README/Release双二维码并排各width220、用户指定主页与零起点教程保持；编辑发布资产仍需本轮授权。
- 本次记录写实际命令、结果、失败/修复、路径、空间及未验范围；不能把接口桩、同帧重复或理论FPS当完成证明。
