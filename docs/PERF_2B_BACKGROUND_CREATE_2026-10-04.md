# 2b：后台创建未通过连续出帧门槛

前存档034c2d8 / checkpoint/perf-nr-2b-before-20261004。E2仅证明并行Create/Evaluate安全，不能证明无缝。新增诊断将每次前台NR实际新Evaluate的输出复制到自有1920×1080窗口并成功Present，不用旧增强输出掩盖创建停顿。一个Core/adapter，两个独立allocator/list/fence，必要GPU依赖保留；静态作者图案，不是实际游戏或音画漂移测试。

RTX5070/616.56，Lecram F95；3次serial/concurrent，每次前台60秒、后台50次创建/求值/释放、三尺寸轮换。serial先完成50创建再开始60秒连续出帧，concurrent与前台重叠。6/6安全通过，300创建/释放，所有Present行sequence==新Evaluate计数，无debug错误/设备移除，最终释放显存稳定。软件成功Present返回间隔三轮中位数（ms）：

| 指标 | serial | concurrent |
|---|---:|---:|
| P50 | 15.8804 | 15.8723 |
| P95 | 31.1031 | 31.3610 |
| P99 | 32.0436 | 74.1890 |
| 每轮最大值的中位数 | 181.061 | 232.528 |
| 超过两个60Hz间隔次数中位数 | 3 | 55 |

并行P99增加131.5%，三轮超33.333ms次数53/55/56；最坏一次1033.39ms。serial也有74.691/181.061/193.676ms尖峰，不能把每次系统/DWM尖峰全归因Create，但并行反复增加尾部间隔，未满足原案最大间隔<两个帧间隔。此路径不进入产品，无生产变更需要回退；诊断和失败证据保留，不泛化为所有未来后台策略均不可行。3c若有新证据可另开修订实验。

命令：nr-concurrent-present.py B3a B2b-fresh-present-v1，build-cache-sr-concurrent-present-v1 exit0。原始每帧present.csv、GPU环境、JSON、命令与EXE/runtime身份位于E:/项目/Veyra/logs/perf-nr-20261004/B2b-fresh-present-v1-*及summary；每个进程280秒上限，GPU测试/构建串行。未取得物理扫描或真实AV漂移证据，不标无缝切换完成。
