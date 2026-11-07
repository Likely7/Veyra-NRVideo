> 后续更新：Claude已复现同路径、相同ZIP时间戳时Qt复用旧编译缓存，修复已并本地main。见[合并验收](CLAUDE_FIXES_MERGE_2026-11-06.md)与[缓存修复](QML_CACHE_PER_BUILD_2026-11-06.md)。下文保留本次初步排查过程。

# 2.1.3 RTX三模型选择跳回DLSS：日志诊断

用户日志35447行，SHA256 2975caad86b74be9ce09be500176a700bbbcc345db1e6eea7b6fd58df8c91df3；RTX3080 10GB，驱动32.0.16.2101。用户明确新目录解压，不归因覆盖残留。

日志第996行选择高码率，997行却accepted quality=0；第20228行从FSR选标准，20229行同样accepted quality=0。其他RTX点击没有提交对应2/17/21，未看到RTX模型创建失败。DLSS/FSR正常提交和创建。注意日志Z时间为UTC11月7日，对应客户端太平洋时间11月6日；原证据保持UTC。

源码与已发布NVIDIA ZIP核对，ProPage/NodePage均使用srQualityFor(string family,tier)；旧2.1.2 ProPage则Number(id)。本地RTX5070私有应用关闭所有效果、不打开媒体，实际菜单点击RTX三种与DLSS/FSR分别得到2/17/21/0/5，全部通过。只在私有ProPage换回旧Number(id)处理器，同样真实菜单点击得到0/0/0/0/5，与用户日志高度吻合。正式包无qmlc/jsc文件。此对照不是用户实际文件内容的证明，也不是RTX3080推理验收。

当前定位为模型ID在选择链路被错误转换的强证据，不能把责任归于GPU算力、驱动或缺DLL；下一步核对用户实际加载的qml/Veyra/ProPage.qml、NodePage.qml及EXE SHA256，确认文件和加载来源。未改产品、不发覆盖补丁、不推送或发布；不要求反复换驱动/重跑模型。证据reports/sr-menu-investigation-20261106/FINDINGS.json，命令ui_runner.py配menu-real.qml/menu-legacy.qml，实际日志logs同任务。

首个QtTest夹具when:windowShown在独立应用中未触发，已停止唯一私有路径测试进程并改Timer显式调用测试方法；两次有效检查均exit0，无engine/QML错误。未碰用户运行进程、配置和原日志。仅作为受控对照把旧处理器放入私有QML，已恢复原字节。
