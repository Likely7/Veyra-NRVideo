# 3c：compute兼容与呈现队列优先级实验

前存档034c2d8 / checkpoint/perf-nr-3c-before-20261004。仅实验候选，默认仍DIRECT/普通队列；HIGH路径由测试环境开关控制，不作为已保留优化。CommandSlotRing按实际队列Type创建匹配allocator/list，限制DIRECT/COMPUTE。每次Close成功后才执行；不删除GPU依赖或等待。

## NGX兼容性

RTX5070/616.56、原版E16及Lecram F95 NR各direct/compute三轮、DLSS SR direct/compute三轮，共18进程540帧。NR使用现有实际snippet/Core契约；SR使用实际DlssSrBackend、线性RGBA16F输入/输出与depth/motion。所有Create/Evaluate/Release、Close、GPU完成和debug检查通过，设备移除0。完整输出逐帧SHA：fresh A-A噪声0、compute差异0，保存每轮首两帧/末帧原始像素。这是作者固定图案上的API/画面兼容证明，不是完整播放、NVOF并发或队列提速证明。

命令：nr-build.py B2d build-compute-priority-v1 veyra_ngx_compute_queue_experiment veyra_qml_ui veyra_nr_gpu_competition_load（15步/exit0）；v2增加逐帧SHA、选帧像素（2步/exit0）；nr-compute.py B2d B3c-compute-v1（18/18）。原始frames.csv/像素/debug/命令/EXE/runtime身份/GPU环境：E:/项目/Veyra/logs/perf-nr-20261004/B3c-compute-v1-*和summary。

官方接口接收ID3D12GraphicsCommandList，不因此推断所有命令类型通用，兼容性由本轮实际调用验证：[NVIDIA接口定义](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx.h)。跨队列仍必须使用正确资源状态与fence：[Microsoft资源依赖说明](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)。

## HIGH呈现实测（进行中）

独立FG DIRECT呈现队列，先CheckFeatureSupport再Create，读回GetDesc记录实际Priority。进程调度保持普通，两个NR+DLSS SR+DLSS2X/3X，另一个自有三NR满载进程；相同M1/尺寸/运行库/驱动，每档三轮Normal/HIGH交错，50秒稳态。记录新鲜原/生成帧成功Present间隔、GPU/掉帧、竞争进程实际完成率。nr-queue-priority.py B2d B3c-present-priority-v1正在执行；结果未完成前不判断收益，也不默认开启HIGH。后续完整compute路径根据实际兼容与尾部间隔再决定，R0仍待。
