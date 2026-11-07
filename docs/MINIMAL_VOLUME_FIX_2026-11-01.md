# 极简播放条音量拖动和数值确认修复
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户反馈：音量难拖；打开数值输入后，回车及点击空白处不能确认。

在当前 `claude/vsync-jitter-wait-20261102` 未提交工作区继续，仅修改 QML/相关界面测试，不提交、不合并 main、不推送或发布。此前补帧修改保留。

## 原因与处理

- 极简和全屏共用独立 `FullscreenBar`。原窗口设置 `WindowDoesNotAcceptFocus`，输入框的 QML 焦点不能代替 Windows 键盘焦点。改为固定可激活的窗口标志；音量按住/编辑时保留焦点，结束后请求回到主窗口；外部窗口激活或播放条隐藏时提交编辑。全屏编辑期间不自动隐藏。
- `VSlider` 原 `activeTranslation` 在拖动激活时从零开始，跨过阈值的第一下会被漏掉，单次快速移动可完全不改变值。现在按下时保存起始值，按当前位置减按下位置计算完整位移，并在激活时立即应用；停用后的位移复位不写值。
- 滑块两端凸出轨道，原输入区只含轨道，100% 中心及外侧不能完整命中。音量专用 `fullKnobHitArea` 包含完整滑块；扩大音量区域至 104×32、保证轨道宽度。密集参数面板仍用原命中边界。
- 音量按下/拖动/编辑时禁止播放条的窗口移动手势抢占；保留空白区域拖动窗口。音量跟随模型更新，拖动期间用草稿值显示。
- 内嵌数值框接收快捷键覆盖，主回车/数字键盘 Enter 确认、Esc 取消；被动观察窗口内外部按下，空白也能提交且不吃下一控件点击。关闭清理隐藏输入焦点，转到其他输入时不抢焦点。

## 验证与纠错

复用 `E:/项目/Veyra/build/claude-screen-recording-20261022` 和 `E:/项目/Veyra/tests/claude-sr-stage-20261028/app`，独立配置在 `tests/codex-cinema-volume-20261101/profile`。原文件保全在 `archives/codex-cinema-volume-20261101`。

构建：`python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-cinema-volume-20261101 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.1-test --jobs 2 veyra_qml_ui veyra_qml_quick_tests`，最终增量 0.7 秒退出 0，日志 `logs/codex-cinema-volume-20261101/build-20261101-235611`。

QtQuick 最终 **132 passed / 0 failed / 1 existing skipped**，33.4 秒，日志 `quick-complete-results.txt`。覆盖共享组件、拖动、模型绑定、输入、普通/数字键盘回车、空白提交、下一输入框焦点、Esc、快捷键恢复、0%/100% 两端拖动、单次快速拖动、空白移动窗口，以及 NR 等原参数控件。翻译检查 2718 条、缺失/占位符问题 0；diff 检查通过。

实际 Windows 窗口用 computer-use 操作、真实本地视频、独立配置验证：从 100% 端点单次快速拖至约 21.95%；输入 63 回车应用为 0.63；输入 41 点击播放条空白应用为 0.41。界面观察和桥接模型日志一致，编辑后隐藏焦点清除，播放条完整显示，窗口未随音量拖动移动。观察探针只注入运行目录，完成后四个 QML 逐字节恢复到产品源码；没有在产品引入定时诊断刷新。此处验证交互和引擎参数，不是声学音量测量。全屏使用同一控件且隐藏条件已修正，本轮人工操作在窗口化极简模式。

过程失败如实保留：

1. 初版随编辑动态改变 `WindowDoesNotAcceptFocus`，实际软件渲染悬浮条内容丢失；撤回动态标志，保持原生窗口标志稳定。该原型的本轮测试 PID 14532 经 EXE/独立 data-dir 核对后停止，未停止用户进程；其余用例退出记录见各日志。
2. 初次 QtQuick 使用了构建目录中的旧测试夹具，只证明原 123 项回归；复制新夹具后另行验证，没有把旧测试数当新增测试通过。
3. 第一轮完整新用例 130/2/1：快速单次拖动仍漏位移，改用 centroid 的按下/当前位置；NR 模式选择被扩大的公共命中范围影响，对照原组件和去掉 padding 都通过，故命中扩展改为仅音量 opt-in。最终完整 132/0/1。

本轮未改音频后端、补帧、GPU 设置、驱动或运行 DLL。原真实配置 7 文件、97 个默认运行组件的保护核验，以及完整文件 SHA、空间和证据在 `E:/项目/Veyra/reports/codex-cinema-volume-20261101/FINAL.json`。只保留小型测试缓存以复用，没有复制完整运行包或声称物理空间回收。

继续测试可使用原 `E:/项目/Veyra/tests/claude-fg-60hz-pacing-20261031/启动补帧刷新率测试.cmd`，其运行目录已更新。
