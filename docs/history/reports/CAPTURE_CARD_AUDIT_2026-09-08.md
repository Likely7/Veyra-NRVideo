# 采集卡 4K18 / 1440p 能力核对（2026-09-08）

## 结论与修复

Veyra 原先仅保留1920×1080或3840×2160，且29≤fps≤61。这是应用过滤错误，明确隐藏了这张卡已支持的4K18，也会隐藏其他设备的1440p。现移除预设白名单，在现有3840×2160采集处理范围内展示所有有效尺寸及设备默认帧率，并标注YUY2/MJPEG/NV12或原始GUID。原始DirectShow索引不变，避免选择错格式；并补SetFormat HRESULT日志。没有修改导出完整性检查、设备驱动或固件。

这次不是只改列表：本机真实采集源与NR播放都执行成功。暂未添加每个分辨率的所有离散帧率选择器；默认帧率不等于该尺寸唯一可选帧率。4K本卡只有18这一档。

## 本机设备身份与链路

- USB视频名USB3 Video，USB音频名USB3 Digital Audio。
- VID345F / PID2131，REV3100，设备序列字符串20260128。该ID与MS2131方案一致，但尚未拆机核对芯片或确认成品品牌，不把通用ID当唯一商品型号。
- Microsoft UVC驱动10.0.26100.8972；驱动日期/固件版本不能直接由序列字符串推断。
- 只读查询实际父USB hub / port1：bcdUSB0320、bcdDevice3100；EX_V2 operatingSuperSpeed=1、operatingSuperSpeedPlus=0。当前是SuperSpeed连接，不是历史测试里的USB2。单看设备名称或根集线器名称不能证明连接速度；以EX_V2实际运行标志为证。
- 证据：logs/optimization-goal-20260908/capture-usb-link.txt。查询接口说明：[Microsoft USB连接信息](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbioctl/ni-usbioctl-ioctl_usb_get_node_connection_information_ex_v2)。没有发送刷写/配置固件命令。

## 驱动与USB描述符相互印证

直接读取本机USB配置描述符（1380字节），解析UVC VideoStreaming Frame Uncompressed(5)与Frame MJPEG(7)，两种格式各12个尺寸：1920×1080、1600×1200、1360×768、1280×1024、1280×960、1280×720、1024×768、800×600、720×576、720×480、640×480、3840×2160。

前11个尺寸离散帧率是约60/50/30/20/10；3840×2160仅18。没有2560×1440，也没有2048×1080。因此这里的“2K”即使分别按1440p或DCI2K理解，都未公布。

FFmpeg DirectShow完整枚举与上述结果一致；VideoInfo/VideoInfo2会显示重复行。本次产品枚举读出实卡48条原始媒体类型，4K18 YUY2索引22/23、MJPEG索引46/47。作为交叉检查，产品也读出OBS虚拟摄像头的2560×1440@60，证明不会再因尺寸名称而过滤1440p。

证据：capture-native-options.txt、capture-usb-config.bin、capture-usb-frames.json、capture-product-formats.txt，均在logs/optimization-goal-20260908下。FFmpeg的-list_options true以“Error opening input”结束是枚举命令退出行为，不代表已尝试持续采集。

## 为什么有4K18而没有2K30

带宽预算并不是一组可自动插值的分辨率菜单。HDMI输入/环出能力、芯片缩放输出、固件UVC帧尺寸与帧间隔表、USB传输能力是不同约束。本机固件没有1440p输出条目，普通应用不能把未公布档位写入菜单就当它受支持。

同为未压缩8-bit YUY2（每像素2字节，不含协议开销）：

| 模式 | 有效图像负载 |
| --- | ---: |
| 3840×2160×18 | 298.5984 MB/s |
| 2560×1440×30 | 221.184 MB/s |
| 3840×2160×30 | 497.664 MB/s |

所以“4K18有，2K30没有”不能解释为2K30单纯带宽太高。可证实的直接原因是当前固件模式表缺少该尺寸；为何厂商选择这张表、芯片/板卡能否经匹配固件增加1440p，尚无该成品型号的证据。

相关芯片研究：[Steve Markgraf/HSDAOH一手演讲](https://people.osmocom.org/steve-m/hsdaoh_slides/hsdaoh.html)报告MS2130约4K18/298.5MB/s能力；[ms-tools原项目](https://github.com/BertoldVdb/ms-tools)研究MacroSilicon芯片与固件。这些说明同类产品的输出与固件有关，不能据此保证本卡刷任意MS2130/2131固件可用。MacroSilicon公开MS2131页面本次获取失败（英文502、中文超时），不将二手产品摘要冒充成功核对官方结论。

现有可行选择：4K18真实采集；1080p较高帧率采集；若必须原生1440p30采集，则须确认成品厂商匹配固件/明确支持此UVC模式的硬件。4K18缩小到1440p仍只有18个源帧，插帧或1080p超分不能冒充原生1440p30。

## 实测与边界

- 构建 scripts/build.ps1 -Root <project> -Preset x64-release：第一次链接遇到运行中exe占用；正常关闭后重建exit0，build-cycle80-retry.log。
- veyra_capture_tests.exe --list：实卡4K18与OBS1440均可见。
- veyra_capture_tests.exe capture:0:22:-1 及capture:0:46:-1：各约1秒，实际配置3840×2160/18；SetFormat0x00000000；Run0x1；收到6帧。消费者故意停300ms期间发生4次mailbox覆盖是该测试预期行为，所有所有权/丢帧恢复检查通过。
- veyra.exe capture:0:22:-1 --realtime --nr --no-sr --no-fg --smoke-view professional --smoke-seconds 8：exit0，107NR/106NVOF；callback18.02fps，captureDropped0，failed=false；NR Create0x1/SEH0。callbackToPresentReturnP9516.864ms不是HDMI到屏幕端到端延迟。日志capture-4k18-app.log。
- 此处3840×2160指USB输出及程序收到的尺寸；未确认HDMI源本身是否原生4K，不能以该数值保证没有卡内缩放。
- 随后FFmpeg单帧图像探测Run失败；同时用户已启动PID20272 / capture:0:22:0，该失败发生于并发占用期间，不能单独诊断能力，也不能证明占用是唯一原因。未取得独立静帧视觉证据。
- 用户已在更新版自行恢复4K18采集，保留其运行。专业鼠标操作、4K18声音/主观流畅度仍由用户实测。Phase7整体状态仍未通过；本修复不声明解决既有4K+FG时序问题。

独立review_capture_modes scopedPASS：重新枚举exit0，离线解析原始USB描述符完全一致，核对采集/NR实测日志，无新增P0/P1/P2；未干扰用户采集。最终程序SHA256：2587E014D349402254C23C8B82108539B4C586E2C4C1D2C558BC636EB4AB34EB。preflight71PASS；整体Phase7状态不变。
