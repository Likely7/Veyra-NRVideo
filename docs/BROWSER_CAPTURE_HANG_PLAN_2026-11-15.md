# 浏览器窗口捕获全屏挂起修复

用户提供三份日志、三个2026-11-15挂起转储。独立codex/browser-capture-hang-20261115，基线main2f0e499；此前采集录音分支、Claude日落/AMD分支、主线与原正式包保留。

两个转储分别对应owner34588、6284，2.1.6映像时间戳/尺寸匹配公开原件。使用原发布构建的对象/库在E盘新目录只重链接映射，.text起点与13681664字节逐字节相同；不改原build/EXE。栈和寄存器证实在FrameCompletionRates::xess逐次写入deque的循环中，剩余计数接近UINT64_MAX。日志显示3840x2159→3840x2160后XeSS交换链重建，累计数归零，而EngineController无符号减去旧值。第三个转储在NVIDIA PTX JIT，15.253秒后日志继续处理，不与前两次永久挂起混同。REA MCP工具未连接，实际证据来自本机PE/转储解析与链接映射，不伪称REA工具返回Evidence。

修复两层：提供方计数以会话代数及回退识别重置，单独消费presented/generated增量、设置修订用当前总数作基线；统计窗口按带权批次计数、工作量不再随外部count增长。保持吞吐语义，不把计数当物理屏幕FPS，不更改FG算法/倍数/颜色/采集API。

验证：现场近2^64输入旧版有限时负例/新版回归、计数代数/回退/延迟生成报告；构建主程序和相关合同/调度；真实WGC动态窗口重复尺寸变化＋XeSS2X/4X/HDR，若本机可用再做浏览器视频全屏/退出切换。无同硬件实测不宣称用户RTX5090实卡已验。所有输出E:/项目/Veyra/{build,tests,logs,tmp,reports,test-packages}/codex-browser-capture-hang-20261115；构建2并发/≤900秒、每测试≤300秒。无合main/push/Release/关机。

## 已执行结果与后续整合

主程序构建317秒，增量4.4秒；live_timing、repair_contract、presentation_worker退出0。真实WGC自建动态窗口＋XeSS 2X/4X各约21秒，分别六次尺寸变化，约490/512帧，停止正常回收；两组控制台各有六条provider-count-reset，无ERROR。测试结果JSON的counterResets未包含控制台日志，不能将其空数组理解成没有重置。尚未完成真实浏览器视频全屏操作或用户RTX5090复测；第三份转储的JIT等待不是本次无符号下溢。

用户最新授权整理全部修复、合并本地main并构建2.1.9，本分支按原代码保留快照后进入该整合；不推送、不发布、不关机。
