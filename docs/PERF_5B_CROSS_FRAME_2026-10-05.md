# 5b 文件/导出跨帧并行实验

从4a明确回退后的8dac9ec继续，先封存方案、无优化产品probe和真实基线，再写候选。用户明确禁止额外GPU竞争、人为满载和显存压力；本节点只做正常文件导出与必要正确性/取消检查。

现状实查：VideoExportJob每帧先等resolveGeneration完成，再交给NVENC；NvencD3D12Encoder本身已有4个异步槽，RGB到NV12转换与Graph同DIRECT ring，NVENC inputFencePoint明确等待转换提交fence。Graph有两份输出和上传缓冲，重用上传缓冲仍等待对应producer fence，不删除这些同步。补帧有效性读回必须在GPU完成后读取，不能一并跳过。

第一候选只对无补帧NVENC导出消除“刚提交即CPU等GPU”的串行点，限制两帧在途，重用前显式确认最旧帧完成；保留取消、设备健康、30秒超时、slow-frame测试语义。其他编码器和补帧仍原路径。先用测试ENV，默认关闭。输出逐帧decoded SHA、PTS、帧数、尺寸、音频轨与时长匹配串行版；同一EXE交错三轮并记录导出整体/处理区间，不把API提交速度冒充最终导出提速。

此候选不代表NVOF(N+1)与NR(N)已重叠。原计划这一部分需要分离源颜色、两份flow/conf及各自producer/consumer fence，当前共用纹理和DIRECT顺序不满足。先测实际剩余瓶颈，再独立实验，不凭代码结构宣称已实现。直播/采集路径不进入此导出候选。

基线以真实exportVideo生产接口构建定向probe；普通4K导出、自然1080源单/双NR、NR+SR4K和无NR的SR8K分开报告。8K高负载NR/其他GPU/设备移除边界不强行测试，不扩展实卡通过范围。每个进程有250秒上限，构建与GPU计时不重叠，导出后软件解码核对在计时波次结束才执行。
