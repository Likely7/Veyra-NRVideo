# 3a：进程GPU优先级竞争实验

前存档f5e7c47/checkpoint/perf-nr-3a-before-20261004。先用真实本进程GPU调度API实验，未确认收益前不加入产品默认和设置。对已封存5c产品EXE，启动时只用本轮Popen返回的子进程句柄设置Normal(2)/High(4)/Realtime(5)，Set/Get NTSTATUS和实际读回class全部入日志。不提权、不改用户应用/全局驱动设置。依据：[微软SchedulingPriorityClass](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ne-d3dkmthk-_d3dkmt_schedulingpriorityclass)、[SetProcessSchedulingPriorityClass](https://learn.microsoft.com/zh-cn/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtsetprocessschedulingpriorityclass)。

本轮拥有的竞争程序用实际产品Graph：M1首张自然输入、三层原生1080p NR/NVOF，源PTS/id继续推进，连续提交并等完成，30帧预热。完整NR求值数、设备/debug、每秒完成Graph吞吐CSV；此吞吐不是游戏或屏幕FPS。目标播放器M1/S3单NR+DLSS SR到4K，50秒；对方保持正常GPU优先级。三轮交错顺序Normal/High/Realtime、Realtime/Normal/High、High/Realtime/Normal；记录目标真实成功Present软件返回间隔P50/P95/P99/max、跳帧及GPU处理、对方同稳态时间窗吞吐、GPU频率/功耗/显存，避免只用播放器改善判断。

仅本轮竞争程序PID允许与播放器共存，其他测试/构建仍不得重叠；对方连续存活由句柄/退出码核验；各GPU进程最长120或270秒，driver不执行GPU命令。优先级只是提交调度改动，完整画面回归归入R0，不能用软件Present宣称物理显示节奏。结果待填。


## v1无效与修正

v1在Popen后立即调用Set/Get，进程GPU上下文尚未创建，三个档位均NTSTATUS 0xC000000D，applied=false。数据不用于收益。只读核验已运行的两个本轮GPU进程时Get均成功、实际class=2，确认需要等待WDDM进程记录就绪。v1保存三个完整组，第四个本轮竞争fixture收到stop标记后正常退出、driver在对方早停断言退出；未关闭用户进程。v2竞争方在LOAD_READY后设置并读回，播放器等待Get首次成功（30秒上限、全部失败状态保留）后才设置并立即读回，不能读回则数据直接判无效。


## 有效18组实测

v2 M1真30帧自然视频与v3-60 M10合成1080p60，各Normal/High/Realtime三轮、交错顺序，共18组全通过。Set/Get各组实际2/4/5，竞争方实际Normal2，真实NR完整求值/debug/device通过。每轮稳态软件Present间隔样本M1为1140、M10为2280，无preview跳帧/过期补帧。表为每轮P95/P99/max再取三轮中位数，不把它称所有样本聚合P99。

| 输入 | 优先级 | Present P95 ms | P99 ms | max ms | GPU就绪滚动P95中位 ms | 竞争Graph完成/s |
|---|---|---:|---:|---:|---:|---:|
| M1 30Hz | Normal | 33.8545 | 34.0324 | 38.2812 | 19.671 | 29.0455 |
| M1 30Hz | High | 33.8166 | 33.9923 | 37.6997 | 19.867 | 29.0009 |
| M1 30Hz | Realtime | 33.8263 | 34.0280 | 40.5258 | 11.807 | 29.1079 |
| M10 60Hz | Normal | 17.6123 | 18.9184 | 40.7287 | 32.3215 | 14.5448 |
| M10 60Hz | High | 17.5489 | 19.0063 | 35.5948 | 32.2205 | 14.5238 |
| M10 60Hz | Realtime | 17.4517 | 17.6260 | 39.6298 | 12.1910 | 15.2195 |

Realtime对M10 P99改善6.83%、GPU就绪估计减少62.28%，竞争Graph吞吐增加4.64%；M1的极端max增加5.86%，不能据平均/就绪收益宣称所有尖峰改善。High M10 P99反而增加0.46%，M1就绪增加约1%，无足以改默认的稳定收益。因此拒绝原计划“默认High”，维持Normal，保留供用户自选的优先级能力，Realtime不默认。全部是软件Present返回和GPU时间戳估计，不是物理面板帧率/端到端延迟。CPU/GPU频率/整卡占用仍在每秒meters中，未锁频。

M10由现有10秒合成translation素材重定时循环生成60秒，3600帧、60/1，前600帧无相邻重复，SHA4f588d4c8eb4945f83986b5a9710b630b25327375570c85c2e68561f9bef40f1；M2同方法为4K60，SHA19482f441ee53f8ed35b2b95da343b18d2fce5764181aefb45bb4c32c46fa943。它们不是真实主机或电影素材，来源/编码命令/framemd5/ffprobe在authored-media-v1 manifest。缓存/其他待建产品源码未用于18组计时：测试使用封存5c EXE bf4952347424f500df45a29ab3f7289742e4ecae188ac66b71805b9743978568。产品首选项/实际生效/画面导出回归仍待。
