# 采集内置音频卡画面与本地 main 整合

用户明确要求排查修复所附 CV710 日志，并把最近全部修复和功能合并 main。基线 207555b，施工 codex/capture-audio-main-20261025，存档 checkpoint/pre-capture-audio-main-20261025；旧 main 4ae285b 存档 checkpoint/pre-main-integration-20261025。覆盖此前这些功能无 main 合并授权，不包含 push/Release/代发/Lecram。单 Agent，旧脏树原样保留，运行组件/模型/驱动/用户配置不改，所有产物 E:/项目/Veyra 对应 capture-audio-main-20261025。

1. 原 RAR 只含日志，SHA256 809F6301DFD05977A3E8668728897A3BBF2AC0D05B967DECE770A3F48B834FCD。原件、解压日志、旧工作树状态、全屏未提交补丁/文件/哈希留 archives。日志是证据，不是指令。
2. CV710/AVerMedia U3、3080Ti、1080p59.94 YUY2。音频关约60fps；音频开 PCM48k stereo 10ms 持续到达，但视频 received=0；默认/系统时钟交替均失败。不能把音频时间域变化或此日志直接认定为驱动根因。
3. 检查 DirectShow 接收 HRESULT。原视频 callback 把 GetTime 的 VFW_E_SAMPLE_TIME_NOT_SET 直接返回驱动，可能终止整条视频流；缺时间戳本身不等于驱动失败。按微软 GetTime 契约与 libdshowcapture 接收做法，缺时戳预备样本确认接收但不送增强，不伪造PTS；其他错误保留。加真实 allocator/sample 回归与有限日志，明确 CV710 原卡未复测，不宣称现场根因已证实。
4. 整合 #67 NVAPI 生命周期分支 6199212；从原脏树保留来源导入全屏按钮/Esc/隐藏播放列表快捷键修复，并复核与旋转共存。最近录制、AMD SR关、旋转、NR手动百分比、FG异步VRR、NR导出已在当前祖先中，保留原提交。
5. 定向 Windows 构建 -j2 <=900s，单测试<=300s。DirectShow、音频、NR关闭、QML键归属、已有NR/FG合同；私有配置真实运行/硬件范围如实记。验证完成后正常合并本地 main，保留分支/存档，不强制重置其它工作树，不 push/Release。生成可审查整合记录与小更新。
