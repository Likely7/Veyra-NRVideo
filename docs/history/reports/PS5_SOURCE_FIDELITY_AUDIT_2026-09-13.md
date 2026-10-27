# PS5 模糊：客户端处理与主机输出边界

基线 `082db52`，分支 `codex/ps5-source-quality`。用户明确要求不再依赖 Agent 看图，检查软件内部与主机限制，清晰度由用户肉眼测试。本轮新增诊断，不修改产品像素算法、主机参数或运行时，不发布。

## 当前判断

不能把严重模糊全归咎于索尼，也没有证据把它归咎于“Veyra把100Mbps限成15Mbps”。已确认输入是压缩的1080p / 4:2:0；本地普通显示有算法质量差异，但尚未证明这能解释用户反馈的全部模糊。流量低、解码成功、能识别人物，都不是画质通过标准。

| 环节 | 核查结果 | 能说明什么 / 不能说明什么 |
| --- | --- | --- |
| 码率请求 | UI100000Kbps原样进入Chiaki launchspec，先前实机反馈97087000bit/s；15000请求反馈14563000 | 请求已传递，未发现15Mbps夹限；不证明实际码率、量化精度或视觉清晰 |
| 输入压缩 | 实机H.264/H.265，解码尺寸1920×1080，SDR硬解NV12 | 亮度1920×1080，Cb/Cr各960×540；增加Mbps不会自动变成4:4:4或原生4K |
| 自动降分辨率 | Veyra `video_profile_auto_downgrade=false`，但上游launchspec仍为 `adaptiveStreamMode=resize` | 不能宣称关闭了主机全部自适应；先前三轮实际尺寸均1080p，未观察到降尺寸 |
| 解码 | RemotePlaySource未设置lowres、跳过去块滤波或FAST画质降级；修复过的avcodec哈希仍正确 | 不存在已发现的主动低质量解码开关；本轮没有重连PS5验证其所有码流 |
| 共享硬解导入 | 本轮普通H.264文件四帧软硬解误差0，并发黑白纹理错误0 | 排除该测试中的硬解导入损伤；不能用普通文件代替完整PS5实机回归 |
| 关闭增强的内部尺寸 | ResolutionPlan的base/output保留源尺寸；1080p不会因实时NR策略变720p | 未发现固定偷偷降分辨率；不能排除尚未覆盖的运行时切换故障 |
| SDR灰阶转换 | 新1080p单像素横/竖细线和合法范围灰阶测试通过，灰阶最大误差0.584/255 | 没有发现该契约下range重复展开、全图低通或亮度精度异常；不证明主机所有HDR/SDR信号意图正确 |
| 1:1最终呈现 | NV12和YUV420P输入，两种交换链，呈现缓冲与图输出逐像素误差0 | 不存在本次正常1:1路径的额外模糊；检查止于交换链缓冲，不包括DWM、显示器或UI动画 |
| 放大显示 | 1080p→1440p/2160p与CPU双线性参考最大误差0.547/255 | 实际就是一次双线性，不是意外多次缩放；双线性自身会柔化细线 |

## 确认的本地质量差异

### 1. 普通缩放较简单

`shaders/PresentBlit.hlsl` 使用linearClamp，`ScaleBlit.hlsl`异尺寸也是双线性。未开启AI超分，1080p仍需缩放到较大的视频窗口；这一步本来就会混合邻居像素。它和DLSS/RTX Video SR是两回事。

本机固定Chiaki源码 `gui/src/qmlmainwindow.cpp` 使用libplacebo的fast/default/high-quality参数；上游当前libplacebo默认放大采用Lanczos，高质量为EWA LanczosSharp，还包含不同的色度重建、色调映射和抖动选项。不能把“都没开NR/SR”当作渲染器完全相同，也不能据算法名承诺视觉胜负。在线libplacebo不是本机正在运行chiaki二进制的版本证明。

这是本地可改进的显示环节，不是修复PS5编码器，也不能恢复源头已经被压掉的细节。

### 2. 色度采样位置未纳入契约

`YuvToLinearRgb.hlsl` 直接读取 `chromaPlane[x/2,y/2]`，将一个色度值复制到2×2亮度像素；`ColorDescription`/解析未携带 `AVFrame::chroma_location`。这是已确认的重建能力缺口，可能导致彩色边缘块状、相位偏差，不能直接断言它造成了全画面严重模糊或发灰。

后续若修，应显式传递色度位置，再按位置重建Cb/Cr，覆盖NV12/P010/planar、软硬解一致性；不能直接加锐化或不分位置地糊一遍色度。保留源亮度像素，不把色度插值扩成亮度降噪。必须先独立shader色值测试，再让用户验收。

## 主机侧已知与未知

实测只证明主机给了1080p有损压缩输入及不同目标码率反馈。当前请求协议只有已验证的分辨率/帧率/codec/HDR/带宽等设置，未找到可验证的无损、4:4:4、固定QP或强制实际100Mbps接口；不能靠改字段名字声称解锁。

索尼2026-03-17公告介绍Portal的1080p High Quality模式，明确比Standard提高码率，重启会话生效。这说明“官方1080p就只能达到当前画质”的说法没有依据。公告没有公开客户端协议、准确码率或编码参数，不能推断Veyra的100Mbps请求已等价启用该模式，也不能推断存在隐藏魔法参数。

本次在线Chiaki主线launchspec仍包含 `videoEncoderProfile=hw4.1`、`minBandwidth=0`、`adaptiveStreamMode=resize`；与本机固定版本这一部分一致。仅凭字段名字不能把hw4.1改成其他值、把minBandwidth当作CBR开关。未改协议、未伪造网络反馈。

4:2:0会减少颜色细节，但不等于亮度只有540p，不能单凭这个解释人物整体模糊或灰。HDR/SDR转换错误是独立待比较项，不能当作码率问题处理。

## 本轮代码与实际测试

- 新 `tests/integration/SourceFidelityTests.cpp`，CMake目标 `veyra_source_fidelity_tests`。只组合共享EnhanceGraph/VideoPresenter及既有显式测试回读接口；不建立第二套播放器，不向正常播放加入GPU回读。使用自身生成的灰阶/单像素图，无PS5连接、主机操作或保存游戏截图。
- `cmd /c out\remoteplay\build-source-fidelity.cmd`：MSVC环境下等价 `cmake --build out/remoteplay/product-repair --target veyra_source_fidelity_tests -j8`，成功；日志 `logs/source-fidelity-build.log`。现有依赖探测警告未改变。
- `out/remoteplay/product-repair/veyra_source_fidelity_tests.exe`：约4秒，exit0。两组颜色/像素检查、12组呈现检查通过。NV12/planar输出一致，单像素边缘错误0，1:1误差0，1440p/2160p双线性最大误差分别0.546875/0.5；日志 `logs/source-fidelity-result.log`。
- `veyra_yuy2_color_tests.exe --remote-yuv`：四组limited/full、601/709通过，颜色参考最大误差1/255，呈现误差0；日志 `logs/source-fidelity-color.log`。
- `veyra_hw_import_image_tests.exe logs/ps5-p1-1080p30-long.mp4 logs/source-fidelity-hw`：四帧软硬解误差0，两种texture array并发错误0，约5秒exit0；日志 `logs/source-fidelity-hw.log`。生成的诊断PNG未查看、未提交。
- 确认开发包avcodec-63.dll SHA256仍为 `0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F`，保留多slice修复。本轮未更换DLL。
- 先前真实PS5同AU软硬解误差0证据为 `logs/ps5-live-patched.log`；明确是此前证据，本轮未再次执行该实机项。
- 本轮没有NR/FG/SR Create/Evaluate；新测试使用RTX5070进行颜色转换与呈现。产品算法未改，因此没有新的产品画质修复版。本轮没有检查PS5真实H265的同AU软硬解、HDR到显示器、游戏时变码率/量化，也没有宣称这些通过。

## 接续顺序

1. 客户端优先补齐色度位置与可对照的普通缩放，保持1:1直通、无AI增强；用已知像素验证不新增损伤，实际清晰度由用户验收。不强制用户换场景，不抢占正在运行的Chiaki串流。
2. 主机侧若要继续追究源头压缩，采集同一压缩序列的SPS/PPS/VUI/量化与实际帧尺寸/字节统计，并做两解码器数值对照。只保存忽略目录；QP只能作为编码损失线索，没有原始未压缩参考不能据QP宣布视觉质量。
3. Portal高质量模式仅列研究线索，必须找到实现/协议和实机差异证据后再改协商；不以网络流量填满为成功标准。

## 一手来源

- [Sony：Portal 1080p High Quality](https://blog.playstation.com/?p=416772)，2026-03-17公告。
- [Chiaki launchspec](https://raw.githubusercontent.com/streetpea/chiaki-ng/main/lib/src/launchspec.c)，本机固定提交 `0e16950165f06e5c3291537c2eeba6e852be7120` 同文件另作对照。
- [libplacebo渲染参数](https://raw.githubusercontent.com/haasn/libplacebo/master/src/renderer.c)，default/high-quality与采样分派；仅作公开上游算法证据。

未推送、未发布；SDK、DLL、凭据、日志与测试媒体均不进入Git。当前结论是定位边界与本地能力差异，不是“模糊已经修好”。
