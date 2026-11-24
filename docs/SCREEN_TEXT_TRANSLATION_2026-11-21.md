# 实时翻译：画面字幕的译文显示与本地模型提速（2026-11-21）

## 反馈

用户在 PS5 采集卡画面上使用画面文字来源：人物连续说多句时只有偶尔一句有译文，后来几乎不出现译文。日志显示 OCR 正常（约 3.8 次/秒、每次约 45 ms）。原因在翻译与显示：本地 Qwen2.5 1.5B 只在 CPU 上运行（`--n-gpu-layers 0`，线程为核心数的 1/4），一句需要数秒；译文回来时原句已离开画面，旧逻辑只显示仍在画面上的那句的译文，迟到的译文只进缓存，于是大多数句子从未显示。

## 修改

- `src/ui/CaptionPresenter.h`（可移植，无 Qt）：移植 VI Translator 的 presenter（MIT，`src-tauri/src/main.rs` v1.2.3）。每句译文显示的时长等于原句在画面上的时长，并整体顺延翻译延迟；原句离开后才到的译文照样显示；下一句译文等当前句显示够时间后再换上，只保留最新一句等待，慢的翻译器不会积压；译文最多比原句晚 3 秒结束。Veyra 另加：每句至少显示 1.2–3.5 秒（按字数），离开画面超过 8 秒的句子丢弃。
- `LiveTranslation`（画面来源）：每句编号；一次只发一个请求，请求的总是画面上当前那句，翻译期间被替换掉的句子跳过；在途请求不中止，回来后交给 presenter 显示。Windows 实时字幕来源的行为不变。
- 本地模型改用 llama.cpp b12240 的 **Vulkan** 包（`llama-b12240-bin-win-vulkan-x64.zip`，37,892,170 字节，SHA-256 `9ea8f113…a8ac7`）。与 CPU 包相比，共用的 23 个文件逐字节相同（SHA-256 一致），只多 `ggml-vulkan.dll`（`345812d6…4522d`）。默认 `--n-gpu-layers 99` 整个模型放到显卡；显卡上的进程退出或 45 秒未就绪时自动改用 CPU；面板新增“用显卡运行本地模型”开关。CPU 线程改为核心数的 1/2（2–8）。运行库目录改为 `llama-b12240-vulkan`，旧的纯 CPU 运行库不会被误认，模型文件无需重新下载。
- MyMemory 当日免费额度用完（`responseStatus` 429 / `quotaFinished` / “MYMEMORY WARNING” 文本）时，状态栏明确提示换用其他引擎。
- 居中字幕过滤：横跨画面中线的一整块文字在偏离中心 15% 以内也算字幕（OCR 漏读一端几个词、或游戏字幕不严格居中时不再整句丢失）；VI Translator 原有的 4% 规则与两侧面板规则保留。

## 验证

| 检查 | 结果 |
|---|---|
| `tests/unit/CaptionPresenterTests.cpp`（g++ 13，`-Wall -Wextra`） | 26 项通过（含 VI Translator 原有 6 组用例） |
| `tests/unit/ScreenTextTests.cpp` | 52 项通过（新增 2 项居中用例） |
| Vulkan 包解压后逐文件 SHA-256 | 24 个文件，与 `TranslationAssets.cpp` 中固定值一致 |

`veyra_live_translation_tests`（迟到译文仍显示、第二句接着显示、繁忙时跳到当前句）与 MSVC 构建在 Windows CI 上运行，结果见发布说明；真实显卡上的翻译耗时需用户机器确认。
