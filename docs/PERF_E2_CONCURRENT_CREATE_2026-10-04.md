# E2 同设备并行NR创建实验

## 结论

本机RTX5070/616.56、默认Lecram运行库上，**同一个NGX核心/NR适配器会话**中的旧Feature持续Evaluate，同时后台线程创建、Evaluate、释放另一Feature，可以完成本轮压力测试，无SEH、D3D12错误或设备移除。不能用这个结果证明两个独立Init/Shutdown/IAT会话能并行；产品2b必须共享同一个会话，不能直接同时初始化两张旧结构的图。

并发安全通过不代表切换无停顿。短测最大前台Evaluate完成间隔108.7962ms，三次240秒压力测最大间隔110.5127/110.4173/115.9049ms，明显大于60fps的两帧33.3ms。当前实验使用逐次等待的诊断路径，不是实际Present；2b还需真实输出、命令槽/队列调度证据，不能直接写“无缝”。

## 实验与实测

`tests/perf/NrConcurrentCreateExperiment.cpp`调用产品库。前台1920×1080；后台轮换1280×720、1920×1080、960×540，参数块在启动线程前分配，避开当前CoreHost跟踪数组的并发写入。独立allocator/list和**独立fence/event/timeline**，共享同一D3D12设备/direct queue及单个适配器/IAT所有者。所有返回值/SEH/HRESULT保留；主/后台各自图像资源与历史独立。

| 运行 | 前台Evaluate次数 | 后台Create/Evaluate/Release | 最大前台完成间隔ms | debug/设备错误 |
|---|---:|---:|---:|---:|
| 串行控制12秒 | 629 | 10/10/10 | 66.9154 | 0 |
| 并发短测12秒 | 651 | 10/10/10 | 108.7962 | 0 |
| 并发240秒，轮1 | 13,614 | 50/50/50 | 110.5127 | 0 |
| 并发240秒，轮2 | 13,606 | 50/50/50 | 110.4173 | 0 |
| 并发240秒，轮3 | 13,607 | 50/50/50 | 115.9049 | 0 |

长测是**三个独立进程各4分钟，累计12分钟**，不是一次连续10分钟；每进程上限280秒，符合当前300秒测试限制。三个长测释放后DXGI用量均92,545,024字节，串行/并发短测也是相同值；起点初始化后35,815,424字节，不能把差额武断叫泄漏或宣称全部归零。后台循环中的释放后显存保持同一主Feature占用，未见随50次创建线性增长。

静态输入前台输出在30次预热后与后续不同次数Evaluate后SHA不同，但各运行30次预热的SHA相同。这提示NR有历史依赖，不能由“源像素一样”推断再次Evaluate结果一样；1a须做固定次数/A-A噪声底和复用对照。本实验没有把这种历史变化直接归因于并发错误。

## 可重现证据与边界

- build-E2-v1.log，`nr-build.py E1 build-E2-v1 veyra_nr_concurrent_create_experiment` exit0。
- `nr-e2.py E2-v1`完成5组，driver/native退出0；summary与各组console/engine/background.csv/result.json及before/after.rgba在`E:/项目/Veyra/logs/perf-nr-20261004/E2-v1*`。
- EXE SHA256 `3a2c90318c9e9524b027173f82b075cfc2136d89af0d40e0172d2219f3bee68b`。
- NR SHA256 `f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc`。

没有测原版/Ampere/SF-v2、HDR、SR/DLSSG混合并发、真实Present/音画、天然画质或其他GPU。2b只能按本机已验证的单会话设计继续实验，原路径需要作为回退。
