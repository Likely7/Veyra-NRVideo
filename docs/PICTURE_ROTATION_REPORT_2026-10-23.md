# 画面旋转实现与本地验收

## 行为和实现

基于本对话上一轮 AMD 超分关闭/录制修复源码4eee190，分支codex/picture-rotation-20261023。起点存档checkpoint/pre-picture-rotation-20261023，旧分支、其它工作树、main和公开版本保持。

极简/专业播放条增加28px顺时针图标按钮，全屏复用极简播放条，每次90°，四次恢复；未开片源禁用。共享RotateButton及同一个桥接动作。手动方向是会话级状态，和容器方向叠加；不修改原文件，不保存到预设。切换模式、全屏、暂停、seek保留方向，新开片源清零。操作在引擎锁内检查会话ID，旧源的延迟点击不能旋转新源。

PreviewView已有的GPU呈现/坐标映射支持旋转，增加独立userQuarterTurns，供呈现/鼠标源坐标/字幕边界共用。源DAR只包含容器方向，手动偏移单独倒置；native及固定比例均适配。旋转时回到居中100%并取消未完成的保护选区。原始处理纹理/估计运动/增强历史不旋转，XeSS沿用已有方向变更重置和运动映射。

极简窗口高度/宽度按旋转后的比例适配并限制屏幕边界。修复MinimalPage原先在高度变化时套回源比例的反馈，避免旋转后反复跳回横屏。播放条左区允许缩小以容纳新按钮；专业模式预览区正确容纳画面，控件与菜单保持正向。游戏模式手动角度非零时沿用已有Scaled输入策略：键盘/手柄留给源窗口，阻止错误鼠标穿透。

**作用范围是播放视图。** 图片截图、视频录制与离线导出继续使用原方向的处理纹理；用户本次没有要求旋转输出文件。本地包说明明确此项，不把视图方向冒充导出能力。

## 构建和验证

所有产物在E:/项目/Veyra/{logs,tests,tmp,reports,test-packages,archives}/codex-picture-rotation-20261023。复用同源路径构建build/claude-screen-recording-20261022，没有修改其它源树、运行组件、模型、驱动或真实用户配置，无子Agent。构建命令：

    python -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-picture-rotation-20261023 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.0-rec veyra_qml_ui veyra_preview_geometry_tests veyra_qml_quick_tests veyra_ui_contract_tests veyra_ui_i18n_tests veyra_repair_contract_tests

- -j2，45.4秒通过，日志logs/.../build-20261023-064045；EXE SHA256 92d0df8e36075581d630b36946dc8bf8312770a6129915e43372313f3a80785b。内部版本沿用2.1.0-rec，本地包外部标识rot1。
- UI contract通过：既有384布局组合，新加4种容器角度×7显示模式×4次手动旋转，共112组比例/坐标/滚轮锚定状态，四次恢复及重置缩放检查。
- repair 272检查0失败；UI i18n 0失败；catalog2660条，三种译文零缺译/占位符问题。
- QtQuick109通过0失败（contracts-064254）：新增共享按钮的真实Qt鼠标事件，12次交替点击与状态同步、无片源禁用。真实播放器夹具调用按钮生产clicked信号，不冒充物理鼠标验收。
- 实际D3D12呈现像素23例通过（gpu-contracts-064445）：包括manual90/180/270/360、容器90+手动90、原图对照、缩放、原像素、固定比例、窗口尺寸保留。像素PNG在同目录pixels，manual90.png已人工查看。
- 真实播放器回归：wide-zh-CN-064450、metadata-zh-CN-064617（ffprobe核验显示矩阵，程序顺时针270°）、portrait-zh-TW-064718、wide-software-en-064852均通过。覆盖两按钮及全屏复用、暂停/seek、模式往返、横竖窗口适配、全部显示比例、重新打开清零；软件界面由--obs-game-capture及software UI日志确认。
- 日文record-ja-065008：旋转和布局通过，实际NVENC H.264录制49.5秒、1380帧、零丢帧、正常收尾。原统计读取fragmented MP4中缺失的nb_frames误报0，失败report.json保留；改用ffprobe -count_frames解码计数，独立reports/.../recording-recheck.json通过。文件640×360，录制方向未随预览改变。

## 失败记录和边界

首个旋转元数据夹具用FFmpeg8的metadata rotate未写出display matrix，探测阶段失败，未计入通过；改用其本机帮助确认的-display_rotation输入选项重新生成并ffprobe核验。旧无矩阵文件保留。

AMD核显真实D3D12 +150% DPI：wide-amd-zh-CN-065142的旋转/布局断言通过，但seek/重新打开有decoder send_packet -22并回退软件，整体严格门槛判失败。未改动的rec2原EXE、原QML、相同片源与AMD/150%配置，在baseline-amd-065446复现两次同错误；其录制EXE身份由原包清单记录。不能将此称为本轮旋转新引入，也不能称为已修复。单列为已有AMD硬解seek问题，不在本轮改解码器。

进一步用同一AMD核显、150% DPI及明确软件解码隔离验证旋转，wide-amd-swdecode-zh-CN-065527全部通过，零错误；日志明确device context initialized adapter=AMD Radeon(TM) Graphics。没有验证AMD独显/RX9000、其它NVIDIA型号、Intel、真实采集卡/主机串流、HDR、跨屏DPI切换、真实游戏鼠标策略、光学显示延迟或长时间稳定性。既有DLSS SR首次新缓存JIT问题沿用上一轮报告，不作新通过声明。

本轮不合main、不push、不发布；本地包保留可用目录和ZIP，并按逐文件清单核验运行库/模型/着色器与rec2供体原样一致。命令scripts/acceptance/package-picture-rotation.py，准确源码提交、ZIP大小/SHA、文件计数以reports/.../packages.json为准，后续只追加文档回执不改变包内源码身份。
