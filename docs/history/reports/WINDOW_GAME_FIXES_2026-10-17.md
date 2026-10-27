# 窗口捕获游戏模式修正（2026-10-17）

用户要求取消进入穿透前的等待，修正窗口捕获拉伸、退出穿透快捷键与源窗口缩放跟随；任务栏残留暂缓，用户正在远程操作。

基线 `d68d6be9c37127350698a1d4d47fc01b36b29475`，分支 `codex/window-game-fixes-20261017`，隔离工作树 `/workspace/veyra-window-fixes`。原 `/workspace/Veyra-NRVideo` 的 main 和文件不变。本轮为 Linux 云环境源码修改，没有连接用户 Windows、改变真实偏好、运行库、模型或驱动。未打包、合并 main、推送或发布。

## 进入与退出

主线没有固定的 3 秒入口倒计时；原来穿透需要等待引擎 running（包含增强初始化），从首页还会等待 820ms 页面动画。本轮游戏模式绕过动画等待，在捕获会话已创建、全屏且键盘监听就绪时立即接管输入，增强继续异步初始化。不承诺视频首帧或模型初始化耗时为零。

保留原有 3 秒 GUI 卡死保护，它是异常恢复机制。停止/失败的 Opening 会话也释放输入。

全局键盘钩子投递到生命周期稳定的 message-only HWND，不再依赖 QML 主窗口 HWND。检测投递失败后恢复普通输入。Qt 平台窗口创建/替换时更新控制器绑定；看门狗通过原子 HWND 读取目标。

退出动作先由控制器清除穿透/不激活状态，再通知 QML 退出全屏。QML 不再因 fullscreen 状态已变化而忽略退出动作。快捷键沿用用户配置。

## 显示与输入

- 捕获对话框“贴合原窗口”：画面按源窗口原尺寸和位置显示，支持真实鼠标精确穿透。
- “填满屏幕”及后续面板的拉伸、原生像素、固定比例选择真正应用到显示器大小的视频区域，不再被游戏模式强制改为模式 0。
- 全屏缩放时，源窗口继续获得键盘/手柄焦点，鼠标点击由宿主拦截，避免放大画面上的位置误点到未放大的源窗口。对话框及首次进入缩放状态提示此限制；切回适应模式恢复原窗口对齐与精确穿透。
- “填满”沿用模式 2（保比例覆盖，可能裁边），面板“拉伸”沿用模式 3（两个方向填充），二者不混用。
- 新捕获以对话框选择为初始显示方式，不继承上一个影片的拉伸状态；之后的面板选择可替换该会话设置。

本轮**没有实现任意缩放窗口的绝对鼠标坐标映射**，没有向游戏注入或重发鼠标消息。需要鼠标操作的窗口游戏使用贴合原窗口，或让游戏本身使用无边框全屏。真实键盘/手柄兼容性仍需实机验收。

## 源窗口缩放

WGC 交付新尺寸帧时同时更新 SourceInfo 的 width、height、displayAspect 和 Resize 标记；引擎重建处理图时同步更新屏幕捕获的 UI 快照源尺寸/宽高比。显示比例缓存增加 DAR，避免固定输出尺寸未变时继续沿用旧比例。

保留 300ms 尺寸稳定等待和 250ms 几何心跳，不增加逐帧 GPU 等待或提高 UI 轮询频率。拖拽期间仍可能按旧尺寸裁切/补边；停止拖拽后更新实际采集尺寸，模型资源重建耗时需实测。

任务栏 Shell 标记与原 fullscreen 实现未修改。显示器大小的视频区域属于缩放所需布局，不代表本轮修复或验收了任务栏问题。

## 修改范围

`GameModePolicy.h` 封装无平台依赖的输入状态规则；`GameHotkeys.h`、`GamePassthrough.h` 管理接收端、失败恢复、平台窗口重绑定和缩放输入模式；`QmlPlayerBridge.h/.cpp` 修改入口、退出、显示方式和比例缓存。`ScreenCaptureSource.cpp`、`EngineController.cpp` 补齐尺寸元数据传播；`Main.qml`、`DialogHost.qml` 与 catalog 同步输入状态和提示。CMake 与三个测试文件加入针对性回归。

## 本轮验证

回执目录 `/workspace/scratch/veyra-window-checks`，不引用历史 Windows 测试作为本轮通过证据。

| 检查 | 结果 |
| --- | --- |
| Linux C++20 编译运行 GameModePolicyTests | 44 项通过，覆盖 Opening、退出/失败、面板、缩放输入及实际 PreviewView 几何 |
| Qt 6.8.3 qmlcachegen：Main / DialogHost | 字节码与 basic-block 验证通过 |
| `python -B scripts/i18n/extract.py --check` | 2428 条，繁中/英/日缺失均 0，占位符问题 0 |
| `git diff --check` | 通过 |
| MinGW-w64 GCC14 Windows 目标语法检查 GamePassthroughTests | 通过，包含真实热键/穿透头文件 |
| 同一翻译单元的 Windows 对象编译 | 通过，生成 x86-64 COFF；命令/退出码见 windows-object-command.json |
| 完整产品 Windows/MSVC 构建与链接 | 未执行 |
| Windows WGC/GPU/实际 QML bridge/真实游戏运行 | 未执行 |

实际便携检查命令：

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Wno-misleading-indentation -I include tests/unit/GameModePolicyTests.cpp -o /workspace/scratch/veyra-window-checks/game-mode-policy-tests
/workspace/scratch/veyra-window-checks/game-mode-policy-tests
python -B scripts/i18n/extract.py --check
git diff --check
```

Qt 命令为 `/workspace/shared/veyra-qt-tools/PySide6/Qt/libexec/qmlcachegen --only-bytecode --validate-basic-blocks -o <scratch输出> <QML文件>`，两个文件分别执行，命令及退出码在 checks.json。

Linux 原生 CMake 配置已尝试，在 project 阶段因缺少 RC 编译器停止，没有记作产品构建成功。交叉工具准备时 apt 索引不含所需包，改用公开 Debian 工具包并核对 SHA；Qt 使用公开 6.8.2 头文件及对应上游 Windows 公共头。工具仅在 `/workspace/shared/veyra-cross-check`，不进入源码 Git。首次对象编译因缺少 Windows 汇编器调用了 Linux 汇编器，已保存失败日志并补齐工具；最终对象编译结果单列回执。这些检查不替代 MSVC、Windows Qt 链接或运行。

## Windows 待执行验收

新增但本轮未运行的回归：

- `veyra_game_passthrough_tests`：保留原发键用例，增加真实 QQuickWindow 全屏/穿透标志切换、面板恢复、源窗口缩放、缩放输入样式、强制平台窗口替换后的退出组合键与原生样式恢复。此测试验证 Qt 宿主和 GamePassthrough，不等同于完整播放器端到端验收。
- `veyra_screen_capture_tests`：源宽高比更新、横向转纵向的 Resize 标记和真实 RGB 内容；`--engine` 再验证增强运行时的 UI 尺寸/比例快照与持续帧输出。

在隔离 Windows 构建目录构建 `veyra_qml_ui`、上述集成测试与 `veyra_game_mode_policy_tests`，每测试不超过 300 秒、每构建不超过 900 秒。实际播放器连续操作“捕获 A → 全屏 → 面板键 → 调整 A 大小 → 关闭面板 → 退出键”，分别检查原尺寸鼠标落点、填满/拉伸输出、帧号持续更新和退出后原生输入样式；普通窗口、独立 Raw Input 程序、真实窗口化/无边框游戏分开记录。显示器捕获作为对照。

远程测试需确认组合键送达目标机：部分远程客户端截获 Ctrl+Alt+Home；未转发的按键无法被目标机钩子接收，此时使用已有设置换键。混合 DPI、跨显示器、不同权限和真实硬件输入尚未验收。

下一条唯一任务：Windows 隔离构建及上述连续流程实测，按失败证据定点修正；通过前不生成“已验收”发布包。
