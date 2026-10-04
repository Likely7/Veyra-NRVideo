# 3a：进程GPU优先级竞争实验

前存档f5e7c47/checkpoint/perf-nr-3a-before-20261004。先用真实本进程GPU调度API实验，未确认收益前不加入产品默认和设置。对已封存5c产品EXE，启动时只用本轮Popen返回的子进程句柄设置Normal(2)/High(4)/Realtime(5)，Set/Get NTSTATUS和实际读回class全部入日志。不提权、不改用户应用/全局驱动设置。依据：[微软SchedulingPriorityClass](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ne-d3dkmthk-_d3dkmt_schedulingpriorityclass)、[SetProcessSchedulingPriorityClass](https://learn.microsoft.com/zh-cn/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtsetprocessschedulingpriorityclass)。

本轮拥有的竞争程序用实际产品Graph：M1首张自然输入、三层原生1080p NR/NVOF，源PTS/id继续推进，连续提交并等完成，30帧预热。完整NR求值数、设备/debug、每秒完成Graph吞吐CSV；此吞吐不是游戏或屏幕FPS。目标播放器M1/S3单NR+DLSS SR到4K，50秒；对方保持正常GPU优先级。三轮交错顺序Normal/High/Realtime、Realtime/Normal/High、High/Realtime/Normal；记录目标真实成功Present软件返回间隔P50/P95/P99/max、跳帧及GPU处理、对方同稳态时间窗吞吐、GPU频率/功耗/显存，避免只用播放器改善判断。

仅本轮竞争程序PID允许与播放器共存，其他测试/构建仍不得重叠；对方连续存活由句柄/退出码核验；各GPU进程最长120或270秒，driver不执行GPU命令。优先级只是提交调度改动，完整画面回归归入R0，不能用软件Present宣称物理显示节奏。结果待填。
