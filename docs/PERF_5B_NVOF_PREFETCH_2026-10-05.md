# 5b NVOF 下一帧准备的独立候选

起点为已保留导出子节点17d7a6c。只在原优化分支创建测试ENV候选，默认关闭；不扩大采集/串流时延，不压力测试，不改运行库。产品原NVOF明确disableTemporalHints=true，当前→上一帧向量仍按原contract；不会用两帧在途的CPU提交冒充实际NVOF重叠。

最小闭环先复用生产EnhanceGraph的无NGX光流路径，创建同一D3D12设备上的独立COMPUTE队列、独立fence/ring，保持共享设备但严禁两个ring给同一fence各自Signal。额外两个flow/conf快照分别有producer/consumer fence；主图每次消费前GPU Wait，不回读整幅像素，不吞同步。先前主图NR/SR仍在途时才准备下一帧输入和NVOF，使用原色彩转换、原光流/置信度shader及历史分析，不另写一套光流算法。

快照描述验证源ID、上一源ID、PTS、settingsRevision、尺寸、设备、格式和reset结果；首帧/scene-cut/时间断点不消费旧motion，重建及关闭等待消费者后注销NVOF资源。只准无FG、CPU解码SDR、本机NVIDIA NVOF常规NR/SR路径进入实验，HDR、颜色节点、其他OF、采集和其他编码器保持原路径。

先用真实生产exportVideo测单NR与NR+SR4K，三个交错回合，B-off与已验证默认导出路径比较整次/处理区间；完整输出文件/decoded PTS不一致则不进入产品。核对debug层、reset/取消及有限资源重用，不做设备移除或真实资源压力。单进程≤250秒。该复用图会额外复制flow/conf且占显存，若成本大于隐去的NVOF等待，明确拒绝此实现；不能为微小收益重写整条source/color链或宣传达到原估计10%。

初始源码检查：独立辅助图需要独立queue/fence但共享ID3D12Device，现D3D12DeviceContext只有新建device入口；新增仅供此候选的共享设备队列创建，事务式资源创建失败不破坏原context。原图NVOF input A/B仍正常更新，故关闭候选后可回原路径，生产默认图无附加GPU资源。

候选按上述结构实现，`VEYRA_TEST_EXPORT_NVOF_PREFETCH=1`才创建辅助图；主图消费导入快照时维持GPU侧fence依赖，生产默认无快照/额外队列。准备过程复用实际EnhanceGraph，不复制颜色/光流算法。主图计数另记prefetchedFlowCount，辅助图实际NVOF execute仍有日志，不冒充主图常规execute。关闭辅助图先等各快照consumer，再drain/注销，原Graph reset仍按自己的真实luma/PTS分析并与快照描述匹配。下一步编译和真实输出/正常耗时证据；尚未声明通过。

build-nvof-prefetch-v1失败，FlowPrefetch初始化误写不存在的nrLayers成员（正确并行数组为nrLayersExtent/Model等）；删除多余访问后进行有据的第二次构建，原失败日志保留。不将编译失败当成运行时/硬件失败。
