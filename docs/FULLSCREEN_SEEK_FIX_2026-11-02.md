# 全屏进度条拖动修复

用户2026-11-02反馈全屏不能拖动进度条。隔离分支 `codex/fullscreen-seek-20261102`，基线 `fedd7fc`，仅修共享播放条和全屏隐藏条件；不推送、合并或替换GitHub发布资产。

## 根因与改动

11月1日音量焦点修复让 `FullscreenBar` 在不交互时交还主窗口焦点，但 `CineBar.interacting` 只包括音量按下/编辑，遗漏进度条。真实Windows复现：按下进度条后7ms收到 `MouseArea.onCanceled`，没有release或seek提交。不是视频解码欠速，也不是全屏禁止拖动。

- 把进度条scrubbing计入interacting；拖动过程中不交还主窗口焦点，松开后按原规则交还。
- 窗口化播放条移动手势在任何交互期间禁用，保持空白区域原移动行为。
- 拖出轨道仍保留缩略图及窗口mask；全屏自动隐藏等待完整交互结束。
- 取消拖动、片源切换导致进度条禁用时停止120ms seek定时器并清理scrubbing，不提交过期跳转。
- 不修改窗口flags、动画、显示调度、音量算法、引擎或驱动。

## 构建和回归

复用 `E:/项目/Veyra/build/claude-screen-recording-20261022`，命令：

```powershell
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-fullscreen-seek-20261102 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --label 2.1.2 --jobs 2 veyra_qml_ui veyra_qml_quick_tests
```

两次增量构建通过，2.2s与最终0.7s。最终回执 `logs/codex-fullscreen-seek-20261102/build-20261102-210225/receipt.json`。

新增QtQuick用例驱动实际CineBar：全屏/窗口化拖动不移动窗口、点击跳转、拖出边界钳位及预览、采集不可seek、拖动途中禁用与取消清理。新夹具在旧源码运行出现5项失败；修复后完整套件 **141 passed / 0 failed / 0 skipped**，35.819s。既有音量与输入测试也在本轮完整套件中。测试从可执行文件旁的独立qml-tests读取当前夹具，没有使用旧复制品。

中间首次完整测试139/2/0：暴露MouseArea禁用不等同于onCanceled，原定时seek仍能执行一次；补上禁用清理后通过。失败记录保留 `quick-first-fixed.txt/json`，未改测试预期掩盖问题。

## Windows实际验证

computer-use `@oai/sky`，真实40s/1080p30文件，软件绘制，独立data-dir `tests/codex-fullscreen-seek-20261102/profile`，不占采集卡。使用现有测试运行目录 `tests/claude-sr-stage-20261028/app` 的同产品源码程序和当前外置QML；测试观察只增加运行副本事件日志，结束逐字节恢复产品QML。

时间为 America/Los_Angeles（PST，UTC−8）：

| 时刻（2026-11-02） | 事件与证据 |
|---|---|
| 20:54:14.941 → .948 | 旧版按下后7ms取消，interacting=false，无seek提交 |
| 21:00:50.110 → .117 | 修复版全屏向前拖动，release提交33.770s |
| 21:00:59.497 → .510 | 全屏向后拖动，release提交5.902s |
| 21:01:06.886 → .899 | 全屏点击，release提交20.000s |

原始日志使用UTC，分别为次日04:54:14、05:00:50、05:00:59、05:01:06。`baseline-console.log` 与最终 `fixed-console.log` 保留原始按下/取消/松开事件，`fixed-engine.log` 有对应 `[ui-seek]` 与实际解码位置。最终3次操作均无SEEK_CANCEL；Esc退出全屏正常。窗口化拖动及边界/禁用由组件回归覆盖，未把短时系统注入拖动当作长时间悬停人工验证。

首次修复版人工尝试在文件结束后发生片源菜单/重开，未计为通过；保留 `first-fixed-*.log`。最终用现有启动测试钩子自动进入全屏，保持正常自动隐藏，再做上述独立验证。仅关闭自己启动的测试实例，原用户配置摘要保持。

## 本地交付

`E:/项目/Veyra/test-packages/codex-fullscreen-seek-20261102/Veyra-2.1.2-fullscreen-seek-hotfix.zip`：仅两个产品QML、使用说明和补丁清单。退出2.1.2，将ZIP内容解压到EXE所在目录，覆盖同名文件；NVIDIA/AMD包共用。补丁清单提供两个正式包原文件摘要和补丁文件摘要，专有运行库与主EXE不变。覆盖前可备份原 `qml/Veyra/CineBar.qml` 和 `Main.qml` 便于恢复。

机器回执 `E:/项目/Veyra/reports/codex-fullscreen-seek-20261102/FINAL.json`；原QML保全在 `archives/codex-fullscreen-seek-20261102`。未重新复制完整运行包，未改真实用户数据或GitHub已发布包。
