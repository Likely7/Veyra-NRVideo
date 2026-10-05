# 2.0.4 列表预设的光流/内容节奏纠偏

用户截图纠偏：移除独立补帧预设，把光流算法、运动估算质量、内容节奏加入已有“另存为列表预设”。共享参数另列勾选项，取消勾选时保留当前值；原有补帧倍率/后端仍用原预设字段。使用同一预设库，不另建存储或工作流。

起点main d2e589a，新分支codex/list-preset-flow-20261006，worktree E:/项目/Veyra/worktrees/list-preset-flow-20261006；所有新产物按用途在E:/项目/Veyra的list-preset-flow-20261006目录。before全引用bundle已verify，21个旧工作树的未提交原字节/patch/status保存；原2.0.4包和20个其它工作区保留。不可变start.json与SHA在archives同任务目录。每阶段先执行list-preset-flow-control.py；不改旧guard和封存证据。

实现：删除独立FG卡片/API，旧prefs里的fgPresets不主动删除。现有PresetEntry加可选共享运动快照和独立内容位，覆盖光流/质量/AMD性能档/节奏；新格式10，旁路保存.flow，不覆盖旧版本库。旧格式1–9仍读，原行为保留；新的未勾选组严格不覆盖。列表/节点/导入导出和实际视频导出共用PresetLibrary，不引入第二序列化器。补齐选择摘要与四语言。

验证：旧库/全部内容掩码、新组单独/组合保存、未勾选保护、非法值/损坏不覆盖、重开/导出导入；真实QML桥接保存弹窗、恢复与移除旧卡片，以及原NR/FG生产热切换/导出。build≤900秒/test≤300秒/GPU串行，无压力/竞争负载、Agent、用户配置/驱动改动或主机连接。

交付：保持2.0.4显示版本，NVIDIA/AMD同生产EXE，继承上一轮已核验payload组件原字节；本地main延续上轮合并授权，仅修本问题。不push/Release。源码ZIP与CRC/全SHA/PE依赖/独立解压启动及新DELIVERY回执齐全后交付，旧包不覆盖。
