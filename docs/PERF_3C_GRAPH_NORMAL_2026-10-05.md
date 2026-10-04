# 3c 完整图普通负载对照

同一 B2d 产品 EXE、M1 自然视频、RTX5070 / 616.56、F95 Lecram NR、双层1080p NR、DLSS SR 4K、DLSS 2X/3X、普通进程GPU优先级。未启动额外GPU竞争程序，未做显存压力。DIRECT普通、COMPUTE普通、COMPUTE+DIRECT HIGH各三轮交错，18/18产品退出与效果身份检查通过；全部EXE/DLL/QML测试前后SHA不变。原始设置、身份、命令、日志、timing.csv及异常全部保留。

| 倍率 / 队列 | 最后一层NR滚动P95中位ms | 增强区间滚动均值中位ms | 成功软件Present间隔P99中位ms | 三轮最大间隔范围ms |
|---|---:|---:|---:|---:|
| 2X DIRECT普通 | 7.363 | 21.386 | 17.2667 | 73.117–79.0399 |
| 2X COMPUTE普通 | 6.595 | 19.477 | 17.2431 | 68.0892–75.5360 |
| 2X COMPUTE / HIGH呈现 | 6.415 | 19.236 | 17.2641 | 73.0872–76.6468 |
| 3X DIRECT普通 | 7.236 | 23.330 | 11.6395 | 71.7813–78.5822 |
| 3X COMPUTE普通 | 6.545 | 21.542 | 11.6505 | 71.9619–76.4113 |
| 3X COMPUTE / HIGH呈现 | 6.163 | 21.190 | 11.6600 | 75.9802–77.3189 |

COMPUTE普通增强区间中位降低8.93%（2X）/7.66%（3X）。这不是纯模型算术、物理显示延迟或全产品提速；该NR字段仍仅最后一层，不能乘层数充当总耗时。呈现P99基本不变；HIGH没有稳定的呈现收益，继续拒绝默认HIGH。

全部源帧跳过计数0。2X各轮生成过期2；3X DIRECT各轮3，COMPUTE普通4/4/5，HIGH各轮4；不隐藏这一差异。UI Timer最大片段83ms，不能冒充渲染帧率或证明全程无卡顿。多数轮在源帧303/304处仍有约98–109ms engine循环，graphSubmit约66–77ms；DIRECT/COMPUTE都有，R0根因继续排查。

命令：`py -3.11 -B scripts/perf/nr-graph-normal.py B2d B3c-graph-normal-v1`。原始18轮：`E:/项目/Veyra/logs/perf-nr-20261004/B3c-graph-normal-v1-*`；汇总`B3c-graph-normal-v1-summary/comparison.json`、`per-run.csv`、`artifacts.json`。当前计算队列仍是测试环境变量候选，尚未自动扩大到AMD/VFG/FSR/XeSS/HDR/节点模式；需补队列迁移/后端回退/预热/最近实例缓存验收后才能保留生产入口。

## 生产准入候选与验收边界

自动COMPUTE仅准入本机已测PCI 10DE:2F04、文件1920×1080、双层非时域1080p NR、Lecram选择、DLSS SR 4K、DLSS2X/3X、NVOF、SDR列表图。其他设备、采集、图片、HDR、VFG/FSR/XeSS、节点图、时域NR和其他尺寸保留DIRECT，不以5070证据扩大支持承诺。没有新增HIGH默认或篡改运行库/驱动。未来拓宽准入需补该组合证据。

队列只在排空后的图重建切换：先创建替代allocator/list/timestamp资源，成功后排空旧队列再交换，保留同一fence递增值。失败保留旧DIRECT，不能降为无效果伪装成功；回到DIRECT失败则旧配置恢复或停止报告。队列变化驱逐最近图，单NR缓存/预热仍走原DIRECT，不破坏已保留优化。修正discardRecording失败重建列表时固定DIRECT的类型错误。定向native逐帧全图/PTS测试包含4次往返、十项不兼容准入及队列创建失败回退，真实Qt再检查后端切换；尚未执行结果不标通过。

构建v1/v3通过。v2新增fixture误写不存在的Fsr3枚举导致编译失败，改为既有Fsr；日志build-queue-admission-v2.log保留。新增测试只在E盘隔离profile和产物运行，无压力或用户应用修改。
