# RGB24 采集倒像修复：直连 top-down 协商 + 手动上下翻转（2026-09-16）

> 2026-09-16 合并说明：本页记录的是 main 侧移植（预设 schema v13）。同日 `codex/framegen-fsr-dolby-20260916`
> 合并进 main（merge `24e7de7`）后，产品代码采用分支原版实现（`015f8a4`，schema v16，含 PS5 串流修复）；
> 本页保留为当时的移植过程与 main 侧验证记录。

用户报告：部分采集卡用 RGB24 时画面上下颠倒，其他格式正常、其他软件正常。用户选定 A+B 方案，明确不做 C（强制走系统 RGB32 转换，徒增一次系统转换与延迟）。

## 1. 根因判定

整条链路只有一处决定上下方向：`include/veyra/source/CaptureMediaType.h` 按 DIB 头 `biHeight` 符号给 RGB 打 `bottomUp`，`copyCaptureSample()` 据此整行倒序读取；GPU 上传与 `RgbToLinear.hlsl` 全程 top-down，没有第二次翻转。YUV 格式（YUY2/NV12/I420/P010…）按 DirectShow 惯例忽略符号、恒按 top-down 读，所以只有 RGB 家族会被方向声明影响。

被报告的设备在 RGB24 媒体类型里声明的方向与实际样本不一致（声明 bottom-up、实际 bottom-up 以外的顺序）。RGB24 自 1.1.0 起改为原生直连（1.0.x 走系统 RGB32 转换路径），把该矛盾暴露出来；其他软件通常经系统 Color Space Converter/渲染器，或在协商时明确要求 top-down，因此不暴露。FFmpeg 的 dshow 输入同样把 `biHeight>0` 标成 "BottomUp"，可证明判断规则本身不是本项目独有的错误。

旧测试无法发现该问题：单元用例用同一公式计算期望（自证式），GPU 采集用例每行图案相同（上下翻转照样通过）。本轮补上了真正的方向断言。

## 2. 修复

**A. 直连 RGB DIB 先协商 top-down**（`src/source/CaptureCardSource.cpp`）

- caps 类型属于 Bgr32/Bgra32/Bgr24/RGB555/565 且 `biHeight>0` 时，先把 `biHeight` 取负再 `SetFormat`；
- 驱动接受 → 重新 `GetFormat` 并按实际连接类型解析（负高度 = 不翻转），说谎的卡被自动纠正；
- 驱动拒绝 → 恢复原符号，继续按声明符号翻转，诚实的 bottom-up 设备行为不变；
- 新增日志 `[capture] DIB top-down request hr=0x… subtype=0x… accepted=0/1 bottomUp=…`，用户抓一行即可判定。

**B. 采集面板"画面上下翻转"开关**（立即生效的兜底）

- `EnhancementSettings::captureFlipVertical`（默认关；加入实时字段集合，切换不重建增强图）；
- `CaptureCardSource::setVerticalFlip`：原子标志，DirectShow 回调线程按样本生效；`copyCaptureSample()` 增加可选翻转参数，RGB 与 YUV 都支持，YUV 连色度行一起翻转；
- 引擎在 configure 前、start 前、重连、每帧与实时设置块同步；主增强开关"全关"（首次默认状态）时也会立即下发；
- 采集面板新增 checkbox 13（含帮助文本）；`--capture-flip` 仅作诊断/烟测；
- 预设 schema v12→v13（行尾追加 bool；旧 v1–v12 文件仍可读，老版本读到 v13 会拒绝并保留原文件）。

## 3. 验证（main 分支）

- 构建：`cmd.exe /c out\build\veyra-build-x64-release.cmd` exit 0（166/166）。
- `veyra_capture_color_tests.exe`：failures=0；新增方向断言全部 PASS（bottom-up RGB24 读最后一行、top-down 直通、手动翻转对两种输入都反相、NV12 色度行跟随翻转）。
- `veyra_repair_contract_tests.exe`：145 checks 0 failures（含 `capture vertical flip defaults off`）。
- `veyra_repair_preset_tests.exe <tmp>`：48 组 v4–v11 迁移 + 全字段往返（含 v13 翻转字段）通过，exit 0。
- 实卡烟测：`veyra.exe "capture:0:0:-1:0" --smoke-seconds 8 --no-nr --no-sr --no-fg --capture-flip` exit 0、`frames=458`、`failed=false`、`captureDropped=0`，日志 `[capture-flip] manual vertical flip=1`（YUY2 路径翻转后仍稳定）。
- delivery 短测 23/23 PASS，48.69 秒，`logs/delivery/94abadd1321948ad84fb46a1c1920e0a/result.json`，EXE SHA256 `A7AF379D7DD7F6183965CED7E01CA7ED423EE747CBAB28AE514C65E3127B8CFD`（gate 自身仍标 `capture=awaiting_user_capture_test`）。

## 4. 边界与未执行

- 本机没有 RGB24 设备："A 的 top-down 协商在问题卡上是否被驱动接受"须受影响用户实机确认；若被拒绝，用 B 的开关兜底。两个结果都能从 `DIB top-down request` 日志区分。
- 未打包、未发布、未推送。
- 相关但独立：PS5"无法打开视频"回归（隔离分支 1.3.1beta 的 dangling else）**不在 main**；main 原本没有该缺陷，本轮只把同一处 `else` 补了大括号防呆。分支上的两个修复存档在 `checkpoint/ps5-rgb24-fixes-20260916`（提交 `015f8a4`）。
