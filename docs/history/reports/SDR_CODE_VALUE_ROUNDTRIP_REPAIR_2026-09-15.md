# SDR 效果全关颜色偏暗：端到端修复

## 反馈与推翻的旧结论

用户用同一视频对比其他播放器，Veyra 在关闭所有效果后仍偏暗。当前运行程序确认为 `out/build/audio-continuity-repair-20260915/veyra.exe`。上一批将文件 BT.709 工作解码改成 BT.1886 理想黑点（幂2.4），但 SDR 输出仍是 sRGB 编码，两者不互逆。这是代码值转换错误，不能作为“发灰已修复”的依据。

上一批 `SourceFidelityTests::expectedGray` 同样计算 BT.1886→sRGB，属于错误预期：能够确认执行一致，无法证明无增强输出忠于原信号。撤回总修复方案中据此认定文件中灰修正正确的结论；旧通过日志只作历史证据。

## 独立复现

用户原文件 `C:/Users/123/Downloads/OpenAI-This_is_GPT-6_Astra__10368kbps-20260906113552.mp4` 为1920×1080 H.264/yuv420p，无range/matrix/transfer/primaries标记。生产解析推定limited/BT.709，假设写入日志。没有足够信息把该推定当成原作者确认的色彩空间。

新检查直接从解码YUV按有限范围展开和BT.709矩阵计算RGB代码值，与关闭所有效果、同分辨率GPU输出逐像素比较。该预期不调用生产transfer函数，不计算任何对比度补偿。同时读取真正呈现缓冲，检验后段是否又修改颜色。硬解参考仅在诊断中回读解码平面，生产链没有增加回读。

修复前：

- 灰阶0至255端到端最大误差13.1233级，平面YUV与NV12均失败。
- 原视频seek到60秒后的相同首帧，软/硬解PTS均513137900；RGB最大误差13.3126级，平均绝对误差6.74098级，平均带符号误差-6.74098，均失败。
- 图输出至交换链缓冲最大误差0，说明本例不是显示blit再次变色。
- 原视频检查退出码1：`logs/sdr-file-before-20260915.stdout.log`；灰阶首次红测 `logs/sdr-roundtrip-before-20260915.stdout.log`。

## 修复合同

普通桌面SDR播放默认保留范围展开和矩阵转换后的RGB代码值。仍使用统一线性工作图，输入采用与既有sRGB输出互逆的解码，不能把源BT.709标签直接改写成sRGB，不能在输出端加一条肉眼调参的补偿曲线。

- `ColorDescription::preserveSdrCodeValues` 明确区分桌面代码值保留策略与源transfer元数据。
- 文件启用该策略；采集保持已有未标记SDR行为，修正上一批新增显式BT.709时的错误2.4分支。
- 本策略只影响BT.709 SDR工作解释；PQ/HLG、线性输入、BT.2020广色域路径不套用该规则。PS5现有参考显示策略保持独立，本例没有PS5同源实测，不能据本次结果宣称它正确或错误。
- 输出、NR proxy、截图与导出仍复用原共享图。新日志同时记录源transfer、代码保留策略及实际工作transfer。

修改文件包括 `FramePacket.h`、`ColorMetadata.h`、`MediaFileSource.cpp`、`CaptureMediaType.h`、`CaptureCardSource.cpp`、`EnhanceGraph.cpp`，以及源保真、SDR色块和采集颜色合同测试。新增 `tests/integration/FileSdrRoundTripCases.h` 执行真实媒体软硬解与呈现缓冲对照。

## 验证命令与状态

```powershell
cmd.exe /c out\build\veyra-build-x64-release.cmd
# run-short-test.ps1 包装，下列用例限90秒
out/build/audio-continuity-repair-20260915/veyra_source_fidelity_tests.exe <用户原文件>
```

构建 `logs/sdr-roundtrip-build-20260915.log` exit0，75/75。用户关闭应用后实际替换了同一候选目录的EXE。

- 修后同文件检查exit0：`logs/sdr-file-after-20260915.stdout.log`。软/硬解同PTS513137900，最大误差0.629614级，平均绝对误差0.256191级，平均偏差-0.0670757级；灰阶最大误差0.575342级；图输出至交换链误差0。误差包含FP16和8-bit输出量化，未冒称数学上的逐位无损。
- HDR/SDR/采集GPU色块：`veyra_hdr_color_tests.exe`，90秒上限，exit0，`logs/sdr-repair-hdr-regression-20260915.stdout.log`。覆盖现有PQ/HLG、full/limited、平面/P010、原生HDR/SDR映射和四种SDR输入packing。
- `veyra_repair_contract_tests.exe`：137项失败0，`logs/sdr-repair-contracts-20260915.log`。首次误用不存在的 `veyra_capture_color_contract_tests.exe` 名称，未实际执行该测试；随后按CMake真实目标 `veyra_capture_color_tests.exe` 重跑，不将先前PowerShell残留退出码作为成功。
- `git diff --check` exit0，只有既存换行提示。

候选EXE SHA256 `EFA748C7F7D64DF3E9ECE814CE56D2015BC976192F00195443C2B0E17A8EDA96`，入口仍为 `out/start-user-issues-candidate.cmd`。本轮未改版本、未发布或替换运行组件。

尚未对其他播放器的显示设置/ICC/驱动视频处理逐项比对，因此不宣称屏幕最终像素与所有播放器完全一致；本次首先要求软件内部的无效果SDR链路不擅自改变代码值。实卡采集/PS5和反馈者屏幕复测未执行。
