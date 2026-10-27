# 采集音频卡画面排查与近期功能整合

本轮仅整合本地 main，正式发布仍为2.1.0；未推送、未发布。施工从207555b开始，#67以merge提交保留6199212原历史；其余最近功能均保留原提交。准确main/source/package身份见 `E:/项目/Veyra/reports/capture-audio-main-20261025/` 的 integration-audit.json、update-package.json 与 FINAL.json。主线入口仍为此 E盘施工树，旧桌面及其它工作树不覆盖。

## CV710 日志与修复边界

RAR内只有veyra-qml.log，原件SHA256为809F6301DFD05977A3E8668728897A3BBF2AC0D05B967DECE770A3F48B834FCD，存archives/capture-audio-main-20261025。日志为3080Ti、AVerMedia U3/CV710、1080p59.94 YUY2。关监听时约60fps；启用同设备内置PCM48k/16bit/stereo后，音频持续299块，视频接收计数为0。默认/系统时钟轮换三次均不能恢复。音频首个无时间戳样本还造成host/流时钟切换和reset；这不证明它阻塞视频，也不能把平均输入不稳认定为原因。

确认源码缺陷：两个原生视频投递回调把GetTime的VFW_E_SAMPLE_TIME_NOT_SET直接作为失败返回给生产者。未设置时间戳是合法样本状态，流生产者可能在Receive失败后停止后续视频。现在只对这个明确状态返回S_OK并跳过，不生成帧、不补造PTS；真正错误继续返回。有效start但无stop仍保留真实start。加首3个视频Receive和限频的缺失/恢复日志，区分驱动从未调用与样本在处理前被拒绝。

这是已复现的接收合同错误修复，**不是已证实CV710现场根因/原卡验收通过**。没有CV710/3080Ti原机；下一次原卡日志的capture-video-delivery/capture-video-time可验证这一假设。若仍无视频投递，不继续盲猜时钟或强行修改驱动/缓冲。没有改10ms音频协商、驱动、运行库或模型。

依据：[Microsoft IMediaSample::GetTime](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-imediasample-gettime) 区分VFW_E_SAMPLE_TIME_NOT_SET和VFW_S_NO_STOP_TIME；[libdshowcapture接收路径](https://github.com/obsproject/libdshowcapture/blob/master/source/device.cpp) 对缺时戳原始数据不送用户回调。这是契约参考，未复制第三方实现。

## 全屏退出补丁的追加纠偏

完整存档原main-merge-20261002的未提交补丁、7个文件及哈希，原树保持不动。顶部还原在全屏调用既有toggleFullscreen，保留进入前的窗口/最大化状态。节点重复Esc合并，隐藏页不抢键，专业保护绘制与全屏Esc互斥。

原待合补丁不完整：仅凭playlistWindow.active禁用主Esc；真实测试证实，隐藏的播放列表仍可能active=true。只补visible仍不足以正确表达工具窗口焦点。最终将播放列表Esc/Delete/Return放到它自己的FocusScope/Keys路径，ShortcutOverride防止主Esc抢键；主全屏Esc统一调度。这同时覆盖隐藏、显示但失焦、显示且获得焦点三种状态。

## 已整合范围

- Claude接手的录制生命周期/声音、AMD超分关闭修复。
- 极简/专业共用90°画面旋转和随画面调整的呈现/坐标。
- 每层NR手动25%–100%，保存/复制/节点/预设兼容。
- fg1后台VRR查询，保留预算/告警/真实PTS。
- 视频NR导出固定档与手动百分比，最终输出尺寸不变、图片完整处理、AMD预算保留。
- #67进程级System32 NVAPI保留与成对关闭日志。
- 本轮采集无时戳预备样本修复与上述全屏/Esc修复。

Lecram仍暂停；旧FSR4/NVIDIA实验等历史分支不混入。清理/依赖迁移文档随原提交保留，不再次删除历史目录。

## 验证与局限

复用build/claude-screen-recording-20261022，脚本build-recording-handoff.py，标签2.1.0-int1，-j2，每次构建<=900秒；所有测试进程<=300秒。9个目标全部通过：采集颜色/原生接收285项（真实DirectShow allocator/IMediaSample复现缺时戳HRESULT、后续有时戳恢复、真错误仍传播）、NVAPI并发/模块固定、Repair272、UI384布局、EffectChain1008组合、异步显示查询、NR模型全76百分比与存储、真实WASAPI音频、QtQuick115项。

真实引擎运行：SF性能/旧模型/原版切换均实际推理并保存图；NR开启状态停止157.685ms，同进程再次打开并NR开启停止通过，NGX Shutdown1成对成功。不冒充原3090/616.92复现通过。整合后再次真实worker导出4K/37%+NVOF，NR创建1420x798，HEVC输出3840x2160共6帧，ffprobe完整解码。

本机KUHAIMI G50显式请求2560x1440p60 NV12，独立HDMI音频48k短接收检查通过：configure不启动、Run后真实帧/52块音频、读者持帧时生产者继续、邮箱有界/Drop重置。此测试只有约1.2秒，含驱动启动期，打印callbackFps10.003；**不能用它宣称稳态60fps或CV710内置音频已恢复**。之前fg1的39秒2K60补帧A/B证据仍是之前提交的证据。

computer-use原生输入：软件UI专业F11/Esc、顶部还原、极简退出；GPU D3D11 UI F11/Esc；明确确认切换到实际节点模式，再测Ctrl+L锁定/Esc、窗口最大化→全屏→Esc恢复。暂停退出画面保留。日志native-software-final/native-gpu-final/native-gpu-node。播放列表三态按键由真实QML窗口、实际焦点QWindow事件测试覆盖。没有做全保护绘制/连线组合、真实CV710、HDR/长期或屏幕光学延迟验收，不承诺绝无bug。

失败保留：首次命令误写测试目标nr_model_policy，Ninja未开始编译，修正为已有nr_model_settings；初始全屏夹具把QQuickView传给QML Window类型失败，后续QTest按键和active标志不能代表真实焦点，反复失败真实保留。最终夹具使用独立QML Window、等待QGuiApplication::focusWindow、给实际焦点窗口送Qt事件；Windows平台115通过。offscreen不能用来证明这类原生激活/键归属。新增测试辅助类不进入产品二进制。运行库177项SHA保持，未复制凭据/真实配置。

命令：`python -X utf8 -B -I scripts/acceptance/capture-audio-main.py prepare|contracts|quick|gpu`，native launch用独立mode；构建/失败/运行原日志均在本任务logs。最终核对100个其它工作树的HEAD/status、全屏原脏文件哈希及旧refs，main用正常快进合并；保留隔离分支和检查点，不force/push/Release。
