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

## 队列迁移与真实UI结果

`nr-queue-migration.py B2d B3c-migration-v1`九组（DIRECT / 自动 / 注入创建失败，各三次）通过，864真实+414生成=1278完整图像SHA/尺寸/PTS逐行一致，A-A差异0；每个自动进程四次队列往返，fence严格递增（最终453），D3D12 debug错误/设备移除0。十项不兼容准入检查各进程通过。注入E_OUTOFMEMORY只验证错误分支，不冒充真实资源耗尽或设备移除试验；没有制造显存压力。

真实Qt：FSR3.1 `B3c-queue-ui-v3-fsr3/phase-review.json`、XeSS `B3c-queue-ui-v4-xess/result.json`、VFG `B3c-queue-ui-v5-vfg/result.json`通过。每组八个已稳定增强阶段的实际队列为COMPUTE、DIRECT、COMPUTE、DIRECT、DIRECT、COMPUTE、DIRECT、COMPUTE，覆盖DLSS2→其他provider→DLSS3→FG关闭→调色开启/关闭→时域开启/关闭和停止。全部NR/SR/FG效果真实启用，结束没有SDK/debug/退出错误。单NR预热/缓存没有改变准入，完整R0仍待。

保留失败证据：v1脚本把UI后端写成fsr（实际是fsr3），退出后脚本仍改设置，引发创建swapchain失败和退出时C0000005；修正断言终止后没有复现崩溃，但退出/重建竞态列R0，不当成已修复。v2/v4-VFG断言在相邻两次重建尚未完全出生成帧时过早检查FG，改为等待实际3X/稳定有效状态，18秒硬超时不掩盖永久失败。v3-FSR原收据按固定七次队列变化误判：两个UI请求中间实际有一次合法COMPUTE图，共九次变化；保留原false收据，新增phase-review独立按已稳定阶段审核，全部八阶段队列正确。未删除/重写原始日志或收据。该回归不证明切换无停顿，重建仍有约1.6–1.9秒成本。

结论：保留限定准入的自动COMPUTE，约8%的增强区间收益仅限已测组合；HIGH不作为默认，其他设备/组合仍DIRECT。源码逐节点存档，R0最终产品回归未完成。
