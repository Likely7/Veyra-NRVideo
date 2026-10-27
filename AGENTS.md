# Veyra Agent 工作规则

## 现在从哪里开始

- 当前正式版 **2.1.1**，发行标签 `v2.1.1` / 源码标签 `v2.1.1-sources`，发行提交 `2fecacf`，已完整进入main。
- 当前施工树：`E:/项目/Veyra/worktrees/screen-recording-fix-20261022`。通用Git库仍在桌面旧工程`.git`，不可把桌面当普通副本删除。
- 先读 [当前状态](docs/CURRENT_STATUS.md)、[开发与验证](docs/DEVELOPMENT.md)、本任务执行记录及 [WORKLOG](docs/WORKLOG.md)。按任务需要阅读具体报告、运行库身份与许可证，不再要求每轮通读所有历史方案。
- 用户于2026-10-27明确授权执行项目整理1–4：清过时产物、收拢分支、归档文档、分职责整理桥接/QML并回归。本轮基线main `7bf02d4`，结构整理提交`d4b1034`已验证，后续从main继续，准确整合HEAD见本轮FINAL回执。**不push、不发布**，Lecram继续暂停。
- 完整历史授权与原规则在 [历史索引](docs/history/INDEX.md)；旧“未合并/未发布/冻结”按当时日期理解，不能覆盖用户后续明确授权。

## 工作纪律

1. 诚实报告实际结果，不迎合、不把推断或历史测试当本次验证。已验/未验、用户反馈/本机测试分别说明。
2. 开工检查`git status`、活动分支、工作树与当前进程。不覆盖未提交工作，不停止用户进程，不抢采集卡或真实主机连接。
3. 单Agent；构建并发最多2、单次构建≤900秒、每个测试≤300秒。不要恢复旧Loop、STOP、控制哈希排队；当前任务按最新用户指令及执行记录。
4. 不改真实用户配置、驱动、运行库、模型或凭据。测试使用任务专用data-dir/log/TEMP，TEMP/TMP只对子进程设置，不改系统环境。
5. 每次只处理可审查的职责边界；结构整理保持接口、配置格式、默认值、信号语义和处理行为。改动后做相关构建与回归，失败保留证据。
6. 不顺手修改UI统计33ms/状态250ms刷新、GPU默认优先级或引入持续刷新。菜单保留原交互，不用淡入淡出掩盖文字抖动。
7. 不发跨会话/邮件/群消息，不评论或关闭issue，不关机，除非当前用户明确要求。

## 产物、依赖与清理

- 全部新构建、下载、日志、测试、解压、临时和打包产物统一放`E:/项目/Veyra/{build,tests,logs,tmp,downloads,deps,test-packages,releases,verify,reports,archives,worktrees}`对应任务目录。源码、受控脚本/文档留Git；不要向桌面、C盘或系统Temp新增研发产物。
- 依赖实体`E:/项目/Veyra/deps/core`；Qt实体`E:/项目/Veyra/deps/qt/6.8.3/msvc2022_64`。`C:/veyra-deps`和`deps/core/qt-veyra`是兼容Junction，保留入口，不在C盘重建依赖。清单`E:/项目/Veyra/deps/DEPENDENCY_ROOTS.json`。
- 当前可复用构建`E:/项目/Veyra/build/claude-screen-recording-20261022`；2.1.1正式归档`E:/项目/Veyra/releases/release-2.1.1-final-20261027`。打包供体与必要源码依赖按开发指南核对。
- FFmpeg保持实际patched版本（PS5 H.264 slices256、dav1d、压缩PGS所需zlib），并保留实际对应源码、补丁、构建记录及许可证；不可退回未打补丁底包。
- 正式归档保留2.0.7及以后已发布资产和对应源码。一个任务共享一份运行目录，以data-dir/log隔离用例；结束后清掉重复运行/解压副本，保留最终交付和必要证据。
- 清理前核对绝对目标、父链reparse、当前进程、hash/文件身份和恢复清单；不把硬链接路径长度相加当实际回收。不修改历史guard/start来制造通过。
- 脏树、唯一脚本/配置/素材、SDK/模型原件、Git共用库和许可对应源码保护；归档分支须有tip映射和verify过的bundle。不要强制移除脏工作树，不复制整工程作日常备份。

## 产品与实现合同

- Windows x64、C++20、D3D12、Qt/QML主界面；直接NGX，不再接一套Streamline链路。播放、采集、图片/视频导出共享增强逻辑，不复制三套颜色/Guidance/reset。
- 采集卡/视频只有最终像素。估计光流、深度等不能宣传为游戏原生数据；要插A与B之间的帧必须等B到达，不能把驱动缓冲当免费未来帧。提交FPS不等于物理刷新率或屏幕端到端延迟。
- 默认处理顺序与允许的实时低延迟NR→SR路径按现有图执行；FG在效果后，字幕/OSD按现有叠加合同。导出、暂停单帧和图片不得按实时预览跳帧策略丢源帧。
- 输入显式解析range/matrix/transfer，统一工作纹理；输出只做一次明确转换。HDR代理增强与原生HDR模型推理分开表述。
- 历史型模块在seek、转场、PTS断点、capture drop、resize、源切换、暂停恢复和device lost时统一reset。保持有界队列/latest-frame mailbox；不放宽告警、伪造时间戳或静音来掩盖欠速。
- 正常GPU处理不加像素回读、逐pass CPU fence等待或无界队列。使用既有GPU slot/fence轮转和NVENC输入协议，诊断/兼容fallback必须明确标记。
- Guidance motion遵守current→previous、post-SR workingExtent像素单位；低置信度区域衰减，depth/confidence格式及NGX参数类型按既有合同，不凭名称猜。
- 保留RAII、模块/Feature/资源逆序释放和受限绝对路径DLL加载；HRESULT/NGX错误、尺寸/格式、GPU计时须进入日志。
- Smooth Motion由用户在NVIDIA App自行管理；不自动改驱动，不强制与内部补帧互斥，不把驱动生成帧计入软件录制/导出能力。

## 运行组件、来源与发布

- SDK、DLL、模型、头文件、LIB、抓帧、凭据、配置不进入源码Git、LFS、资源嵌入或base64代码。专有二进制不修改、不重签名、不伪装，不从未知来源补齐。
- 当前默认发行组件按2.1.1正式包manifest、`docs/BUILD_2.1.1.md`及其指向的固定来源核对；旧组件哈希在历史规则中。用户允许替换DLL，不用固定哈希/签名拒绝用户替换；发布者默认包仍逐文件核验身份与许可。
- ReShade/RenoDX add-on仅可作隔离对照，主线不加载、注入、链接或随包分发。Lecram研究暂停，不自动继续。
- 有成熟、许可证兼容的开源实现优先适配复用；在代码和THIRD_PARTY_NOTICES记录来源仓库、固定提交、许可证、修改。Veyra原有代码GPLv3；含Chiaki串流的组合程序同时遵守AGPLv3及OpenSSL例外（licenses/remoteplay）。无许可证项目不复制代码，厂商分发权不自行扩大。
- 发布授权按用户当前任务决定；本轮无push/Release。发布包只带已批准组件，manifest、许可证与对应源码完整；二维码区保留两图并排各width220，资产公开后核对真实访问和摘要。

## 交付与续接

- WORKLOG和本次执行记录写实际命令、结果、失败/修复、产物路径、空间新增/回收、已验与未验。
- 提交前检查diff、相关构建和回归，不以接口桩、成功counter、仅编译、同帧重复或理论FPS声称功能通过。
- 用户2026-10-20已确认此前NV显存增长解决、AMD实测无问题，不能把旧调查自动重列为当前缺陷；新改动的硬件验证边界仍按本次证据表述。
