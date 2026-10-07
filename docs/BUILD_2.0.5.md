# Veyra 2.0.5 本地测试包对应源码

应用源码为同目录 `Veyra-2.0.5-source.zip`；源码提交与SHA见 `DELIVERY.json` / 包内manifest。本次只更新Veyra版本和整合已完成修复，外部依赖未替换。

沿用2.0.4已核验的 `Veyra-2.0.4-dependency-source.zip`，SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`，本机位于 `E:/项目/Veyra/releases/publish-2.0.4-20261006/`。依赖的固定提交、许可证及构建材料见 [2.0.4构建说明](BUILD_2.0.4.md) 与相应source manifests；不要将2.0.4应用源码误当本次应用源码。

Visual Studio2022/MSVC x64、C++20、CMake/Ninja、Python3.11、Qt6.8.3。保留FFmpeg PS5 H264 slice补丁和静态libass/字体依赖；RemotePlay、Moonlight、Xbox及必要公开依赖路径沿用固定缓存。配置 `-DVEYRA_DISPLAY_VERSION=2.0.5`，项目/PE数字版本也为2.0.5。构建 `veyra_qml_ui`，按本轮验收记录执行实际QML入口定向测试。

本机复现脚本 `scripts/acceptance/release-2.0.5-build.py` 从已审计 `build/list-preset-flow-20261006/CMakeCache.txt` 提取外置依赖到新任务构建目录，不重用旧编译输出。其他机器应显式配置自身外置路径，不能照搬本机绝对路径。新工作树/不可变基线/测试输出按本轮PLAN设置；脚本不适用于脱离这些前提的任意目录。

同EXE/QML/shaders分别组合已批准NVIDIA/AMD组件；不将SDK、专有运行库或模型进入源码Git/源码ZIP。不改proprietary DLL；Qt动态链接可替换，静态LGPL组件可按提供源码重编译/重链接。此次仅本地测试交付，未创建公开2.0.5 Release。
