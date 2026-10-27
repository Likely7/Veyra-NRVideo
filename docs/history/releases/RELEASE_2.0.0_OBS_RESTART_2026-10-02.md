# 2.0.0 OBS 开关、重启确认与本地包

2026-10-02 用户确认前一 OBS 候选有效，要求增加是/否重启确认、保存设置并构建新 2.0.0 包。当前在 codex/release-2.0.0-20261002 整合已验 OBS 五文件 UI 改动及此分支图标；main 和 OBS 分支保持不变，不 commit/merge/push/公开 Release。

独立 guard 的不可变基线保持原样；仅增加本次明确授权的 bridge 头/源、SettingsPage 和翻译表四条 allowlist。无 engine/runtime/shader/CMake 改动。

开关调用 setPreference 成功保存后，仅在所选值与当前模式不同时弹 VConfirm；否关闭弹窗且不回滚设置。是调用 restartApplication，正在导出则提示等待，其他情况记忆位置并退出事件循环。runApplication 的 UI/engine/QApplication 对象销毁后，外层 main 才用 QProcess::startDetached 启动同一 EXE。保留 --data-dir、返回设置页，不继承单次 --obs-game-capture 或自动打开/测试参数；启动失败显示本机错误框。未声称自动恢复串流。

构建：release-2.0.0-build.py build-obs-restart-v2.log veyra_qml_ui veyra_ui_i18n_tests 成功。初次 git apply 因图标同位置插入而冲突（检查阶段，未应用），随后逐项保留图标并整合 OBS 改动；build-obs-restart.log 仅是旧图标状态无增量构建，不当作修复验收。

测试脚本 obs-restart-test.py 在 baseline-app 隔离副本临时加 QML Timer，调用真实开关信号和是/否按钮信号，检查持久化与重启后 active 状态，不是鼠标点击测试。初版查找从 Window 而非 contentItem 进入，未找到开关；修正夹具，不改产品。失败证据 obs-restart 保留；修正版 obs-restart-v2。

产物统一 E:/项目/Veyra：build/tests/logs/tmp/archives/release-2.0.0-20261002；新包 releases/2.0.0-obs-20261002。旧包、测试者候选和用户配置保留。验收最终结果另补。


最终设置回归：obs-restart-v3 的 no / enable / disable 全部通过，保存值分别 true/true/false，父进程退出 0，子进程完成对应后端启动并退出。v2 的 enable 已在 console.log 出现 CHILD_PASS，但夹具仅查看父 app.log 而误判；v3 改用父子共用 console.log，未改产品。UI 日志确认每次窗口图标 10 尺寸成功加载。i18n 1864 条、0 failures；git diff --check 和范围 guard 通过。

用户前轮已验收 OBS 兼容视频和缩放；本轮保持该模式机制不变，仅增加确认、正常生命周期重启及图标。没有再将早先 720 次缩放写成新二进制重新完成了 720 次。
