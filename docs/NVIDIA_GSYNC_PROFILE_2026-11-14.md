# NVIDIA G-SYNC 程序配置

## 为什么需要这个入口

播放器的“可变刷新”开关控制呈现策略，不会替用户保存 NVIDIA 驱动配置。
全局 G-SYNC 已选择窗口和全屏时，程序专用配置仍可能覆盖为“仅全屏”。
Veyra 的全屏是无边框窗口；因此窗口和全屏都可能仍以屏幕最高刷新率运行。
程序级的这两项细分范围没有在普通控制面板中完整展示，用户通常需要额外使用 Profile Inspector。

一次 Win10 / RTX 4070 SUPER / GS27QTS 280 Hz 的对照中，全局两项为 2，Veyra 专用两项为 1。
只把专用范围改为 2 并手动重启，窗口和全屏的显示器实时刷新计数都开始变化，日志出现
`requested=true、active=true`。播放器代码与同步策略未变。该结果支持增加程序配置入口，
不应据此归因为 Windows 10 或无边框呈现本身不支持 VRR。

## 使用方法

在 **专业 → 显示 → 可变刷新** 下方找到 **NVIDIA G-SYNC 配置**：

- **检查**：读取当前 EXE 完整路径的驱动配置，不写入驱动。
- **修复**：备份原状态，只为当前路径允许窗口和全屏 G-SYNC。保存后手动重启播放器。
- **恢复原设置**：存在有效恢复记录时可用；恢复已完成但记录清理失败时也可再次点击清理。

“已允许窗口和全屏”只表示配置已保存，实际激活仍需看驱动与显示器报告。
显示器及 NVIDIA 控制面板的 G-SYNC 总开关、监视器技术等设置仍需正确开启。
普通“可变刷新”开关继续保留原来的呈现设置语义。

相同路径覆盖更新通常保留驱动关联；换目录后，检查和修复使用当前新路径。
旧路径、其他程序、全局／基础／预定义／共享配置不会自动修改。
仅按 EXE 文件名匹配的配置不自动写入，应按提示手动处理。

## 手动配置

1. 从 [原作者发布页](https://github.com/Orbmu2k/nvidiaProfileInspector/releases) 获取 Profile Inspector，按其要求以管理员身份启动。
2. 选择关联当前 `veyra_qml_ui.exe` 的 Veyra 专用配置；新路径没有配置时，为它单独建立配置并添加当前 EXE。
3. 在该 Veyra 配置内，将 `GSYNC - Application Mode` 与 `GSYNC - Global Mode` 都设为 **Fullscreen and Windowed**。
4. 点击 **Apply Changes** 并手动重启 Veyra。名称带 Global Mode 的条目也可保存在程序配置中，操作时仍选 Veyra 专用配置。

## 写入与恢复边界

只调整 `VRR_MODE (0x1194F158)` 和 `VRRREQUESTSTATE (0x1094F1F7)` 两个 DWORD：
1 为仅全屏，2 为窗口和全屏。没有关联时建立专用完整路径配置；已有配置须为精确匹配的自定义单程序配置。
写入前重新核对快照，使用一个 DRS 会话保存，随后新会话回读，并核对全局两项未变。

原状态原子保存到软件数据目录 `driver-profiles/vrr-<路径摘要>.json`，不覆盖已有原始记录。
恢复原来继承的值时删除覆盖项，避免把旧全局数值写死；检测到用户独立修改则停止。
本功能创建且没有新增其他设置的配置可以整体恢复删除；否则只恢复两项，保留其他设置。
恢复记录与写入由文件锁串行化，失败时保留记录，不自动重试写入。

所有操作由显式点击触发，在后台执行，不自动切换窗口或重启，不在启动或稳定播放时轮询驱动。
权限不足时提示用户在配置时以管理员身份重试，不改变播放器启动权限。
通过系统 `nvapi64.dll` 工作，不随包新增 SDK、Python、Profile Inspector 或驱动 DLL。
该入口不改变 NR/SR/FG、录制、HDR、同步、限帧和 GPU 优先级逻辑。

## 独立验证

`tests/vrr-profile` 可脱离媒体／AI 开发依赖，单独编译真实驱动适配器、Qt 控制器及界面测试。
策略测试默认不调用驱动；控制器测试通过链接期模拟替代三个驱动入口，不读写真实配置。
界面测试沿用真实 `VRow`、`VButton`、`Theme` 和翻译目录，覆盖四语及 280/360/520 像素宽度。

在带 MSVC 2022 与 Qt 6.8.3 的开发环境或 GitHub Actions 中运行：

```powershell
cmake -S tests/vrr-profile -B out/vrr-profile -G "Visual Studio 17 2022" -A x64
cmake --build out/vrr-profile --config Release --parallel 2
ctest --test-dir out/vrr-profile -C Release --output-on-failure --timeout 120
python -X utf8 -B scripts/i18n/extract.py --check
```

这套检查不替代完整播放器构建、实际驱动写入／恢复、GPU 性能或光学延迟验收。
