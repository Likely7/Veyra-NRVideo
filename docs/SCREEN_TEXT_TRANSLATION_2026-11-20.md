# 实时翻译：画面文字来源（采集卡 / PS5 字幕 OCR）

实时翻译新增第二个“字幕来源”选项。原有来源是 Windows 实时字幕（识别声音）；新来源用 Windows 文字识别（Windows.Media.Ocr）读取**画面上绘制的字幕**，适用于采集卡、PS5 远程游玩、Xbox/电脑串流和视频。识别逻辑移植自 [VI Translator - Desktop](https://github.com/vi-translator/vi-translator-desktop) v1.2.3（MIT，`715f04a8`），许可与移植范围见 THIRD_PARTY_NOTICES.md。默认来源不变：有 Windows 实时字幕时仍为“Windows 实时字幕”，只有 Windows 10（没有实时字幕）默认选画面文字。

## 用法

设置 → 实时翻译设置… → 字幕来源选“画面文字（采集卡 / PS5 字幕）”→ 打开“在画面上显示实时翻译”。识别区域默认“底部字幕”（画面 x 8–92%、y 70–97%，只保留居中的字幕，丢掉两侧 HUD/菜单文字）；另有底部整行、顶部字幕、整个画面（每 600 ms 读一次）和自定义区域（四个滑块）。原文语言“自动识别原文”按英语识别；选其他语言时需在 Windows 设置中安装该语言，否则状态栏显示缺少哪种 OCR 语言。翻译引擎、目标语言、同时显示原文、字幕样式均沿用原有设置，译文仍由原有字幕叠加窗口绘制在 Veyra 预览画面内。

## 实现与边界

- `ScreenText.h/.cpp`（可移植，无 Windows/D3D12）：VI Translator 的三遍 OCR（最亮通道 / 白色或彩色 / 仅描边内字母）及取舍、垃圾词与质量检查、词序检查、补洞、居中字幕过滤、丢失撇号修复、“像不像英文”检查、编辑距离判同一行、稳定 300 ms（不确定 ×3）/ 消失 800 ms 后清除。Veyra 另加：高于中位字高两倍的“词”视为场景误读并丢弃。GTA VI 专用部分（说话人名、按钮图标、高亮颜色、游戏页面、短语库、同步服务器）未移植。
- `ScreenTextCapture`（`PresentCapture`，与虚拟摄像机并列挂在 `captures_` 扇出上）：仅启用时工作。按间隔（默认 250 ms）把呈现帧的识别区域 `CopyTextureRegion` 到 2 个回读缓冲之一，转换屏障 COMMON→COPY_SOURCE→COMMON；`record()` 不等待 GPU、工作线程或锁（`try_lock`），生成帧（插帧）跳过，工作线程正在识别时跳过。工作线程（MTA）在栅栏完成后 Map、转 RGBA8（SDR 原样；scRGB 与 HDR10 PQ 以 250 nit 为白；宽于 1920 的区域整数倍平均缩小），再识别。缓冲只在最后一次拷贝完成后释放或替换；析构时等待栅栏，超时则保留缓冲不释放。日志只记录次数与耗时，不记录画面文字。
- 换源/停止通过 epoch 丢弃在途读取并立即清除当前行。`LiveTranslation` 新增 `captionSource / screenRegion / customRegion`，画面来源不启动实时字幕读取进程，也不套用 6.5 s 字幕寿命；最近 300 条译文缓存，重复出现的游戏台词不重复请求。

## 本次验证（Linux 沙箱，未在 Windows 构建）

| 检查 | 结果 |
|---|---|
| `tests/unit/ScreenTextTests.cpp`（g++ 13 ASan/UBSan、clang，`-Wall -Wextra -Wshadow -Wconversion` 无警告） | 50 项通过 |
| 端到端：合成 1920×1080 帧（噪声场景、亮天空背景、两行说话人、右下角 HUD “Browse”）→ 底部字幕裁剪 → 移植的三遍流程（以 Tesseract 代替 Windows OCR）→ 居中过滤 → 时序 | 4 帧全部正确：字幕读出、HUD 被排除、两行说话人保留、无字幕帧清空 |
| `ScreenTextCapture.cpp` 以替身 Windows/D3D12/WinRT 头编译，驱动真实 record/submitted/worker：假 GPU 栅栏、假回读、假 OCR | 线程/栅栏/槽位/epoch/析构 13 项通过（ASan 无泄漏）；SDR/BGRA/scRGB/PQ/4K 缩小转换 7 项通过 |

**未验证**：MSVC 构建（`veyra_engine`、`veyra_qml_bridge`、`veyra_screen_text_tests`、`veyra_live_translation_tests`、QML 组件测试）、真实 Windows OCR 与真实 PS5/采集卡画面上的识别率和耗时。替身头只能检查本仓库代码本身的语法与逻辑，不能证明真实 SDK 签名完全一致。合并前请在 Windows 上构建并运行上述目标。
