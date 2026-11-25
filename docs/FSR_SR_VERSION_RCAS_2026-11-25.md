# FSR 超分提供方版本与 RCAS 锐化（2026-11-25）

用户要求给 FSR 超分加上“选哪一代提供方”和“锐化强度”两项，并把实际运行的版本显示在界面上。

- **分支**：`feat/fsr-sr-version-rcas`，基点 `bc59d457`。
- **范围**：只在本机。没有提交、没有暂存、没有合 main、没有 push。
- **分工**：A 段是引擎接线，B1 是预设/会话持久化，B2 是界面、词库和文档（本文记录 B2 之后的状态）。

## 改了什么

1. **显式提供方版本**：FSR 超分可以选择 `自动（优先最新）`、`FSR 3.1`、`FSR 4.x`。设置字段是 `EnhancementSettings::fsrSrVersion`（`enum class FsrSrVersion : uint32_t { Auto=0, Fsr31=1, Fsr4=2 }`）。
2. **界面回读**：播放中实际创建上下文的提供方版本会显示在“FSR 版本”这一行的提示里，例如“实际运行：FSR 4.1.1”。文字来自引擎快照（`EnhanceGraph::fsrSrProviderVersion()`），不是界面自己猜的。
3. **RCAS 锐化**：`fsrSrSharpness`，0 到 1，两位小数，0 为关闭。这是 FSR 提供方自带的那一遍 RCAS（`dispatch.enableSharpening` / `dispatch.sharpness`），不是另加的后处理。

## 版本选择怎么工作

- 加载器用 `ffxQueryDescGetVersions` 报出这台机器装了哪些提供方，名字形如 `4.1.1`。
- `gfx::FfxVersionSelect`（`include/veyra/gfx/FfxVersionSelect.h`）解析这些名字并按（主、次、补丁）从大到小排序：`rankedFfxProviderIndices`。自动模式优先可解析版本，剩下的无名条目排在后面仍然可用。
- 选中某一版时按 `matchesFsrSrVersion` 过滤：`FSR 3.1` 要求主次为 3.1，`FSR 4.x` 要求主版本为 4。
- 创建上下文用 `ffxOverrideVersion` 指定版本 id，创建后再用 `ffxQueryGetProviderVersion` 读回真实提供方并核对。
- **明确的版本不会静默回退**：清单里没有匹配项就直接报错不创建；读回的版本和请求不符就释放该上下文并拒绝。只有自动模式才会在失败后不带版本覆盖再试一次。
- 创建失败留下的半成品上下文一律 `DestroyContext` 释放后再清句柄，不会把泄漏的提供方状态留给下一帧。这一段是既有的占位释放逻辑，本次没有改。

## 界面

- **列表模式**：`qml/Veyra/ProPage.qml`，在“DLSS 模型”之下、超分阶段编辑器之上新增“FSR 版本”和“FSR 锐化”两行，控件名 `list-fsr-sr-version` / `list-fsr-sr-sharpness`。
- **节点模式**：`qml/Veyra/NodePage.qml`，同样两行，控件名 `node-fsr-sr-version` / `node-fsr-sr-sharpness`。
- **何时可见**：`veyra.videoSrQuality === 5` 或者双超分的第二段是 5（FSR）。**是否可用**：`Support.available(veyra, "sr5")`，即 FSR 加载器与 `amd_fidelityfx_upscaler_dx12.dll` 都在。
- 版本下拉的选项标在每次语言切换时重建（`NOTIFY settingsChanged`，不是 `CONSTANT`），所以切换界面语言不会留下旧语言的标签。
- 提供方文字走单独的 `fsrSrProviderChanged`：只在文字真的变了才发信号，避免每秒重排；语言切换由 `settingsChanged` 带一次。

## 持久化

- **预设库**：`PresetStore` v40；`PresetLibrary` v27。
- **会话**：`ChainSessionStore` v22。
- **旁文件**：需要写新字段时保存到 `.fsr-sr` 旁文件，旧文件字节不变，退回旧版构建仍能读原文件。自动版本 + 关闭锐化这种默认组合不会触发 v27，测试里专门检查了这一点。
- 删除超分节点、以及节点图里没有超分节点时，这两项会跟着 `srTarget` / `dlssSrModel` 一起恢复默认（`src/ui/bridge/Chain.cpp`、`src/ui/QmlPlayerBridge_p.h`），避免留下“看不见但可编辑”的 FSR 值。

## 导出

导出用的是开始导出时冻结的整份 `EnhancementSettings`，其中包含这两项，导出图直接按请求的版本建上下文。因此：

- 导出不会把请求的提供方换成另一代，也不会降低锐化。
- 请求的版本在导出设备上不存在时，导出任务按失败处理并给出原因（“增强管线初始化失败，请查看诊断”），而不是写出一个设置看着不一样的文件。
- 预览里 SR 失败时引擎确实有一条 `FSR 4.x -> FSR 3.1` 的恢复路径（`BackendRecovery.h`），但那条路径会同时给出“FSR 4 ML 超分提供方不可用……已改用 FSR 3.1”的提示，不是静默替换。

## 词库

- `python3 -B scripts/i18n/extract.py --check`：3332 条，9 种语言缺译 0，占位符问题 0。
- 新增 5 条：`自动（优先最新）`、`FSR 版本`、`FSR 锐化`、`FSR 内置 RCAS 锐化，0 为关闭。`、`实际运行：FSR %1`。另外补了 A 段引擎里两条原本缺译的提示：`FSR 4 ML 超分提供方不可用……`、`FSR 4 ML 超分未能启用……`。
- `FSR 3.1` 和 `FSR 4.x` 两个选项标签不进词库，和现有的 `DLSS 4 · K` 一样是字面量。

## 本次离线验证

- `tests/unit/FfxVersionSelectTests.cpp`：54 checks，0 failures。
- `tests/unit/PresetLibraryTests.cpp`（含 v27 / session v22 旁文件用例）：687 PASS，0 FAIL。
- 词库检查：见上，0 缺译 0 占位符问题。
- 顺手做的清理：`PresetLibrary::save()` 和 `ChainSessionStore::save()` 里 `else if(upgradeFsrSr)` 那一支永远不可达（`upgradeFsrSr` 的定义就是同一条件），去掉了重复判断，`upgradeFsrSr` 保留给上面的旁文件守卫用。

## 未验证

- **MSVC 构建**：本机没有 Windows 工具链，B2 的 C++/QML 改动没有编译过，只有 `x86_64-w64-mingw32-g++` 对预设库单元测试的交叉编译通过。
- **QML 语法**：没有 `qmllint` / `qmlformat`，只按相邻的“DLSS 模型”行逐项核对属性名和括号配平。
- **实卡**：RX 9070 XT 上的 FSR 4.x 提供方、实际回读文字和 RCAS 画质差异都没有实测。锐化强度对画质的影响也没有和关闭状态做过对比。
- **双超分**：FSR 作为第二段时只按代码路径检查了锐化只作用于最后一段（`EnhanceGraph.cpp` 里首段强制 0），没有实跑。
