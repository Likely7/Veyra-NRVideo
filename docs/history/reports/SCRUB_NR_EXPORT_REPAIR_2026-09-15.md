# 拖动预览、NR参数与导出收尾修复

日期：2026-09-15。分支：`codex/user-issues-repair-20260915`；本轮基线 `0b49279`。仅本地候选，未发布。

## 结论与验收范围

| 项目 | 确认的问题 | 本轮处理 | 验收与限制 |
| --- | --- | --- | --- |
| 拖动时显示画面 | WM_HSCROLL 明确忽略 THUMBTRACK，仅松手请求 seek | SeekPreview 保留一个在途请求和最新指针目标；前一帧显示后继续请求最新位置，松手立即覆盖为精确目标 | 真实4K文件、全屏暂停/播放拖动及方向键自动测试通过；刷新速度受关键帧解码和所开增强耗时限制，不承诺逐鼠标事件或60fps预览 |
| NR强度2暗部全黑 | 在线性光中外推 `base + 2*(NR-base)` 可成为负数，最后 max(0) 导致细节整片归零 | 对超过原NR暗化端点的加强量使用正值、切线连续的延伸；强度0原图、0至1正常混合、默认1原端点保持。HDR合法负分量不按SDR黑色截断 | GPU合成测试、真实NR参数测试通过；没有复制Magpie代码或宣称画质等同。加强暗化仍会变暗，并非把强度2改成强度1 |
| 导出99%失败 | 旧代码混合编码finish/音轨/trailer结果；关闭输出未检查错误；视频包写失败仍增加written；验证/改名错误解释不足 | 正确计数成功写入的帧；分开记录封装收尾、关闭刷新、逐帧验证、MoveFileEx错误；显示逐帧验证进度；保留partial | 完整3600帧有效视频+改名锁测试、损坏最后一帧测试通过。缺反馈者原文件/日志，不能认定其根因，更未取消完整性校验 |
| 点击导出没窗口 | 按钮启用条件不含failed，但点击处理函数遇到failed静默返回 | 导出使用独立worker及已应用参数，允许已有帧的失败预览发起；其他不可导出状态明确提示；记录CommDlgExtendedError并给默认文件名 | 实际UI点击观察到“另存为”及默认文件名，取消无任务。未知机器的系统弹窗错误仍需新日志 |

## NR参数逐层审查

SettingsWindow控件100/101/102/103分别提交模型强度、局部明暗、局部结构、肤质；107至111分别提交total/darken/brighten/color/luminance。模型浮点字段经NGX F32，风格/自动遮罩/UI修正经I32，未发现错接字段。范围：前三项0至1，肤质-1自动或0至2，残差五项0至2。残差常量顺序与HLSL一致。

旧 `RepairParameterTests.cpp` 在默认效果改为关闭之后，没有设置 `s.nr=true`，applySettings实际关闭了NR。因此原测试不能继续证明参数有作用。现已显式开启，并要求18组各4帧、累计72次真实Evaluate。

真实测试：intensity/tone/structure/style/autoMask及五个残差控制均观察到输出变化；肤质、UI修正在该合成素材上未观察到变化，继续保持未证实标识。未知运行库、实际人脸/UI素材和Magpie同帧对照尚未验证。

暗部GPU独立夹具使用原始线性0.10、NR结果0.04。旧强度2得到-0.02后归零；新结果0.016，保持正值、暗于默认结果。0/0.5/1分别仍为0.10/0.07/0.04；逐项测试五控件0/0.5/1/2，中性颜色不会因“色彩变化”产生色偏。HDR默认测试保留(-0.2,3,0.75)，零残差/零强度保留原图。这里没有修改输入gamma、输出transfer或整体对比度。

## 导出验证

`FileSafetyTests -- export-lock-final` 在封装关闭后持有只读文件句柄，允许解码、禁止rename。真实解码3600/3600帧成功后，MoveFileEx返回Windows32；必须报告“验证已通过，但保存文件名失败”。这证明一种99%保存失败机制，不能冒充反馈者复现。

`export-corrupt-output` 在校验前破坏最后一个视频包；必须失败并保留partial。能够被播放器播放或被MKV重封装不代表每一帧、时间戳和结尾均完整。当前CFR帧数、时间戳及HDR格式验证继续保留。已有目标文件不覆盖，界面要求新名称。

## 实际命令与证据

- 构建：`cmd.exe /c out\build\veyra-build-x64-release.cmd`，日志 `logs/scrub-nr-export-build[-b/-c/-d/-e]-20260915.log`，均exit0。使用现有patched FFmpeg，未改运行组件。
- `scripts/run-short-test.ps1 -Exe out/build/audio-continuity-repair-20260915/veyra_repair_shader_tests.exe -TimeoutSeconds 45 -LogPrefix logs/scrub-nr-shader-20260915`：exit0，暗部/零残差/HDR有符号端点通过。
- 同wrapper运行 `veyra_repair_parameter_tests.exe`，120秒上限，`logs/scrub-nr-parameters-20260915.stdout.log`：exit0，72次Evaluate；实际NGX创建/执行日志保留。
- GUI：`veyra.exe <用户4K视频> --no-nr --no-sr --no-fg --smoke-transport --smoke-seconds 24`，wrapper55秒上限。最终 `logs/scrub-transport-c-20260915.stdout.log` / `logs/scrub-transport-c-app-20260915.log` exit0。首次失败为测试读取了恢复前的旧快照；第二次目标受系统鼠标消息影响，测试只发送合成坐标却未同步真实指针。修正测试等待下一快照及测试指针定位，保留两次失败日志，不删除失败记录。
- `veyra_file_safety_tests.exe export-lock-final loop/local/fixed_clips/test_av_1080p.mp4 logs/export-lock-20260915.mp4`，wrapper120秒上限：exit0，`logs/export-lock-20260915.stdout.log`。
- 对应 `export-corrupt-output` 模式：exit0，`logs/export-corrupt-20260915.stdout.log`，损坏结尾被拦下。
- 使用computer-use实际打开专业导出页，滚动到按钮、点击，确认“另存为”、`test_av_1080p-Veyra.mp4`及保存类型。取消后恢复界面，未发起导出。`logs/export-dialog-app-20260915.log`记录打开/取消。

## 未完成项与下一步

反馈者99%失败的原文件/导出参数/独立worker日志尚未取得；不能宣称该用户问题已根治。NR需用户对问题素材复测。此前HDR转SDR的固定峰值/色域裁切改善仍在待修范围，本轮没有把暗部残差保护当作HDR映射修复。

HDR后续已查阅[ITU-R BT.2390-6](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2390-6-2019-PDF-E.pdf)的EETF和YRGB方法及[BT.2446-1](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2446-1-2021-PDF-E.pdf)。下一条实现任务是以独立标准参照替换粗糙HDR转SDR曲线及逐通道裁切，解析静态峰值、保留原生HDR不变并加入彩色测试；不能以“缺问题片源”为由永久搁置已知算法缺口。原生杜比视界、XeSS SDK内部精确计时及未到手硬件验收仍不冒充完成。
