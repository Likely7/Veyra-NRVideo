# 5b 文件/导出跨帧并行实验

从4a明确回退后的8dac9ec继续，先封存方案、无优化产品probe和真实基线，再写候选。用户明确禁止额外GPU竞争、人为满载和显存压力；本节点只做正常文件导出与必要正确性/取消检查。

现状实查：VideoExportJob每帧先等resolveGeneration完成，再交给NVENC；NvencD3D12Encoder本身已有4个异步槽，RGB到NV12转换与Graph同DIRECT ring，NVENC inputFencePoint明确等待转换提交fence。Graph有两份输出和上传缓冲，重用上传缓冲仍等待对应producer fence，不删除这些同步。补帧有效性读回必须在GPU完成后读取，不能一并跳过。

第一候选只对无补帧NVENC导出消除“刚提交即CPU等GPU”的串行点，限制两帧在途，重用前显式确认最旧帧完成；保留取消、设备健康、30秒超时、slow-frame测试语义。其他编码器和补帧仍原路径。先用测试ENV，默认关闭。输出逐帧decoded SHA、PTS、帧数、尺寸、音频轨与时长匹配串行版；同一EXE交错三轮并记录导出整体/处理区间，不把API提交速度冒充最终导出提速。

此候选不代表NVOF(N+1)与NR(N)已重叠。原计划这一部分需要分离源颜色、两份flow/conf及各自producer/consumer fence，当前共用纹理和DIRECT顺序不满足。先测实际剩余瓶颈，再独立实验，不凭代码结构宣称已实现。直播/采集路径不进入此导出候选。

基线以真实exportVideo生产接口构建定向probe；普通4K导出、自然1080源单/双NR、NR+SR4K和无NR的SR8K分开报告。8K高负载NR/其他GPU/设备移除边界不强行测试，不扩展实卡通过范围。每个进程有250秒上限，构建与GPU计时不重叠，导出后软件解码核对在计时波次结束才执行。

## 串行基线已执行

build-export-baseline-v1 UI/probe构建成功，编译产品源码86e0458；只修正驱动gpu_query拼写后以b71fa96脚本跑`nr-export-pipeline.py B2d B5b-serial-v2 serial none4k nr nr2 nrsr4k sr8k`，15/15导出完成、帧数足量、最终编码drain完成、全部载荷SHA保持。M2是4K60作者测试媒体，M1是自然1080p30；三轮baseline输出留待完整软件解码A-A检查。

串行总耗时三轮中位约普通4K2.484s、1080单NR4.306s、双NR4.544s、NR+SR4K6.341s、4K→8K的SR4.400s。除最后60帧，其余120帧。包含创建的总耗时和排除创建的pipeline计时必须分开，前者是实际用户等待。

观察到多个配置pipeline每源帧约15.8ms、NR+SR4K约31.4ms，completionWait占比很高。当前等待是Sleep(1)轮询，尚未测其真实休眠时长，不能直接断言系统timer根因。候选将把fence事件等待和两帧在途作为独立开关/组，避免混淆等待精度与跨帧并行的收益。

基线完整软件解码15/15、1620帧，三轮A-A全图/PTS/尺寸/轨道/时长完全一致；核实SR8K实际7680×4320，NR+SR实际3840×2160。review在E:/项目/Veyra/logs/perf-nr-20261004/B5b-serial-v2-decoded-review.json，framemd5每帧覆盖完整重建图。后续复用这些不可变源与已核验hash，避免无依据重复解码。

候选实现：每个pending持有完整FrameOutputs/lease与NVENC转换consumerFence，最多两帧；在第3帧进入Graph前确认最旧producer+consumer完成并释放lease，末尾全部收齐再encoder.finish。Graph上传/allocator和NVENC4槽依赖不变。fence事件仅同一fence对象上的值取max，FG status仍完成后resolve；50ms有界wait、250ms健康检查和30s超时保留，事件注册失败记HRESULT并回退轮询。两个测试ENV分别启用，默认关闭，未测不标产品通过。

## 候选普通对照与完整输出

`nr-export-pipeline.py B2d B5b-candidate-v1 all none4k nr nr2 nrsr4k sr8k`60/60实际导出通过，五配置×四策略×三轮交错，同EXE/组件载荷前后SHA一致，无压力。`nr-export-report.py`核实每组策略实际生效、maxInFlight1/2、NR真实创建数与无ERROR，再输出comparison.json；初次报表因运行库写入非UTF8字节读失败，改errors=replace（仅ASCII字段解析），未删改raw日志。

| 三轮中位 | 串行总ms | 组合总ms | 总耗时降幅 | 串行处理ms | 组合处理ms | 处理降幅 |
|---|---:|---:|---:|---:|---:|---:|
| 普通4K / 120帧 | 2488.830 | 1384.420 | 44.37% | 1892.387 | 801.251 | 57.66% |
| 自然1080单NR / 120帧 | 4338.210 | 3314.370 | 23.60% | 1881.085 | 860.843 | 54.24% |
| 自然1080双NR / 120帧 | 5492.240 | 4712.150 | 14.20% | 1941.885 | 1614.626 | 16.85% |
| NR+SR4K / 120帧 | 7189.020 | 6727.860 | 6.41% | 3796.911 | 3226.199 | 15.03% |
| 4K→SR8K / 60帧 | 4649.890 | 4467.930 | 3.91% | 1901.832 | 1703.155 | 10.45% |

总耗时含初始化/最终保存；处理区间不含创建但含encoder.finish，不能将后二列当作整次导出改善。双NR初始化波动较大，组合总耗时范围4648.730–5127.380ms，完整范围在comparison.json。多数普通4K改善来自改变等待方式：仅fence事件处理814.877ms，组合801.251ms；单NR仅事件946.126ms，组合860.843ms，可见两帧并行的额外收益。8K仅事件1704.953ms，组合1703.155ms，不能宣称8K跨帧本身明显收益。

`nr-export-verify.py B5b-serial-v2 B5b-candidate-v1`75/75通过、8100完整decoded帧与PTS/尺寸/轨道/时长全部一致；所有策略生成的整个封装文件SHA也与各自A一致。收据B5b-candidate-v1-decoded-review.json，各原始framemd5/ffprobe保留。该验证覆盖当前本机/运行库/素材，不代表其他GPU、HDR或直播通过。

下一步单独验证GPU debug、取消、单帧、FG保持串行和事件API失败回退。只模拟API错误分支，不制造GPU压力或设备重置；候选继续默认关闭。
