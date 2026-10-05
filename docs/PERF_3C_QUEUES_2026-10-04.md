# 3c：compute兼容与呈现队列优先级实验

前存档034c2d8 / checkpoint/perf-nr-3c-before-20261004。仅实验候选，默认仍DIRECT/普通队列；HIGH路径由测试环境开关控制，不作为已保留优化。CommandSlotRing按实际队列Type创建匹配allocator/list，限制DIRECT/COMPUTE。每次Close成功后才执行；不删除GPU依赖或等待。

## NGX兼容性

RTX5070/616.56、原版E16及Lecram F95 NR各direct/compute三轮、DLSS SR direct/compute三轮，共18进程540帧。NR使用现有实际snippet/Core契约；SR使用实际DlssSrBackend、线性RGBA16F输入/输出与depth/motion。所有Create/Evaluate/Release、Close、GPU完成和debug检查通过，设备移除0。完整输出逐帧SHA：fresh A-A噪声0、compute差异0，保存每轮首两帧/末帧原始像素。这是作者固定图案上的API/画面兼容证明，不是完整播放、NVOF并发或队列提速证明。

命令：nr-build.py B2d build-compute-priority-v1 veyra_ngx_compute_queue_experiment veyra_qml_ui veyra_nr_gpu_competition_load（15步/exit0）；v2增加逐帧SHA、选帧像素（2步/exit0）；nr-compute.py B2d B3c-compute-v1（18/18）。原始frames.csv/像素/debug/命令/EXE/runtime身份/GPU环境：E:/项目/Veyra/logs/perf-nr-20261004/B3c-compute-v1-*和summary。

官方接口接收ID3D12GraphicsCommandList，不因此推断所有命令类型通用，兼容性由本轮实际调用验证：[NVIDIA接口定义](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx.h)。跨队列仍必须使用正确资源状态与fence：[Microsoft资源依赖说明](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)。

## HIGH呈现实测（已完成，拒绝默认HIGH）

独立FG DIRECT呈现队列，先CheckFeatureSupport再Create，读回GetDesc记录实际Priority。进程调度保持普通，两个NR+DLSS SR+DLSS2X/3X，另一个自有三NR满载进程；相同M1/尺寸/运行库/驱动，每档三轮Normal/HIGH交错，50秒稳态。nr-queue-priority.py B2d B3c-present-priority-v1十二组完成，所有请求实际读回0/100，无source preview skip/设备故障。

| 设置 | 队列 | 软件Present P95 | P99 | 每轮max中位 | 增强处理滚动均值的中位数 | 竞争图完成/s |
|---|---|---:|---:|---:|---:|---:|
| 2X | Normal | 17.1023 | 17.3202 | 63.1653 | 27.653 | 15.8293 |
| 2X | HIGH | 17.0946 | 17.4002 | 73.4092 | 27.677 | 15.7525 |
| 3X | Normal | 11.5513 | 16.7883 | 78.4057 | 30.436 | 13.2248 |
| 3X | HIGH | 11.5418 | 16.5618 | 74.1415 | 30.587 | 13.2990 |

各列为三个运行统计量的中位数，ms（最后列除外），不是物理扫描；滚动P95观察的中位也不冒充全帧aggregate percentile。2X P99+0.46%、max+16.22%；3X P99-1.35%、max-5.44%，处理成本+0.50%。改善随倍率不稳定，拒绝默认HIGH，产品仍普通队列。测试ENV暂留用于尚未完成的compute组合对照，不当成可用性能功能或发布默认。三轮范围、每帧submit/负载/GPU环境/构建身份见同任务B3c-present-priority-v1-*、B3c-present-priority-v1-summary.json与B3c-present-priority-v1-comparison.json。

## 完整处理图正确性（已完成，性能仍待）

前存档0212db3 / checkpoint/perf-nr-3c-graph-before-20261004。单PreviewGpuSession增加仅测试ENV的COMPUTE生产队列，device/Core/adapter仍一个；ring作为该fence唯一signaler。EnhanceGraph的解码、输入、NVOF、GPU-DIS/AMD和呈现归还依赖均等待ring实际queue，GPU timer也查询实际queue。DIRECT绘制/Present保留独立ring/fence；生产输出交给DIRECT时既有COMMON→PIXEL→COMMON往返保持，等待/生命周期不删。测试默认关闭，尚未获保留结论。

build-graph-compute-v1构建产品UI、负载fixture及native fixture，exit0。nr-graph-queue.py B2d B3c-graph-native-v1四组（单NR、NR+SR、双NR+SR+DLSS2X/3X）各direct/compute三次，共24/24通过；M1真实自然视频60帧/进程，1440当前真实输出+1062全部生成输出=2502完整图像。2X每进程59生成帧，3X118，完整SHA、dimensions/subframe/PTS每行均一致，fresh A-A噪声0。所有D3D12 debug错误/设备移除0，Core关闭通过。该native测试因诊断读回串行等待，只验证完整图/NVOF/生成/时间戳正确性，不用于吞吐收益。

CSV/全SHA/关键原始RGBA、身份/环境/命令均E:/项目/Veyra/logs/perf-nr-20261004/B3c-graph-native-v1-*与B3c-graph-native-v1-summary.json，build日志同目录。现阶段源码按checkpoint/perf-nr-3c-graph-candidate-20261004存档，校验收据E:/项目/Veyra/archives/perf-nr-20261004/3c-graph-candidate/checkpoint.json；下一轮需实际Qt在负载下DIRECT-normal/COMPUTE-normal/COMPUTE-HIGH三轮交错比较及独立B-off，收益不成立则revert该候选。用户要求当前轮结束汇报，尚未启动下一轮，R0仍待。

## NR耗时复核（用户要求拉出测试数据）

条件：RTX5070/616.56、M1 1920×1080/30、Lecram310.8.3.0，两层NR内部1080p、DLSS SR到4K及DLSS2X/3X，时域NR关闭。队列轮次明确同时运行自有三层原生1080p NR压力进程，尽快连续求值，并非单播放器空闲GPU场景；例如2X-normal-r1开始/结束nvidia-smi GPU使用率快照均93%。压力图稳态完成约13–16次/s，每次含三次NR求值。

| 测试条件 | NR第1轮 | 第2轮 | 第3轮 | 增强处理三轮中位 |
|---|---:|---:|---:|---:|
| 原基线S4，2X，无自有竞争进程 | 7.212 | 7.188 | 7.202 | 21.192 |
| 本轮2X，Normal，三NR竞争 | 11.365 | 11.367 | 11.374 | 27.653 |
| 本轮2X，HIGH，三NR竞争 | 11.385 | 11.391 | 11.328 | 27.677 |
| 本轮3X，Normal，三NR竞争 | 11.536 | 11.389 | 11.445 | 30.436 |
| 本轮3X，HIGH，三NR竞争 | 11.518 | 11.426 | 11.560 | 30.587 |

单位ms。每轮数值为稳态滚动日志读数的中位；gpuNrP95Ms是滚动P95，enhancementProcessingMs来自dashboard.enhancementProcessing.mean，是滚动均值，修正前表误写P95。两者不能相加或当成同一分位统计。GpuTimer::mark每层写同一个GpuStage::Nr槽，因此该NR字段只保留最后一层snippet求值区间；增强处理总计另合并Flow/SR/NR/per-layer/residual/FG时间区间去重，包含每层但不是端到端延迟或整图完成包络。多层总成本以这些完整指标判断，不能将NR列乘层数充当测量。

十二轮全部player-timing行里的NR滚动P95最大读数11.972ms；这不是逐帧最大NR求值时间，稳态中位也不等于峰值。完整graph-native正确性fixture含诊断GPU等待/全图读回，不提供性能验收数据，不能用其进程墙钟反推NR耗时。

NR高读数确实存在。GPU竞争是明确的条件差异。2026-10-05已补同一97dbccb…EXE无自有压力三轮及一对确认，与A交错八组，详PERF_3C_NORMAL_LOAD_2026-10-05。全四轮含异常的NR中位A/B7.3925/7.256ms、增强21.394/21.245ms；此前B普通2X有压力11.367/27.653ms，高读数回落且未测到该设置下普遍NR回退，差异不足以宣称持续播放提速。A3有约2.84s软件提交断档、89源帧跳过，B3累计13跳过/14生成过期，确认组0跳帧；根因未明，保留证据并列R0，连续性不标通过。接着才评估完整COMPUTE队列收益；核心复用/缓存/预热收益针对创建或切换，暂停收益针对暂停，不代表持续播放NR求值减少。

数据导出：E:/项目/Veyra/logs/perf-nr-20261004/B3c-present-priority-v1-NR-readout.csv（基线15轮+队列12轮=27行，缺少的原始指标留空）；原始baseline-M1-summary.json、B3c-present-priority-v1-summary.json和各轮player.log/timing.csv保持原样。此次仅复核既有数据，无新GPU测试或产品改动。
