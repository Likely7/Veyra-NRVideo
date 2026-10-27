# 专业模式原生视频遮挡播放条修复

用户反馈专业模式画面向下盖住播放条，要求追溯是哪项修复导致。基线34f479e（含已验采集节奏修复），隔离codex/pro-video-geometry-20261012；不改变main、其它工作树或用户正在使用的程序。开工start/bundle位于E:/项目/Veyra/archives/pro-video-geometry-20261012，保护65旧树/509脏文件。

已查到4557ab6将专业页统计区由固定172改为按状态说明动态增高，但Main.videoRect仍减固定172；同时旧公式长期固定32像素标题、36像素播放条和多减14像素宽度，与实际响应式布局不同。先用原生产EXE/原QML记录实际videoArea、transport与原生HWND边界，保留失败负对照，不能只用不含原生子窗口的Qt截图证明修复。

只统一ProPage持有的布局尺寸，Main原生视频几何使用同一份尺寸并在几何变更时同步。保留打开源前可计算的非零尺寸，覆盖普通/窄窗口、检查器调整、状态选项/语言变化、页面切换及全屏恢复。不添加生产Timer/update/requestUpdate，不修改33ms统计、250ms状态发布、GPU优先级、引擎/算法/驱动/运行库或真实用户配置。

独立运行目录和data-dir，原子替换QML，运行时保持原字节；所有产物写入E:/项目/Veyra/{tests,logs,tmp,reports,archives}/pro-video-geometry-20261012。每个测试进程≤300秒；必要构建≤900秒。定向检查实际HWND与QML区域一致、播放条不重叠、图标软件/GPU模式、无QML错误，以及UI绘制节奏未被改变；性能数字只按实测口径报告。

本次没有合并main、push、打包、Release或关机授权。完成后记录WORKLOG与报告，提供原因、修正、验证和未测边界。
