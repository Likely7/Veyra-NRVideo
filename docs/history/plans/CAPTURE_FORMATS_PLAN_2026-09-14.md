# 采集格式扩展与显示修复

用户要求补齐P010、RGB24及其他常见未支持格式。本轮本地修复，不发布、不改变Smooth Motion或驱动设置。基线236228c，分支codex/capture-formats-20260914，存档checkpoint/capture-formats-20260914。

## 实现

- 统一格式名称/原生接入表：RGB24/32、ARGB32、RGB555/565、YUY2/UYVY/YVYU、NV12/NV21、I420/IYUV/YV12、P010/P016。
- 保留驱动索引；GUID不冒充乱码，已知格式显示名称。原生支持、需系统解码/转换、布局不支持分别说明。
- 直接接入有界mailbox；packed RGB/YUV仅重排字节，不经过系统颜色转换或降为4:2:0；平面格式正确处理stride、UV顺序、短包和上下翻转。
- P010/P016以16位纹理进入共享图，位深与HDR标记分离，SDR不经8位中转。现有PS5 HDR契约不改；采集HDR元数据不完整时不猜PQ，默认SDR需在列表说明，明确不支持的颜色信息拒绝。此任务不声称采集HDR已完成。
- 压缩MJPEG及系统解码回退保留，私有/调色板/复杂10位422等未实现格式不宣称原生支持。

## 验证

构建产品；合成样本验证逐字节重排、边界、plane offset、未知GUID回退及动态格式拒绝；GPU验证RGB/YUV/P010/P016范围及10位阶梯，复核既有PS5 HDR回归。单次测试上限300秒。反馈者Live Gamer Ultra 2.1不在本机，真实设备仍由反馈者验收。结果和命令在本文件与WORKLOG补录。

## 已完成与证据

原生格式共15项（含原有三项）：RGB24/RGB32/ARGB32/RGB555/RGB565、YUY2/UYVY/YVYU、NV12/NV21、I420/IYUV/YV12、P010/P016。统一CapturePixelFormat名称与packing，CaptureMediaType解析/复制，CaptureCardSource直接连接；RGB/YUV422重排为现有BGR0/YUY2内存合同，未颜色降采样。EnhanceGraphDesc.captureBitDepth与hdrInput分离，P010/P016在R16/R16G16纹理转换为FP16工作颜色；P016输入不是承诺16位最终显示，SDR最终显示仍为现有8位输出。

P010/P016列表明确为SDR，缺少色彩元数据沿用有日志的SDR假设；已知但无法识别的颜色元数据拒绝，不隐式转RGB32掩盖。采集HDR、私有格式、调色板、Y210/P210等复杂格式未宣称原生支持；MJPEG和未知格式的系统解码/转换仍取决于系统filter。文件与PS5的原有HDR边界未全局修改。

修改文件：include/veyra/source/CapturePixelFormat.h、CaptureMediaType.h，src/source/CaptureCardSource.cpp；include/veyra/pipeline/{FramePacket,ColorMetadata,EnhanceGraph}.h、src/pipeline/EnhanceGraph.cpp、src/engine/EngineController.cpp、shaders/YuvToLinearRgb.hlsl；相应CaptureFormatCases、CaptureColorContractTests、CaptureFormatGpuCases、HdrColorTests、CaptureSourceTests及capture_color_probe。诊断工具按实际plane/rowBytes导出，避免高位深样本被旧的4字节假设越界读取。测试专用FP16只读访问器无正常播放readback。

- `cmd.exe /c out\gpu-dis-build-all.cmd`：首次159步构建遇到运行中veyra.exe占用导致LNK1104；用户关闭后完整62步重建通过，最后诊断工具4步增量通过。另有测试头缺少iostream导致编译失败，补include后通过；记录build.log/build2.log/build3.log/build-final.log。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-short-test.ps1 -Exe out/remoteplay/product-repair/veyra_capture_color_tests.exe -Arguments contract -TimeoutSeconds 30 -LogPrefix logs/capture-formats-20260914/layout`：127 PASS，0 failures。首次未传Arguments触发现有runner空ArgumentList错误，未执行测试；本次传入测试不使用的contract参数，未修改runner。
- `veyra_hdr_color_tests.exe`（Start-Process Hidden，WaitForExit上限120秒，无参数）：30组采集GPU范围/精度检查和8组既有HDR检查全部通过，耗时约1.4秒。gpu2.stdout.log。首次测试误拿8位呈现资源当FP16资源，测试拒绝格式；改为读取真实FP16 ingress纹理、显式NON_PIXEL_SHADER_RESOURCE往返barrier后通过，保留gpu.stdout.log失败记录，未放宽精度断言。
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair`：23/23 PASS，56.2209961秒；logs/delivery/544cd7222e404abcb7a760a63f1c546c/result.json。真实RTX5070执行NR Feature18 Create=0x1、handle非空、seh=0，原生4K NR Evaluate=12；播放器、控制、H.264/HEVC导出和取消回归通过。
- 真实本机USB3 Video：`veyra_capture_tests.exe --list`枚举成功；`capture:0:0:-1`原生YUY2 1080p60 SetFormat/ConnectDirect=0，Run=0x800705AA，exit4，未通过。现场OBS正在运行，但没有证明其占用了设备，不断言根因。未复测反馈者Live Gamer Ultra 2.1，不把合成测试当实卡成功。旧probe控制台编码会损失中文，GUI使用wstring并无该转码；名称映射已由单元测试核对。

所有日志位于logs/capture-formats-20260914。桌面快捷方式所指out/remoteplay/product-repair/veyra.exe已更新，版本资源仍为1.0.2，EXE SHA256 B62CA082176CE02D690D302236BEBC86ABD557CA18243CDD2000E21645A49BCC。avcodec仍为0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F。未改运行DLL、未push、未修改GitHub 1.0.2资产。下一步反馈者以P010 SDR/RGB24验证实卡。

布局依据：[Microsoft RGB subtypes](https://learn.microsoft.com/en-us/windows/win32/directshow/uncompressed-rgb-video-subtypes)、[10/16-bit YUV](https://learn.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats)、[8-bit YUV](https://learn.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering)。独立实现，未复制第三方项目代码。
