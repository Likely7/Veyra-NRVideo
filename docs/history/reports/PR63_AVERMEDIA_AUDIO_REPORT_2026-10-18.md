# PR63 圆刚 5.1 音频适配验收（2026-10-18）

PR63 head `4db24dada13748364d7e613bc4160224b330a7d2` 已保留原提交三方整合；验收代码为 `3a77635ff374a93271da0fd0d7a9fa2078ba8bd2`。GitHub PR63 已合并（`642f6087c34accee82d4e8bad563eb3e5e2732e6`）；与本机已验树核对一致，整合提交 `8463a60b034c322cb140b176c098e896ebd5812e` 及本报告按本轮授权正常同步至 main。无新 Release／新包／代发评论／关机操作。

## 最终行为

- 自动音频入口仅对所选圆刚设备使用本机已安装的 AVerMedia Multichannel Audio 图，USB 实例优先、名称必须唯一；拒绝同名其他卡和不明音频源。普通采集卡、麦克风、WASAPI、禁用音频均保留原路由；PCM-only 不开启厂商 non-PCM 路径，接收器透传偏好先沿用原 DirectShow 选择。
- `AudioDevice` 只建立图，不负责完整 non-PCM 切换。对照厂商固定公开提交 `7d1250b42e42f964b4fa3a6bcb7269cc09a1e798` 的头文件及 Activate 顺序，在确认厂商枚举的确切设备后，在 STA 上调用已有 DeviceOpener 开关/监测，再 Reset→Update→Connect→SetCallback→Start。保留 PR 核心 5.1 路径，避免只建立图却一直收到立体声。
- 按公开 12 字节元数据按值回调处理，删除私有 `core+0x18` 访问；日志按 va_list 格式化，外部调用保护 SEH/C++ 异常。启动只等待初始结果最多 150ms，慢 SDK 在独立 STA 继续，状态区分 starting、running 与 failed；视频 Run 成功且已有音量/同步设置之后才向会话交付样本。
- PCM 使用回调实际采样率/位深；连续检测小端 IEC61937，真实解码 AC-3/E-AC-3 后按解码器声道布局配置原会话，支持 6→2→6、44.1/48kHz 与 PCM/杜比反复切换。声道不硬猜成固定 5.1；编码器的 back/side mask 不能直接代表解码器输出，本机独立 FFmpeg 参考解码得到 AC-3/E-AC-3 的 0x60F。
- 初始 PCM 立即交付；不积压/重放四秒 PCM。压缩返回 PCM 仍保留四秒无载波保护窗口，仅交付最新不超过 20ms，并保留碎片样本对齐。压缩数据不直通成 PCM 噪声；载波缓存上限 131072 字节，64 个突发无解码输出触发恢复。
- 回调消失默认 6 秒监测、最多 3 次重建；重试耗尽由采集源回退到原先选中的 DirectShow 输入。修复原提前 return 绕过恢复、会话声道不重配、异步失败不可见、跨线程 bool、早退 DLL 目录残留和回调退出前释放会话的问题。另补共享 BitstreamDecoder 布局/重采样历史 reset、输入 padding 和解析器无进展保护。

> PR64后续更正：上一轮调用DeviceOpener切换/监测不等于已确认底层non-PCM flag开启。PR64补显式查询、条件写入和回读；详见[PR64验收](PR64_AVERMEDIA_REPORT_2026-10-19.md)。本机一直没有圆刚实卡，模拟流程通过不能替代现场恢复证据。

## 本机验证

最终 MSVC Release 构建 14 个目标通过，含完整 QML 应用、音频/采集测试和两个自写 ABI 测试 DLL；RemotePlay/Moonlight/Xbox 保持启用。构建 -j2，最终增量 27 秒；源码输入 763 文件核对，1374 个共用运行文件哈希保持；测试 DLL 不进入 app staging，更不进入源码二进制或发布包。最终构建收据：

`E:\项目\Veyra\logs\pr63-avermedia-audio-20261018\build-20261018-201338\receipt.json`

12 组定向回归全部通过：真实 AC-3/E-AC-3 编解码与分片载波、独立参考布局/采样率、PCM16/24 转换及格式切换、坏载波/缓存边界；厂商 ABI 65 条检查和缺失导出 7 条检查；现有圆刚开关、IEC 探测、双声道/5.1 真 WASAPI（静音）会话、音频 DSP、采集颜色、精确格式、能力枚举。ABI 测试使用 80/100ms 的短监测参数验证恢复逻辑，不冒充真实驱动 6 秒计时实卡测试。

`E:\项目\Veyra\logs\pr63-avermedia-audio-20261018\checks-20261018-201448\receipt.json`

本机实卡为 MCS 4K-T911，未发现圆刚卡。最终 NV12 1080p60 两轮含重连的物理回调分别为 60.049599 / 60.050652 FPS，PTS 单调、无软件限帧；原 DirectShow HDMI 音频 48kHz/16bit、63 块输入、0 underruns，缓冲所有权/过期帧丢弃/停启验证通过。它证明普通卡回归，不能替代圆刚 5.1 通过。

`E:\项目\Veyra\logs\pr63-avermedia-audio-20261018\card-20261018-201630\final.json`

## 失败记录与边界

- 首次完整构建暴露旧 CaptureAudioTests 缺 FFmpeg include 目录，已补 CMake 测试目标包含路径。初版自写测试夹具把 .def 引号当成导出名，以及未接受 Windows 的 MAINSTA；均按真实 PE 导出和 COM 语义修正，并重跑通过。初版编码布局断言错误地要求编码前 Windows mask 原样保留，改为独立 FFmpeg 解码参考，未为过测试修改生产声道数或门槛。
- 首轮双声道模拟 80/160ms 视频延迟同步读数未过；合并前主线独立编译/链接基线通过，最终双声道与 5.1 复测均通过。CaptureAudioSession、WasapiAudioSink、CaptureAudioDsp 原源文件哈希与基线完全一致；首次失败日志仍保留，不能据此声称偶发音频同步抖动已根治。基线收据见 reports/pr63-avermedia-audio-20261018/baseline.json。
- 本机没有圆刚实卡/官方组件。作者报告 GC553G2+PS5 Dolby 6ch 通过，属于作者证据；GC553PRO、GC575、其他驱动/组件版本、真实功放 5.1 声道走向、Dolby↔PCM 实卡切换和长期热插拔仍待持卡者验证。DTS 仅逻辑支持未做本机真实编码/实卡验收；MAT/TrueHD/DTS-HD 载波在此入口明确不支持，静音报错，不宣称 Atmos 支持。
- 150ms 是启动初始等待，不是厂商 Start/Stop 的硬超时。外部同步 SDK 调用无法安全强制取消；真实驱动若不返回，退出仍可能等待。若 Stop 返回失败，则保留该单个图/模块和回调宿主至进程退出，禁止再次复用，防止卸载后回调访问已释放内存；这是故障隔离的有意保留，不能宣称所有错误关闭路径零资源残留。
- 未改变 QML/apps、UI 33ms/250ms、引擎/视频处理图、Shader、NR/超分/补帧实现；本轮未追加 UI/GPU 性能收益或屏幕端到端延迟结论。

## 隔离与存档

开工存档 90 工作树、103 分支与全部状态/哈希；按 start.json 统计：桌面466个脏路径、其他四个旧工作树43个、Claude美颜21个，合计530个；有既有ZIP对照及逐路径哈希。更正上一版“桌面509”的误标：509是桌面与另外四个旧树的合计，不是桌面单独数量。施工期间 Claude 美颜分支继续产生改动，快照据实记录，未写入该目录；原桌面466个脏路径与其他旧分支保持。AMD 0.44／全屏修复分支保留独立，未夹带本 PR。

档案：`E:/项目/Veyra/archives/pr63-avermedia-audio-20261018`；固定厂商公开参考：`E:/项目/Veyra/deps/pr63-avermedia-audio-20261018/reference`；本机测试程序：`E:/项目/Veyra/tests/pr63-avermedia-audio-20261018/app/veyra_qml_ui.exe`。源码不含厂商 DLL/SDK/模型，未修改本机厂商文件、驱动或用户配置；不自动下载/分发组件。第三方来源及改动在 THIRD_PARTY_NOTICES.md。最后审查收据：`E:/项目/Veyra/reports/pr63-avermedia-audio-20261018/verification.json`。
