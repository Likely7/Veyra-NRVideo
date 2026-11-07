# NVENC导出关键帧审查与修复
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户要求先审第三方诊断、再修，不盲改。分支`codex/nvenc-export-keyframes-20261103`，基线`edad6e9`；继承RTX30 VFG实验兼容与全屏进度条修复，不合main、不push/发布。不改用户配置、驱动、SDK、运行库、模型或实时录制。

## 修改前证据

当前`NvencD3D12Encoder`从P4低延迟preset取得完整配置后，仅覆盖GOP为120、IPPP、关闭lookahead，未同步覆盖codec的IDR周期。`VideoExportJob`直接把编码器回调的key写入`AV_PKT_FLAG_KEY`，未解析码流。录制路径另有显式IDR周期，不是本次修改对象。

真实RTX5070生产`veyra_export_probe`导出640×360、30fps、270帧（9秒），效果全部关闭：

| 导出 | MP4 stss（1起始） | IDR个数 | 其他 |
|---|---|---|---|
| H.264 | [1] | 1 | 269个非IDR slice |
| HEVC | [1] | 1 | 2个CRA，267个TRAIL_R |

回执/原文件：`E:/项目/Veyra/reports/nvenc-export-keyframes-20261103/before.json`、`E:/项目/Veyra/tests/nvenc-export-keyframes-20261103/before-{h264,hevc}.mp4`。这支持缺陷机制，但不是用户那份未提供原视频的独立验真。

## 对建议的审查

- SDK13.0头文件说明：`idrPeriod`为0时默认跟随GOP；但调用GetPreset获得的配置不能当成全零配置，必须覆盖继承的周期。修复日志记录preset与最终周期。
- 显式为当前codec设置`idrPeriod=gopLength`；保持原120帧GOP、编码质量、码率、D3D12 slot/fence、HDR配置和时序。不把120帧说成任意帧率下均为2秒：30fps为4秒、60fps才是2秒。
- **不采纳I帧全标关键帧的兜底。** H.264非IDR I不清空DPB，不能无条件承诺随机访问；FFmpeg n8.0的NVENC同样只给IDR设置KEY，I只设置picture type。本修复产生真正周期IDR并保持严格标记，不伪造索引。
- 不承诺普通`ffmpeg -c copy`必能修历史文件：重封装可能沿用源关键帧标记，不能凭空把非IDR变成IDR；旧视频需单独检验码流及重建索引方案。
- 不只用ffprobe flags证明修复：核对MP4原始stss、实际AnnexB NAL类型、生产导出帧数、时间戳、全程解码与中间/末段随机跳转像素一致性；覆盖H.264/HEVC、MP4/MKV、30/60fps和码率模式。HDR额外验证HEVC Main10。

主要依据（只查一手来源，不复制第三方实现）：

- [NVENC13.0.19头文件](https://github.com/FFmpeg/nv-codec-headers/blob/n13.0.19.0/include/ffnvcodec/nvEncodeAPI.h)：IDR周期、I/IDR语义。
- [FFmpeg n8.0 NVENC](https://github.com/FFmpeg/FFmpeg/blob/n8.0/libavcodec/nvenc.c)：codec周期与输出关键帧判定。

## 执行和最终验证

实际日志确认H.264/HEVC的`presetIdrPeriod=4294967295`（无限），最终显式设为120。产品修改9行增加（5行注释、4行参数及日志），不扩大I帧关键帧判定。普通`ffmpeg -c copy`实测重封装两个旧MP4后stss仍为[1]，不能用作本例可靠补救；见remux.json。

构建命令：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-nvenc-export-keyframes-20261103 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.2-test --jobs 2 veyra_export_probe veyra_qml_ui`。修改前probe构建3.1秒；修改后probe及主程序6.5秒退出0，回执logs/codex-nvenc-export-keyframes-20261103/build-20261103-030555/receipt.json。日志时戳UTC，客户端当天2026-11-03。

验收命令入口：`python -X utf8 -B scripts/acceptance/verify-export-keyframes.py --probe <exe> --runtime-dir <只读DLL目录> --shader-dir <shaders目录> --output-root <新证据目录>`；默认14组，可用`--cases`缩小重测。调用真实VideoExportJob，不另造编码器；本机FFmpeg/ffprobe 8.1.1，子进程上限240秒，TEMP和全部产物在测试目录。

- 新验收脚本在保存的修改前probe上跑H.264/HEVC MP4均失败：key=[0]、stss=[1]。旧代码失败证据保留matrix-old/RESULTS.json。
- 修复后12组SDR全通过：两codec×两容器×30/60fps的CQ八组，加两codec×CBR/VBR的30fps MP4四组。各270帧，无PTS/DTS重排；key序号0/120/240，MP4原始stss=[1,121,241]；码流有3个真正IDR、HEVC无CRA。完整解码270帧；第150/255帧的输入跳转像素hash与从头解码相同，索引定位确实落在120/240帧，没有偷从首帧解码。
- HDR10 HEVC Main10 MP4/MKV两组通过相同检查，输出yuv420p10le/PQ。首次合成HDR素材因libx265未写入transfer/primaries，实际只是未声明HDR的10位素材，两个HDR断言失败。只修正测试素材的显式x265 VUI并新增输入metadata先验检查，重测两个HDR用例通过；没有修改产品颜色逻辑，原失败目录保留。
- 最终14个通过用例共3780帧、28次索引及像素一致跳转。本机RTX5070、640×360合成样本，不替代用户原片或其他GPU实测。

结果位于tests/nvenc-export-keyframes-20261103：matrix-fixed/RESULTS.json（12通过、2素材失败）、matrix-hdr-fixed/RESULTS.json（正确HDR两通过）、matrix-old/RESULTS.json（旧代码两预期失败）。总回执reports/nvenc-export-keyframes-20261103/FINAL.json，保留所有命令/返回值和失败。

本地覆盖包：test-packages/codex-nvenc-export-keyframes-20261103/Veyra-2.1.2-export-keyframes-update.zip，带新EXE、继承的全屏修复及此前RTX30 VFG实验入口、对应GPL源码差异。30实卡未验边界保持，不新增SDK/DLL/模型，不改正式归档。退出软件后覆盖2.1.2 NVIDIA版EXE目录。空间、摘要和源码补丁可重放校验见FINAL回执。未合main、未push/发布。
