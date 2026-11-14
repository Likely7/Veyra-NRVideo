# 在自己电脑上编译测试包

`scripts/build-local-test.ps1` 从当前分支编译 `veyra_qml_ui.exe`，放进一份官方 2.1.5 便携包的副本里，生成一个可以直接运行的测试包。

本分支的代码与官方 v2.1.5 相同，只多了虚拟桌面捕获的改动，所以测试包只替换 **主程序 `veyra_qml_ui.exe` 和 `qml\` 界面文件**。Qt、FFmpeg、NVIDIA/AMD/Intel 运行库、模型、着色器等都沿用官方包里的原文件。

> 这个脚本是在没有 Windows 的环境里写的，还没有实际跑过。哪一步报错，请把最后几十行输出发给我。

## 需要准备

| 项目 | 说明 |
|---|---|
| Windows 10/11 x64 | 至少 20 GB 可用空间（Qt、官方包、编译目录、测试包） |
| [Visual Studio 2022](https://visualstudio.microsoft.com/zh-hans/vs/community/)（Community 免费版即可） | 安装时勾选工作负载 **“使用 C++ 的桌面开发”**，并确认右侧勾上 **MSVC v143**、**Windows 11 SDK**、**用于 Windows 的 C++ CMake 工具** |
| [Git for Windows](https://git-scm.com/download/win) | 用来拉取代码和公开 SDK |
| [Python 3.9+](https://www.python.org/downloads/windows/) | 安装时勾选 “Add python.exe to PATH”；脚本用它安装 aqtinstall 来下载 Qt 6.8.3 |
| NVIDIA Optical Flow SDK | 到 <https://developer.nvidia.com/opticalflow-sdk> 免费注册 NVIDIA 开发者账号后下载（官方包用的是 5.0.7，5.x 都可以），**不用解压** |
| CUDA Toolkit（可选） | 已安装时脚本直接用它的 `cuda.h`；没装时脚本会尝试从 NVIDIA 下载 CUDA 头文件。两者都没有，NVIDIA VFG 补帧在测试包里不可用，其余功能不受影响 |

其余依赖都由脚本自动下载：Qt 6.8.3、DLSS SDK 310.7、nv-codec-headers 13.0、XeSS 3.0.2、FidelityFX 2.3.0 头文件、FFmpeg n9.0.1 头文件，以及官方 2.1.5 便携包。FFmpeg 的导入库直接从官方包里的 DLL 生成，保证编出来的程序和官方包的 FFmpeg 完全配套。

## 步骤

1. 把代码克隆到**不含中文和空格**的路径（MSVC 链接器读不了含中文的路径）：

   ```powershell
   git clone -b feature/virtual-desktop-capture https://github.com/stock-master-zhou/Veyra-NRVideo C:\src\Veyra-NRVideo
   ```

2. 打开 PowerShell，运行脚本（把路径换成你下载的 Optical Flow SDK 压缩包）：

   ```powershell
   cd C:\src\Veyra-NRVideo
   powershell -ExecutionPolicy Bypass -File scripts\build-local-test.ps1 -NvofSdk "$env:USERPROFILE\Downloads\Optical_Flow_SDK_5.0.7.zip"
   ```

   - AMD 显卡加 `-Gpu AMD`，会基于官方 AMD 包。
   - 已经下载过官方包，可以加 `-PackageZip D:\Veyra-2.1.5-NVIDIA-win64-portable.zip` 省去重新下载。
   - 想顺便打成 zip 加 `-Zip`。
   - 所有下载、依赖、编译文件都放在 `C:\veyra-build`（`-WorkDir` 可改，同样要求不含中文和空格），不会改动代码目录。脚本可以重复运行，已下载和已编译的部分会复用；加 `-Clean` 从头重新编译。

3. 完成后运行：

   ```text
   C:\veyra-build\package\Veyra-2.1.5-vdesk-test-NVIDIA\veyra_qml_ui.exe
   ```

   测试包的设置保存在包内的 `runtime_local\` 文件夹，不会影响你原来那份 Veyra。关于页面的版本号显示为 `2.1.5-vdesk-test`。

## 怎么测虚拟桌面捕获

1. 按 Win+Ctrl+D 新建桌面 2，在桌面 2 打开一个持续变化的窗口（游戏、本地播放器）。
2. 按 Win+Ctrl+← 回到桌面 1，打开测试包里的 Veyra → 屏幕捕获 → 类型选“窗口”。
3. 列表里应出现 `[桌面 2] 窗口标题`，选中后开始捕获，画面应持续更新。
4. 再切换桌面、把源窗口拖到别的桌面，捕获都不应中断。

Chrome、Edge 这类浏览器在所在桌面不显示时会暂停绘制，画面会停住，这是浏览器本身的行为，详见 `docs/VIRTUAL_DESKTOP_CAPTURE.md`。

## 测试包里没有的功能

这些功能需要额外的 SDK 或依赖，脚本没有编进去；需要时请用官方包：

- PS5 Remote Play、PC 串流（Moonlight）、Xbox 串流
- ASS 特效字幕的完整渲染（libass）
- Magewell、DeckLink 采集卡的原生接入
- AMD 光流（FidelityFX 1.1.4，需要另行编译其静态库）
- 离线安装 VB-CABLE 虚拟声卡（可用 `-VbCableZip` 传入原版 `VBCABLE_Driver_Pack46.zip` 后编进去）

测试包只供自己测试，请不要再分发：里面带有官方包中的 NVIDIA、AMD、Intel 等第三方运行组件。
