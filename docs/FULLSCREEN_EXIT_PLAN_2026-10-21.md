# 全屏退出按钮和 Esc 修复

用户反馈 2.1.0 全屏后顶部按钮和 Esc 均无法退出。存档 checkpoint/pre-fullscreen-exit-20261021，起点 4ae285b，隔离分支 codex/fullscreen-exit-20261021；产物 E:/项目/Veyra/{tests,tmp,logs,reports,archives}/fullscreen-exit-20261021。

## 已复现及来源

- 发布包私有副本、独立配置、GTA4K 原视频，真实鼠标点击顶部按钮无状态变化；toggleMaximized 在 fullscreen 直接 return，按钮仍显示“最大化”。该代码来自 4fbc2b0（10月1日），不是最近 UI 软件绘制修复新增。
- 真实 Escape 无效而 F11 正常。仅测试副本加 activatedAmbiguously 日志证实 Esc 冲突；单独给播放列表 Escape 加 visible && active 后，真实 Escape 成功退出。播放列表未限定的 Escape 来自 e6ead9f（10月9日），隐藏工具窗口也参与匹配。
- 同查发现节点页有两条相同连线 Escape 和一条多选 Escape；合并为单一可见状态动作，避免连线/多选再次冲突。专业保护绘制 Esc 与全屏动作也需互斥。

## 施工范围

仅 Main/PlaylistWindow/NodePage/ProPage QML 窗口和快捷键接点、必要回归及文档。顶部按钮在全屏走既有 toggleFullscreen 恢复原窗口状态，提示显示还原。列表 Esc/Delete/Return 只在列表可见且激活时启用。全屏 Esc 统一调度，先取消可见的保护绘制或节点操作、游戏模式先收起面板，其余退出；隐藏页不抢键。

不改 GPU/软件 UI 默认、不加刷新 Timer/requestUpdate、不改33ms/250ms或视频/增强/补帧/驱动/运行库，不触碰用户配置/其他工作树；无子 Agent、main合并/push/Release/关机。

## 验证

使用 computer-use skill 原生输入复现和回归 GPU/软件界面的极简/专业/节点全屏按钮、Esc、锁定、窗口/最大化恢复，保留暂停状态；验证隐藏和激活播放列表的键归属及节点取消。测试副本隔离，不修改原发布目录。定向 Qt Quick 测试与既有组件/布局检查、构建检查；单测试≤300秒，构建≤900秒限并发。报告真实通过及未覆盖项，不把短测当性能或全硬件验收。
