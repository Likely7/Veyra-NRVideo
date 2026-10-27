# HDR→SDR 色调与色域映射修复

前置提交 aa40381；沿用 codex/user-issues-repair-20260915 本地修复分支。用户授权接着修 HDR→SDR，不发布、不变更系统 HDR。原生 HDR、SDR代码值保留、NR HDR代理/基底还原合同保持。

## 证据与设计

旧 YuvToLinearRgb：所有输入用1000尼特峰值的肩部；先把转换为BT.709后的负分量截断，再按亮度压缩并逐通道saturate。这会改变宽色域颜色的原始亮度和色相。此前原生PQ/HLG数值往返已通过，不能把转SDR问题扩大成所有HDR路径错误。

采用公开BT.2390 EETF的PQ域Hermite肩部，目标理想黑0、SDR峰值203尼特；输出为相对于目标峰值的linear709，经已有输出只编码一次sRGB。源峰值与目标峰值相同则不压缩；不把较暗素材拉到全白。203是软件的明确映射参考，不是测得用户显示器亮度，也不是保证HDR漫反射白映射到SDR满白。

峰值优先有效MaxCLL（0视未知，MaxFALL不能大于MaxCLL）；其次有效母版显示峰值（仅标作fallback，不能称内容实测）；再用显式1000尼特假设。范围外/NaN拒绝，低于203的有效峰值按203处理防止放大。HLG保持已有1000尼特参考OOTF，不用PQ静态标签更改HLG显示定义。每个图首次HDR帧锁定峰值，不跟随逐帧亮度；动态峰值扫描和动态元数据不在本轮。

色域映射保留有符号linear709分量，用同一色度倍率向同亮度灰轴压缩；在目标色域边界90%处开始连续肩部，内部颜色不动。不独立裁RGB三通道，保留线性RGB色度方向和映射后的Y。这里的色相方向是线性RGB几何定义，不宣称感知空间的完美等色相。

参考：ITU-R BT.2390-3 §5 的PQ归一化、knee和Hermite公式，https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2390-3-2017-PDF-E.pdf 。BT.2390-6 PDF读取超时，未冒充读过；没有复制第三方项目源码。

## 实现清单

- FramePacket/ColorMetadata/MediaFileSource：传递并读取帧/流静态亮度元数据，记录来源；HdrToneMap.h单独决定有效峰值及fallback。
- EnhanceGraph：每图锁定选择值，YUV shader常量由8扩到12，记录映射路由和峰值出处；结束清除。
- HdrToSdr.hlsli/YuvToLinearRgb：替换旧肩部和逐通道裁切，共享现有GPU pass，不增加CPU回读/帧等待/额外处理循环。
- VeyraShaders.cmake：新增include依赖，防止增量构建沿用旧shader。
- HdrToneMapGpuCases：标准参考用独立Bezier/de Casteljau表达，避免把生产Hermite展开式原封不动复写成测试。覆盖PQ/HLG、有限/全范围、203/400/1000/4000/10000峰值、灰阶/高饱和色，以及Y保持、色度方向、有限范围、单调性。
- HdrColorTests：保留原生HDR、SDR和采集格式检查，替换旧色调映射期望；文件软/硬解和真实增强继续回归。

## 验收计划

构建→独立GPU颜色/原生HDR回归→真实元数据文件软硬解对照→NR/SR/FG下的HDR转SDR→完整delivery。单次测试不超过300秒。记录失败原因，用户屏幕/原始问题电影未提供，不以合成信号宣称全部素材主观验收。最终结果随后补记。

## 已执行的颜色与文件验证

`scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_hdr_color_tests.exe -Arguments @('out/hdr-audio-fixtures/pq-static-2000-20260915.mp4','2000') -TimeoutSeconds 150 -LogPrefix logs/hdr-tonemap-file-c-20260915` exit0。包括20组新映射GPU色块、元数据有效性/继承、同图reset稳定性、原有16组原生HDR往返及其真实呈现缓冲、SDR/采集格式/旧HDR输出组合；全部通过。

本地合成PQ/HEVC fixture为1280×720、30帧、内容峰值MaxCLL=2000、MaxFALL=400、母版显示峰值4000，使用FFmpeg/libx265独立编码，命令输出 `logs/hdr-tonemap-fixture-20260915.log`。软件/硬解均读出并选中2000，整帧无增强RGB8输出最大差0个代码值；并非只是文件能打开。

同一文件运行6组真实增强：软/硬解各做NR、SR→NR→FG、NR→SR→FG；每组NR Evaluate=12，后四组SR=12且generated=11，日志全部pass1。NGX Init/CreateFeature返回0x1 Success，NVOF执行见同日志。测试同时包含4K工作输出，不等于4070/所有硬件实测性能。

失败记录：首次扩常量时误把ComputePass的描述符槽参数8改成12，实际根常量仍为8，GPU测试检测转SDR黑输出并exit1（`hdr-tonemap-colors-a`）。修正为8个描述符槽、12个根常量后`hdr-tonemap-colors-b`及最终file-c测试通过，未改测试阈值掩盖黑图。build-c/build-d因用户重新打开的Veyra锁住EXE而LNK1104，独立测试目标已构建，不能把这些整包构建标为成功；等待关闭后重链。旧诊断media_probe同步扩根常量到12，避免使用新shader时ABI不匹配。

## 已知边界

静态元数据错误或缺失时不能知道真实内容峰值；仍明确回退1000而非宣称已测量，超过所选峰值的高光会到输出白点。没有逐帧动态亮度扫描、HDR10+/DV动态元数据、显示器测光或反馈者原始电影对照。本轮解决的是固定映射缺少元数据及逐通道裁切的确定局限，不宣称HDR→SDR可以无损保留HDR全部亮度/色域。

## 当前可续接状态

新映射20组最大linear Y误差0.000526118、色度方向误差0.000390535；16组原生HDR往返保持通过，实际呈现缓冲一致。用户18:49重新启动的Veyra PID11576持续播放capture2采集，标准EXE被锁，未强制关闭。

为继续独立验证，采用Ninja给出的实际链接命令，只将输出EXE/LIB/PDB名字换成veyra-hdr-tonemap，`out/hdr-tonemap-link-alternate.cmd` 成功；全部最新应用对象与库正常链接，未复制或更换运行组件。独立候选 `out/build/audio-continuity-repair-20260915/veyra-hdr-tonemap.exe` SHA256 `07FF8C78BB20724DE16EF4302AC56CF3E1267B7562C66F4C56B1774EDA68CE99`，单独入口 `out/start-hdr-tonemap-candidate.cmd`。常用入口仍指向未最终替换的veyra.exe。media_probe同步ABI后经 `out/hdr-tonemap-build-probe.cmd` 构建通过，本轮未运行该历史probe。

整体gate命令 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/audio-continuity-repair-20260915 -PlayerExe out/build/audio-continuity-repair-20260915/veyra-hdr-tonemap.exe` **失败**，`logs/delivery/4579d1c0b0e3407d84b0f74c8fdf11ee/result.json`。实际NR/NVOF和原生4K前项通过，但player-sync时差P95=80.15ms，超过50ms，processed43FPS、generated1。另一份Veyra同期开着采集可能竞争GPU；这只是待排除因素，不能未经独立复测归因。未运行后续gate项、不标全包验收通过。

下一步：收到用户停止现有采集的回复后，正常构建替换veyra.exe并在无并行采集时重跑delivery；若仍失败，比较SDR分支计时和运行负载再定位。当前颜色专项通过，整包性能验收与常用入口替换未完成，不发布。

### 后续标准候选替换完成

用户随后报告补帧受限却显示180fps；检查时Veyra已退出。保留HDR修复，并修正主帧率显示口径后，标准构建 `logs/fg-rate-display-build-20260915.log` exit0，完整delivery `logs/delivery/6bade0b84d8449d89198cffa37453a2d/result.json` PASS。这次无并行用户采集，前次player-sync失败未再次出现；单次通过不构成并行负载因果证明。

常用入口 `out/start-user-issues-candidate.cmd` 已指向含本轮全部HDR改动的标准veyra.exe，SHA256 `888BC5E1ED098AD1E3A4C0188D58100452E31364DF45826267FAADB9C01C24E6`。颜色专项证据沿用本文件file-c（其后未再改颜色算法），全包验证补齐；采集3X生成后过期是新增未闭环问题，见FG_OUTPUT_RATE_AUDIT。未发布。
