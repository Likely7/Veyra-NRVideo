# 七项独立审查修复

基线 `80f7dc4`，存档 `checkpoint/pre-seven-audit-20260919`，隔离分支 `codex/seven-audit-20260919`。依据 `XESS_GC573_LOG_REPAIR_PLAN_2026-09-19.md` 的“独立审查项”，用户要求七项一起修复。上一轮 DLSS/XeSS 调度修复保留；这些缺陷不能解释所有 GC573 RGB 或 5080 功耗问题。

## 实现范围

1. 压缩采集队列保持容量 3。MJPEG 可独立丢旧帧；帧间压缩溢出时清除待处理包，在解码线程 flush 并等待关键帧重新建立参考链。记录丢包与恢复边界，不继续使用损坏历史。持续解码欠速仍可能等待关键帧，不能保证无停顿。
2. 压缩 mailbox 的序号来自已解码输出计数，在同一锁内快照；不再使用仍在增长的采集回调计数。原生采集序号保持原语义。
3. 共享 FFmpeg 解码器在 EAGAIN 时接收输出并重送同一个输入包。输出缓冲最多 16 帧；超过上限明确失败，不伪报接收成功。已接收计数只在真正成功后增加；flush/close 释放缓存。文件及诊断调用方沿用 bool 接收合同。
4. 采集软件颜色转换保留解码 PTS、时间基及颜色信息；worker 使用实际输出 PTS 匹配有界输入元数据（最多 64 项），取得原始到达时间和 duration。B 帧包 PTS 的逆序不触发错误的解码重置。没有 PTS 或元数据无法匹配的输出明确丢弃并记录，后续输出标记断点。worker 排空已就绪输出，不将输出数量绑定为每包一帧。硬解首次输出不可导入时，软解从关键帧恢复。
5. D3D11 Context4 的 QueryInterface/Detach 所产生的自有引用由 releaseInterop 释放。
6. 顶部、中部、底部分别维护布局游标，中部先测整组高度再居中；显式 ASS 位置保留。支持 alignment override 和垂直 margin。
7. 字幕缓存包含所有样式字段、精确数值、字符串长度与 DPI，避免只改粗体、边距、阴影或背景时沿用旧图。

## 验收计划

- 构建主程序、压缩解码回归、字幕像素回归及文件读取回归。
- 带 B 帧的 H.264/HEVC 软件/D3D12VA 解码；批量提交强制触发 EAGAIN，核对帧数、PTS 和 flush；人为丢包后核对关键帧恢复及像素一致性。
- 检查字幕实际像素上下界、缓存命中及粗体修改后的像素变化。
- 保留原有文件播放与调度单测；没有实卡则不宣称 GC551/GC573 实机通过，不把像素测试称作人工观看验收。

产物：`E:/项目/Veyra/{tests,logs,tmp}/seven-audit-20260919/`。复用构建 `E:/项目/Veyra/build/slider-reset-20260919/` 和依赖缓存 `E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt`。本轮不打包、不发布、不修改运行库。

## 早期执行记录

`scripts/build-isolated.ps1` 的 build/build2/build3 日志构建成功。生成 320x180、30fps、3 秒、GOP15/B2 的 H.264 和 HEVC 两个测试文件；实际测试日志 `h264-recovery` / `hevc-recovery` 已覆盖软件/D3D12VA、90 帧 EAGAIN 完整输出及丢包恢复像素对比。

首次向 ANSI main 传中文绝对路径导致旧测试 SKIP，不能算通过，随后在测试目录传 ASCII 相对路径重跑。字幕首次测试因测试程序缺少现代 Windows manifest 未创建出 layered child，不计通过，后改为相同 renderer 的 layered popup，最终结果如下。

## 最终验收

干净构建 `build-utf8.log` 完成 380 个步骤；头文件触发重编译后 `build-delivery.log` 完成 14 个步骤，均退出 0。主程序及五个回归目标构建通过。以下日志位于上述本轮 logs 目录：

- `h264-delivery`、`hevc-delivery`：320x180/30fps、3 秒、GOP15/B2；软件与 D3D12VA 各输出 88 帧（实时捕获未发送 EOS，尾部两帧仍在解码器中）。单独的完整文件 EAGAIN 测试各 90 帧、64 次重送，输出顺序/PTS/已接收计数和 flush 通过。丢包后从关键帧恢复，像素校验和与基准一致。D3D11VA 各三轮打开/解码/关闭及 GPU 资源导入通过。
- `file-delivery`：3840x2160/30fps、24 帧 H.264，9 项检查通过；单线程与线程解码每帧像素/PTS 一致、EOS 数量准确、EOS 后 seek 正常。
- `subtitle-delivery`：真实 GDI+ DIB 像素检查，上/中/下两条字幕无重叠、中部整组居中；10 项样式修改使缓存失效，未修改时命中缓存，粗体修改实际改变像素。测试截取绘制结果，不等同实屏人工观感验收。
- `presentation_worker-clean`、`live_timing-clean`：已有呈现线程和时序回归通过，含 2X/4X/6X、29.97Hz、reset、时间戳跳变等。
- `app-delivery`：主程序 `--smoke-empty --smoke-seconds 5` 退出 0。

构建使用 `scripts/build-isolated.ps1 -Root .`，BuildDirectory、DependencyCache、TempDirectory 为前述目录，Targets 为 PowerShell 数组：`veyra,veyra_capture_compressed_tests,veyra_subtitle_overlay_tests,veyra_source_tests,veyra_presentation_worker_tests,veyra_live_timing_tests`。测试通过 `scripts/run-short-test.ps1`，解码测试上限 60 秒、主程序 30 秒；工作目录设为本轮 tests 目录，使用 ASCII 相对媒体路径。进程 PATH 加入 1.4.2 便携包和 patched FFmpeg bin，TEMP/TMP 仅进程重定向。

元数据最终行为补充：没有 PTS 或元数据无法匹配的输出均明确丢弃并记录，后续输出标记断点，不编造到达时间。

## 失败与追加修复

4K 文件回归曾崩溃：旧 CMake MSVC `/showIncludes` 前缀乱码，头文件修改未触发 MediaFileSource 等对象重编译，形成新旧 ABI 混合。此前增量构建不作为最终证据。强制 `VSLANG=1033` 在只安装中文语言包的本机无效；最终脚本为 configure/build 统一控制台 UTF-8 编码并恢复原编码，旧目录迁移时重新配置并清理对象（ConfigureOnly 也执行清理），避免混合链接。干净编译另暴露 SubtitleSettingsPanel 的 `std::clamp` LONG/int 推导冲突，已统一类型。

最终 Ninja 依赖记录包含 FFmpegVideoDecoder.h；仅触碰该头文件时间戳，dry-run 正确列出 MediaFileSource、CaptureCompressedDecoder、EngineController、VideoExportJob 等重编译，随后实际重编译并重跑交付测试。`git diff --check` 通过。初次错误目标名 `veyra_source_file_tests` 后改为实际目标 `veyra_source_tests`。

## 验收边界

本机 RTX5070。未接 GC551/GC573，未做真实 DirectShow 采集队列溢出和帧身份端到端测试；worker 的队列及元数据逻辑经过代码复查，解码恢复经过上述合成输入测试。未覆盖 AV1/VP9 丢包恢复、D3D11 COM 引用长期内存压力、多屏字幕视觉矩阵。未将这些修复宣称为 GC573 RGB53fps 或全部用户补帧受限的共同根因。没有独立 Reviewer；本轮为本人复查和自动回归。没有打包、合并 main、推送或发布。
