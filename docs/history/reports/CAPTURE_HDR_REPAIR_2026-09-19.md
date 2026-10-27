# P010 采集 HDR 识别与颜色排查

## 范围与结论

用户确认输入 P010；尚未提供采集卡型号、HDR/SDR 显示器状态和问题日志。
在 `codex/5060-presets-subtitles-20260918` 现有隔离区继续，保留 XeSS 回退与测试。
不发布、不合并、不改变补帧调度；P010 是位深/存储格式，不能单独证明 HDR。

已复现并修复的软件缺陷：

- `CaptureMediaType.h`：明确 PQ/HLG、缺少 matrix/primaries 时，旧代码保留 SDR 默认值，无法满足共享图的 HDR 合同。现在只针对明确 HDR transfer，为缺失字段补 BT.2100 / BT.2020 NCL 默认值，并保留 assumed 标志；明确的相冲突字段不覆盖。补充 BT.709/BT.601 primaries 解析。
- 同处丢失设备声明的 chroma siting。现在传递 MPEG2 left、MPEG1 center、cosited top-left（可带 progressive 位），未声明或不能表达的布局不猜测；颜色位置变化需要重新建立合同。
- `ColorGrade.hlsli`：曝光等非中性调色先将 BT.709 工作色域中的合法负分量截成 0，HSV 还原和函数返回再次截断。BT.2020 饱和色会因此改变色相/亮度。改为保留有符号线性光、signed gamma 和扩展饱和度范围；最终映射仍由原有 HDR/SDR 输出负责。纯中性设置已有旁路，旧版该测试通过，不能宣称只打开中性开关就必然变色。
- `CaptureCardSource.cpp`：记录原始 controlFlags、元数据存在位、transfer/matrix/primaries/chroma、手动覆盖及 HDR 推定字段；未声明 HDR 的 10 位输入明确记录保留 SDR 的原因。

## 实测

日志目录：`E:/项目/Veyra/logs/capture-hdr-20260919/`。
临时目录：`E:/项目/Veyra/tmp/capture-hdr-20260919/`。

先在已有 `E:/项目/Veyra/build/xess-view-reset-20260919` 构建回归测试。
`unit-before.log` 有 PQ/HLG 缺失字段失败；显式 BT709 测试最初错用了保留值 1，
查本机 SDK 后改为 `DXVA2_VideoPrimaries_BT709`（2），该次失败不能作为产品缺陷证据。
`gpu-before.stdout.log`：+1 EV 四组 PQ/HLG、full/limited 失败，负分量误差达到 1；中性四组通过。

修复后 `unit-final.log` failures=0；`gpu-final.stdout.log` exit 0：

- 8 组 untagged AVFrame + DirectShow P010 部分 HDR 元数据，与完整文件元数据输出逐像素一致；覆盖 PQ/HLG、full/limited、scRGB/PQ 输出。
- 8 组 HDR 调色中性/+1 EV 检查通过，包括纯 BT.2020 红绿蓝与 0..10000 nit 灰阶；+1 EV 最大归一化误差 0.000625，中性误差 0。
- 原有 HDR 往返、真实呈现缓冲一致性、HDR→SDR 色域/亮度映射与 SDR/采集格式 GPU 回归通过。
- `grade-final.stdout.log` exit 0：SDR 调色关闭/中性、曝光、曲线、饱和度、混色、抖动、实际 LUT 和错误 LUT 空间拒绝测试通过。

增量构建发现 MSVC 本地化 showIncludes 前缀乱码，header-only 变化没有可靠触发消费者重编译。
已显式重编译测试入口取得上述最终结果，另用全新目录做完整构建消除旧对象影响；不把早期缺少新增测试行的运行算作新测试通过。

命令（PowerShell，工作目录为现有隔离源码区）：

```powershell
./scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/capture-hdr-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/capture-hdr-20260919 -DisplayVersion 1.4.2beta -Targets veyra_capture_color_tests,veyra_hdr_color_tests,veyra_color_grade_gpu_tests,veyra
```

测试使用 `scripts/run-short-test.ps1`，每项最多 150 秒；仅测试进程 TEMP/TMP 指向本轮 tmp，PATH 使用已有便携包 FFmpeg DLL。没有新增或修改专有运行库。

全新目录构建最终 exit 0（`build-clean.log`）；该目录的三个测试程序分别再次运行，
`unit-clean.log` failures=0、`gpu-clean.stdout.log` / `grade-clean.stdout.log` 均 exit 0，
8 条 `HDR_CAPTURE_PARTIAL_TAGS exact=1`、8 条 `HDR_GRADE_SIGNED pass=1` 均实际出现。
主程序已构建为 `E:/项目/Veyra/build/capture-hdr-20260919/veyra.exe`，未制作新便携包。

## 未验证与下一步

这是确定软件缺陷的回归证据，不能据此断言反馈者的全部症状已根治。
没有元数据的 P010 仍无法可靠区分 SDR/PQ/HLG；只报 BT.2020 也不足以确定 HDR transfer。
动态采集中 SDR/HDR 切换仍需重开输入和处理图，本轮未扩展该生命周期。
未执行反馈者实卡、物理 HDR 显示器、NGX NR/SR/FG Create/Evaluate 或 LUT 全空间 HDR 主观验收。
下一步使用本轮程序取得反馈者 `capture-color` 日志和卡型号，确认是未报元数据、部分元数据还是动态切换，再决定是否需要厂商信号查询。
