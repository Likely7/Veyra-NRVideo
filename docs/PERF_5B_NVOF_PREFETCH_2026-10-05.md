# 5b NVOF 下一帧准备的独立候选

起点为已保留导出子节点17d7a6c。只在原优化分支创建测试ENV候选，默认关闭；不扩大采集/串流时延，不压力测试，不改运行库。产品原NVOF明确disableTemporalHints=true，当前→上一帧向量仍按原contract；不会用两帧在途的CPU提交冒充实际NVOF重叠。

最小闭环先复用生产EnhanceGraph的无NGX光流路径，创建同一D3D12设备上的独立COMPUTE队列、独立fence/ring，保持共享设备但严禁两个ring给同一fence各自Signal。额外两个flow/conf快照分别有producer/consumer fence；主图每次消费前GPU Wait，不回读整幅像素，不吞同步。先前主图NR/SR仍在途时才准备下一帧输入和NVOF，使用原色彩转换、原光流/置信度shader及历史分析，不另写一套光流算法。

快照描述验证源ID、上一源ID、PTS、settingsRevision、尺寸、设备、格式和reset结果；首帧/scene-cut/时间断点不消费旧motion，重建及关闭等待消费者后注销NVOF资源。只准无FG、CPU解码SDR、本机NVIDIA NVOF常规NR/SR路径进入实验，HDR、颜色节点、其他OF、采集和其他编码器保持原路径。

先用真实生产exportVideo测单NR与NR+SR4K，三个交错回合，B-off与已验证默认导出路径比较整次/处理区间；完整输出文件/decoded PTS不一致则不进入产品。核对debug层、reset/取消及有限资源重用，不做设备移除或真实资源压力。单进程≤250秒。该复用图会额外复制flow/conf且占显存，若成本大于隐去的NVOF等待，明确拒绝此实现；不能为微小收益重写整条source/color链或宣传达到原估计10%。

初始源码检查：独立辅助图需要独立queue/fence但共享ID3D12Device，现D3D12DeviceContext只有新建device入口；新增仅供此候选的共享设备队列创建，事务式资源创建失败不破坏原context。原图NVOF input A/B仍正常更新，故关闭候选后可回原路径，生产默认图无附加GPU资源。

候选按上述结构实现，`VEYRA_TEST_EXPORT_NVOF_PREFETCH=1`才创建辅助图；主图消费导入快照时维持GPU侧fence依赖，生产默认无快照/额外队列。准备过程复用实际EnhanceGraph，不复制颜色/光流算法。主图计数另记prefetchedFlowCount，辅助图实际NVOF execute仍有日志，不冒充主图常规execute。关闭辅助图先等各快照consumer，再drain/注销，原Graph reset仍按自己的真实luma/PTS分析并与快照描述匹配。下一步编译和真实输出/正常耗时证据；尚未声明通过。

build-nvof-prefetch-v1失败，FlowPrefetch初始化误写不存在的nrLayers成员（正确并行数组为nrLayersExtent/Model等）；删除多余访问后进行有据的第二次构建，原失败日志保留。不将编译失败当成运行时/硬件失败。

v2构建成功；B5b-prefetch-smoke-v1七个真实debug导出通过，单/双NR、NR+SR4K整文件SHA各自同off，取消清理通过，debug0/removed0；每个24帧组实际导入23个flow，原NVOF执行0，辅助图确有23次NVOF执行。raw目录同任务logs。此小样本的处理计时包含辅助图创建，尚不能用于稳态提速结论：下一构建将计时起点放在辅助图创建之后，整次导出仍包含创建。没有改写旧数据。

追加独立trace开关，只在诊断组记录真实GPU Flow/NR/SR timestamp，按每个队列GetClockCalibration映射到QPC并保留调用耗时界限，禁止用裸跨队列计数相同直接证明重叠。依据[微软GetClockCalibration文档](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-getclockcalibration)；正常性能三轮trace关闭，以免日志干扰。最终是否保留仍需整次/稳态和额外显存的收益风险比较。

## 普通负载结果与拒绝

产品编译1efd344，build-nvof-prefetch-v4成功。`nr-export-pipeline.py B2d B5b-prefetch-normal-v1 prefetch-interleaved nr nrsr4k nr2`18/18及`... B5b-prefetch-8k-v1 prefetch-interleaved sr8k`6/6真实导出通过；正常GPU优先级，三轮同EXE交错，无竞争/显存压力，trace关闭。4K/8K是实际导出尺寸；8K为4K→8K SR，不冒充8K NR。各组6个完整文件SHA与此前75文件/8100帧完整decoded证明相同，因此像素/PTS/轨道/封装一致；不声称重新解码了这些新文件。

| 三轮中位，ms | 原默认整次 | 预取整次 | 整次变化 | 原默认处理 | 预取处理 | 处理降幅 | 额外显存MiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| 单NR / 120源帧 | 3297.120 | 3343.480 | +1.41% | 862.045 | 810.269 | 6.01% | 232 |
| NR+SR4K / 120源帧 | 5804.920 | 5924.600 | +2.06% | 3214.290 | 3167.120 | 1.47% | 232 |
| 双NR / 120源帧 | 4273.270 | 4390.320 | +2.74% | 1609.029 | 1565.907 | 2.68% | 232 |
| 4K→SR8K / 60源帧 | 4203.130 | 4423.780 | +5.25% | 1692.796 | 1695.573 | -0.16% | 867 |

完整范围、日志、GPU快照、载荷前后SHA及比较JSON在`E:/项目/Veyra/logs/perf-nr-20261004/B5b-prefetch-normal-v1-summary/`、`B5b-prefetch-8k-v1-summary/`。120帧组actual imported119个flow、8K60帧组59，主图常规NVOF execute=0，辅助图真实执行相应次数；没有靠少做原帧/降低尺寸取得数字。

`... B5b-prefetch-trace-v1 prefetch-trace nr nrsr4k`2个独立诊断组、各119个(N+1 flow/N NR+SR)匹配对；`nr-prefetch-report.py B5b-prefetch-trace-v1`记录依赖区间重叠中位1.028/1.068ms，119/119超过估计校准界限0.076/0.226ms，前后校准drift约0.024/0.095ms。该Flow区间包含API/提交/queue-wait空隙，不是独立NVOF硬件kernel耗时；不把trace组wall time混入正常三轮。[D3D12 timing说明](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing)提示空闲可能使校准漂移，本次记录漂移而未改变稳定电源/用户驱动设置。

决定拒绝并明确回退本准备器、共享队列入口和导入接口。模型画面与debug并无错误，拒绝原因是整次负收益、处理降幅不足目标10%及额外显存/生命周期复杂度；不是声称NVOF理论上不能并行。当前helper重复一部分源颜色与输出工作，更轻的源准备需要另一次有数据支撑的设计，不在这个负候选里无限重构。该负候选不扩进文件播放，文件/采集/串流保持原图；不宣称文件播放或8K NR验收。前面已保留的两帧NVENC/事件优化不回退，继续后续节点。

回退已完成：`git revert --no-commit 1efd344 f07f6c3 58f5845 5edbf79`在历史文档发生冲突，保留当前完整实测文档后继续；Git产生484b425/b954b58/30cedcb三个明确revert提交。检查CMake、gfx context、Graph头/实现、VideoExportJob及主driver与before347a184的diff完全为空；新FlowPrefetch/GpuQueueTrace产品文件已删除。原测试EXE/载荷、源码bundle、debug/普通/trace数据及报表脚本保留。不把格式冲突解决写成新的性能测试，R0仍待。
