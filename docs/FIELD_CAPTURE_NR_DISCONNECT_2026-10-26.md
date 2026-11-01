# 外部用户开启NR后采集断连诊断

用户提供logs(20).zip，描述60帧采集开NR约一分钟断开、不启用则正常。本轮仅诊断，不改产品、不运行采集、不改用户配置；为符号化旧dump，单独编译诊断工具并重链接保留的旧对象文件；2.1.1封包仍暂停。

## 结论

日志确认有真实采集停流/重连失败：音视频同时不再进展，音频恢复重启图返回0x800701B1（Win32 433 / ERROR_NO_SUCH_DEVICE），另有DirectShow EC_DEVICE_LOST通知。未发现NR Evaluate错误、D3D12设备移除或显存耗尽的证据。可确认采集回调完成/音频进展停止及后续恢复失败；不能由回调完成计数冻结推出驱动没有调用回调，回调可能已进入后阻塞在软件或GPU接口中。**尚不能判定最初停流由NR、光流、GPU负载、USB/驱动中的哪项触发，不能将“采集卡连接不稳定”作为结论。**

NR开启确实与多次停流时间相邻，不能据此排除NR关联；但有NR关闭、SR+NVOF开启时同类停流的反例。因此“当前必须开NR才会断”不成立；反例同一进程此前开过NR，不能进一步声称从未运行过NR也一定会出现。

## 设备与日志范围

- RTX3080，专用显存约10GB；NR community-SF-RTX20-RTX50 / preset2 / performance=true，内部1920×1080。
- 音频设备明确为MCS T911-HDMI (MCS 4K-T911)，视频USB VID_0BDA/PID_0911。不能与上一包RTX5090D的YUY2约53fps问题混为一谈。
- 包内三份日志128209行，覆盖10月25/26日多会话。另有10月23日dump，不与本次日志时间重合。
- 既有4K60 NV12/YUY2，也有1080p请求165fps而回调/样本PTS约240次/秒。回调次数不是不同内容画面数量，也不是实测HDMI源信号帧率。不得把全包当成一个固定60fps条件。

## 关键时间线（原始UTC）

### 4K60 NV12、NR+光流

veyra-qml.old.log：16:24:01.310图初始化sr=0/nr=1/fg=0/nvof=1；16:26:24.838仍收到60fps回调，实际原帧处理47fps；之后音视频计数停止。16:26:35.446出现PCM stalled，重启Run返回0x800701B1。

这轮是启用NR后约2分24秒停止视频进展，约2分34秒打印音频恢复，不是精确60秒触发。另有数秒就停、几分钟后停的会话，未找到“一分钟定时主动断开”的证据。

| 同次会话稳态窗口 | 回调FPS中位数 | 原帧完成FPS中位数 | NR阶段P95读数中位数 | 显存MiB |
|---|---:|---:|---:|---:|
| 16:22:00–16:23:50，效果全关 | 60.00 | 60 | 0ms | 1958 |
| 16:24:10–16:26:20，NR+NVOF | 60.00 | 47 | 13.954ms | 2437 |

NR+光流时GPU ready P95读数中位数20.858ms，处理侧确实跟不上60fps；但采集回调仍为60，性能不足导致的预览跳帧与后续设备停流是两件事。各项P95不可简单相加当作端到端延迟。显存预算9285MiB，现有读数不支持“爆显存导致断连”。

### 最接近“开NR一分钟后断”的一轮

veyra-qml.log 25567行：12:47:54.309开启NR+NVOF。26148行：12:48:38.665最后一条处理时序，回调240.01、完成56fps、NR P95 14.392ms。随后音频inputBlocks停在64428，视频PTS也停止。26178行：12:48:49.106 PCM stalled；26216行：Run=0x800701B1；26218行：控制调度调用耗时7359.955ms；26222行开始整图恢复，直到13:21:16.214才记录重新连上。

该轮视频停止进展约在NR启用44秒后，恢复报错在约55秒后，但其输入为1920×1080，协商165、回调240，不能拿它作为“固定60fps一分钟断”的严格复现。

### NR关闭时的同类反例

28085行：13:21:30.747图sr=1/nr=0/fg=0/nvof=1。30014行：13:23:47.526回调240.01，gpuNrP95=0、nr=0，SR约6.780ms、Flow约2.841ms；然后音视频停流。30043行：13:23:58.062 PCM stalled；30081行：Run=0x800701B1；30083行控制调度耗时7353.674ms。该轮NR没有正在执行，但同进程先前开过NR。

13:24:24重连后，效果全关至14:11退出前连续约47分钟回调约240、完成约240，未再出现音频停流恢复。NR+NVOF、SR+NVOF都曾停流；全关有长稳态，这支持进一步分离增强负载与共享光流的影响，不足以直接宣判NVOF或USB为根因。

## 软件侧可确认的问题与证据缺口

当前对应源码EngineController在控制/调度线程同步调用CaptureCardSource::recoverAudio；AudioInputRecovery观察PCM包计数3秒无进展才触发，不是看音量大小。recoverAudio同步Stop共享音视频图、移除/重接音频、再Run。因此日志PCM stalled是在Stop返回后打印，不能把它的打印时刻当作首次停流时刻，也不能说恢复动作造成了此前已发生的停流。

本例控制阶段多次约7.35秒阻塞，与该同步恢复流程一致；恢复流程明显放大了用户看到的卡住。恢复后NO_SUCH_DEVICE说明当时某个图中设备过滤器已不可用，不能仅由该错误判定物理拔线/供电/线材问题。

EC_DEVICE_LOST=31表示设备移除或重新可用。当前日志只记录lParam1的过滤器IUnknown指针，漏记lParam2，无法区分两个方向；detail=0x…不是GPU错误码或HRESULT。Microsoft要求设备恢复可用后重建旧过滤器图，保留旧videoFilter的音频局部重建在设备已失效时不能完成恢复。[Microsoft事件定义](https://learn.microsoft.com/en-us/windows/win32/directshow/ec-device-lost)。错误433另由本机Windows SDK winerror.h核对为ERROR_NO_SUCH_DEVICE。

三日志合计26次PCM停流恢复、25次Run=0x800701B1、17条device-lost通知（非17次独立物理拔插）；4条ERROR均为旧会话剪贴板失败。没有NR失败或GPU设备移除日志。Windows USB/Kernel-PnP/驱动事件及同条件外部应用对照未提供，不能继续细分初始触发原因。

## 首次停流的定位缺口与下一步

当前SampleCB的capture-callback日志在复制和发布完成后才打印，received也在回调后半段增加。缺少独立于该锁的入口/退出序号和正在执行的阶段。因此完成计数冻结至少有两种解释：上游没有再次调用；或上游已调用但回调阻塞在GetTime/GetPointer、图时钟、帧互斥锁、GPU fence查询或内存复制中。现有记录无法区分。不能用后续Run错误倒推首次停流由物理断连触发。

静态代码发现SampleCB → writeCaptureFrame → selectCaptureUploadTarget → CaptureUploadFrame::gpuBusy会在采集交付线程调用ID3D12Fence::GetCompletedValue，且查询时持有帧内部mutex。这是需要测量的候选耦合点，尚无转储或时序证据证明它发生阻塞，不能宣判GPU查询为根因。

下一轮应优先制作一次复现即可定位的定点诊断：独立原子入口/出口/阶段心跳，在首次停流而尚未Stop/Run前保存线程快照，记录原始DirectShow事件的两个参数和对应过滤器身份，并收集受影响机器同时间USB/PnP事件。日志记录Stop/Run调用前后及耗时。该方案尚未实施，也不要求用户先轮流尝试多个开关。本轮未修改产品代码。

## Dump与复算

dump头时间2026-10-23T13:21:47Z，212模块、55线程、无ExceptionStream；模块路径为F:/dlss5/Veyra-2.1.0-test-NVIDIA-win64-portable。该dump早于包内10月25/26日日志，无同时间日志可以对齐，不能证明最新停流现场。

已用系统DbgEng编译只读展开工具，匹配dump中Windows PE时间戳/SizeOfImage并从Microsoft符号服务器取得对应公开PDB。应用不含PDB，但其PE身份与正式2.1.0 NVIDIA donor相符。用保留的bilibili-2.1.0-20261019对象/库在E:/项目/Veyra/tmp/field-capture-nr-disconnect-20261026单独重链接生成MAP；不运行生成EXE、不改旧build。重链接后的完整.text段与匹配原EXE逐字节一致，SHA256均为b48bcd462af9d82aeee97b919fd90d5c033d38ea04dd7408edf09db56c35b71c，才能用于函数归属；其余资源/时间戳不要求相同。

展开结果：GUI线程位于Qt事件循环，音频播放线程位于CaptureAudioSession::Impl::run → AudioRenderer::waitForEvent；播放列表/字幕工作线程等待条件变量，采集相关ksproxy/quartz线程处于事件等待。该瞬间未取得首次停流的SampleCB阻塞栈。NVIDIA私有模块缺少匹配镜像，部分栈不能可靠完整展开，不能据此排除GPU侧问题，也不能拿普通等待栈证明死锁。旧dump没有为最新停流提供可归因证据。

证据：reports/field-capture-nr-disconnect-20261026/map-text-verification.json、dump-stacks-mapped.log、matching-images.json、symbols-download.json。诊断工具编译首次失败（类名冲突/缺头文件），修正后通过；首次重链接缺MANIFEST:EMBED失败，补齐后成功，相关命令及日志全部留在该tmp/reports目录。产品未构建/运行/测试，不能将诊断工具通过计作产品验收。

ZIP SHA256：6ce6753f1b3d110da61998ba019410bda89c96dc0c29dcf03e2b929e25041f41。副本及提取logs/dmp在E:/项目/Veyra/archives/field-capture-nr-disconnect-20261026，限制路径/解压大小，不执行内容。

复算：python -B E:/项目/Veyra/reports/field-capture-nr-disconnect-20261026/analyze.py，已成功；analysis.json含按窗口统计/26次事件的前序状态/dump元数据，evidence.txt有原始行号。此次没有产品代码修改、当前产品构建或实卡复现，不能宣称已定位根因或修复。

## 用户补充：同一软件包此前正常

用户确认：前几天正常，最近两天异常，一直使用同一个软件包。这降低了“最近源码变更直接引入本次现象”的嫌疑，不排除既有软件缺陷被新配置/输入/运行状态触发，也不证明硬件损坏。尚未核验其EXE和可替换运行库哈希是否完全未变。故障包内25/26日日志的D3D12设备初始化记录只有驱动32.0.16.2101，缺少此前正常时期驱动、采集配置及环境证据，不能声称驱动近期更新。后续重点对比正常与故障时实际输入格式/帧率、效果设置、驱动/系统及设备事件。
