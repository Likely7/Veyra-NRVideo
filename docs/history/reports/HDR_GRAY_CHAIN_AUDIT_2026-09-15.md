# HDR 发灰链路复核

后续施工：用户已授权替换HDR→SDR映射，当前实现、专项结果和待完成整包验收见 [HDR→SDR修复记录](HDR_TO_SDR_MAPPING_REPAIR_2026-09-15.md)。以下是此前审计，保留追溯，不代表新映射尚未施工。

## 本轮结论与范围

用户要求检查此前HDR发灰是否也是错误曲线，反馈时实际输出模式未知，没有取得反馈者原始HDR媒体或日志。不能从SDR的BT.1886/sRGB错误推出HDR也有同样错误。

本轮检查覆盖原生PQ/HLG的输入范围、YUV矩阵、线性工作单位、输出编码及真实交换链缓冲；同时检查NR基底恢复、NR+视频SR+DLSS FG，以及一个本地合成HDR视频的硬解输入。未修改HDR画面曲线，没有用对比度抵消未知原因。

结论：测试范围内没有发现原生HDR链路的系统性亮度抬升或压暗。HDR转SDR存在固定映射和简单色域裁切的局限；这与已确认的SDR错误不同，尚不能认定为反馈者的根因。原生HDR和转SDR必须分开收集证据。

## 当前实际链路

1. PQ：有限/全范围展开→BT.2020 NCL YUV转RGB→ST2084 EOTF到绝对尼特→BT.2020转线性BT.709→除80，作为scRGB工作单位。
2. HLG：范围/矩阵转换后，逆HLG OETF恢复场景光，再按1000尼特参考显示、gamma1.2的OOTF得到显示亮度。HLG参考峰值当前固定，不是读取实际显示器峰值。
3. 无FG原生HDR：FP16 scRGB直接呈现，1代表80尼特，保留负色域分量和大于1的高光。
4. FG原生HDR：工作色域转回BT.2020并编码PQ，使用RGB10/PQ交换链；参考画面也只在对应边界编码PQ。未发现重复sRGB编码。
5. HDR转SDR：在线性BT.709域截掉负分量，以203尼特归一化，按固定1000尼特峰值做亮度肩部压缩，最后RGB通道裁切到0至1并编码sRGB。开启增强时先做此转换，再进入SDR增强。
6. 原生HDR增强：NR/视频SR使用SDR代理供模型处理，把模型改动合成回保留的HDR基底；这不是模型原生HDR推理。零代理改动应保持原基底，测试覆盖了该不变量。

Windows scRGB单位核对：[微软Advanced Color文档](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)。HLG/PQ定义核对：[ITU-R BT.2100](https://www.itu.int/rec/R-REC-BT.2100)。这些标准说明转换定义，不代替本产品测试。

## 新增的独立GPU检查

`tests/integration/HdrNativeRoundTripCases.h`，由 `veyra_hdr_color_tests` 执行：

- PQ输入包含0、0.005、0.1、1、10、80、100、203、400、1000、4000、10000尼特中性块、BT.2020原色和混合颜色；HLG以对应场景信号生成，按BT.2100的参考显示解释。
- limited/full、P010/平面10bit、scRGB/PQ输出共16种组合。HDR10格式通过关闭NGX而选择FG输出格式来独立验证，不能把这16种格式检查宣称为实际补帧验证。
- 期望值从量化后的源YUV重新按双精度矩阵和标准公式计算；原生HDR要求还原源亮度，不以固定色调映射代码作预期。
- 增加 `VideoPresenter::presentedResourceForTest()`，读回真正的HDR交换链缓冲，与图输出精确比较；只被集成测试调用，播放/导出不增加回读。返回引用必须在resize/close前释放。

最终 `logs/hdr-native-hlg-audit-20260915.stdout.log` exit0：

| 输入/输出 | >=1尼特通道最大相对误差 | 图输出→呈现缓冲 |
| --- | --- | --- |
| PQ→scRGB | 0.0868% | 完全一致 |
| PQ→RGB10 PQ | 0.524% | 完全一致 |
| HLG→scRGB | 0.0893% | 完全一致 |
| HLG→RGB10 PQ | 0.648% | 完全一致 |

scRGB近零颜色分量（包括高饱和颜色经矩阵抵消后的分量）最大绝对误差0.126尼特，不等于纯黑抬升。FP16/PQ10量化保留在结果里，不宣称数学无损。测试覆盖设备合成前的缓冲，未测量物理屏幕亮度或显示器色彩处理。

## 增强及文件验证

- `veyra_hdr_enhancement_tests.exe 1`：exit0，NR实际20帧，测试高光1003.75尼特；零代理改动、近黑/宽色域基底和JXR回验通过。`logs/hdr-audit-nr-20260915.stdout.log`。
- `veyra_hdr_enhancement_tests.exe 3`：日志最终 `pass=1`，NR20/SR20/生成18，1000尼特测试高光998.932尼特。`logs/hdr-audit-sr-fg-20260915.stdout.log`。会话工具在用户打断后句柄失效，后续确认进程已退出并读取最终日志；不将无法取回的退出码补写为0。
- `veyra_hdr_enhancement_tests.exe 1 0 out/hdr-audio-fixtures/pq-tagged-51.mp4 1`：90秒上限，exit0；真实FFmpeg D3D12硬解及NR20帧，高光1003.75尼特，`logs/hdr-audit-file-hardware-20260915.stdout.log`。该视频是项目本地合成fixture，不是反馈者电影。
- `veyra_hdr_color_tests.exe` 同时完成此前SDR/采集/PQ/HLG色块回归；新用例最长90秒。

中途失败：测试首次调用不存在的makeReadbackBuffer，编译失败后改为显式READBACK资源创建；首次给10000尼特FP16输出设固定0.1尼特中性误差限，超过格式能表达的精度而失败，改为按FP16精度判断相对偏差。原始失败 `logs/hdr-native-audit-20260915.stdout.log` 保留。正式阈值与测量值均在源码及日志列明。未删除失败结果制造通过。

## 确认的局限及后续方案

HDR→SDR无法与HDR屏幕输出保持绝对亮度恒等，必须明确选择色调/色域映射。现有固定映射并非由显示器或每个文件的实际峰值驱动。它对所有素材套用同一压缩，超过参考峰值的颜色可能裁切；把负BT.709分量先截掉也会改变宽色域饱和颜色的亮度和色相。没有实际问题素材时不能仅凭这一缺口宣布全部发灰已定位。

后续若确认反馈走转SDR路径：

1. 取得问题片段及实际输出路由，对照同一帧范围、色域、亮度和裁切比例。
2. 将MaxCLL、母版峰值和缺失/异常值分别处理，不把母版显示器峰值等同于内容实际最大亮度，也不盲目让元数据控制画面。
3. 明确目标SDR参考白和输出曲线，采用经过独立参考比对的亮度与色域映射；必要时增加稳定峰值估计，避免逐帧抽动。
4. 原生HDR旁路必须继续通过本轮恒等检查。转SDR的验收用明确参考实现/已知目标灰阶与彩色映射，而不能仅复写shader公式。

本轮新增诊断：文件首帧记录母版亮度、MaxCLL/MaxFALL及其frame/stream来源；缺失明确为-1。图记录实际PQ/HLG解码、工作单位、HDR/SDR输出和是否套用固定映射。只记录已取得信息，不把缺失值写成真实1000尼特。不修改当前映射策略、运行组件或系统HDR开关。

待验收：反馈者真实文件/采集/PS5；跨显示器HDR状态变化；实际显示亮度；未标记或误标记HDR媒体。不能将本轮通过扩大成全部HDR发灰根治。

## 本地交付

最终全量目标构建 `cmd.exe /c out\build\veyra-build-x64-release.cmd` exit0，`logs/hdr-audit-product-build-20260915.log`（38/38）。`veyra.exe out/hdr-audio-fixtures/pq-tagged-51.mp4 --no-nr --no-sr --no-fg --smoke-seconds 3` 经30秒短测包装exit0，日志 `logs/hdr-audit-gui-20260915.log`。未重复上一轮完整delivery，原因是本轮产品改动只增加日志及测试专用读回接口，未改渲染行为；本轮相关GPU及GUI验证如上。

本机这次GUI实际为 `hdrInput=true hdrOutput=false forceSdrPreview=false`，自动选择转SDR；30帧、failed=false、NR/NVOF均0。说明“打开HDR文件”并不自动代表“正在HDR输出”。这不是反馈发生时的状态证明，也不能单凭此判断Windows开关或显示器硬件原因。

候选仍在 `out/build/audio-continuity-repair-20260915/veyra.exe`，SHA256 `1CAC34939919C19EEF1FF271857159E8786AD2EF1BA7FCA5CBD6EBE394DC6321`，入口 `out/start-user-issues-candidate.cmd`。源码/测试/文档本地提交，不包含SDK、日志、fixture或运行组件；未push、未发布。
