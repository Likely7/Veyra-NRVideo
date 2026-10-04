# 5c：暂停GPU与残差编辑候选

现有Engine暂停循环不重复调用EnhanceGraph，仅每50ms重新呈现保留帧；普通暂停不跑NR已实现。仍需实际30秒计数/GPU测量与设置刷新回归，不能仅从代码推断整卡GPU占用为零。

前置实测 `pause-reset-M1-v1`：M1第一张自然图像、300次相同输入，每次明确reset，单层NR三轮输出均逐字节相同；NR+DLSS SR三轮各262张不同。各轮native安全通过、debug0、设备正常，原始数据 `E:/项目/Veyra/logs/perf-nr-20261004/pause-reset-M1-v1-*`。因此不能将暂停复用推广到超分链路。

候选只在已测Lecram运行库（NrRuntime=0）、NVIDIA单层、无抗闪烁/稳定器、无SR/FG/HDR输入输出/颜色节点、相同不可变暂停AVFrame/源序号/PTS情况下，复用一次成功“暂停刷新且reset”的NR结果；首个暂停编辑完整处理，不复用此前连续播放的历史。只改残差参数（其余完整EnhancementSettings逐项相同）才复用；模型/颜色/源/seek/普通播放/失败/重建全部失效。保留原残差着色器和输出/租约/fence路径，无GPU像素回读。

`VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE=1` 在同EXE回退；`paused-nr-cache event=reuse` 与真实NR Evaluate计数分别记录。原生实验每组300次暂停残差编辑，150次时改变NR模型以验证失效；保存完整输出SHA和关键帧。优化前EXE已封存 `E:/项目/Veyra/tests/perf-nr-20261004/A5c-paused-v1-app/`，SHA066ac43c6df06819b181e564fb2a42ed2b1d82441f3fc31837bdbf0d07986045。

## 三轮实际A/B/B-off

`A5c-paused-v1`、`B5c-paused-v4`、`B5c-paused-v4-off`各12组通过：四配置×300次编辑×三次，共10800个完整RGBA8 SHA256。三轮A-A噪声0，全部B/A、B-off/A对应输出0差异，D3D12 debug错误0、设备正常。每组第150次改模型，验证缓存失效。表为排除首次和模型改变两次完整求值后的CPU处理含GPU完成等待中位数，再取三轮中位数；不是屏幕延迟或纯GPU时间。

| 配置 | A ms | B ms | B-off ms | 每轮NR求值 A/B/B-off |
|---|---:|---:|---:|---|
| 单NR | 7.46140 | 0.76570 | 7.50225 | 300 / 2 / 300 |
| NR+DLSS SR | 10.92785 | 10.90910 | 10.96415 | 300 / 300 / 300 |
| NR抗闪烁 | 7.57300 | 7.60030 | 7.58155 | 300 / 300 / 300 |
| 两层NR | 14.42515 | 14.44390 | 14.45600 | 600 / 600 / 600 |

单NR减少89.74%，B三轮中位范围0.76235–0.76820ms，A7.45805–7.49265ms，B-off7.49735–7.53375ms。其他组合约0.4%以内波动，不算优化收益。比较JSON：`E:/项目/Veyra/logs/perf-nr-20261004/B5c-paused-v4-comparison.json`；候选原生EXE SHA256 `cedd84884f9d83af09ab7ab54b78ee9a7421f95ec875fd21083c0a4878e803f6`。

v1/v2未产生收益：测试单层使用legacy平铺参数但保留数组，残差分类未归一化未启用数组。v3将全部保留槽的残差参数归一化，模型/拓扑仍严格比较；v4进一步限制未测运行库/HDR/SR/FG请求。失败证据保留，没有将“API通过但未复用”算收益。

## 真实Qt行为

`B5c-paused-ui-v4`三配置均通过。静止暂停30.0–30.5秒，真实NR计数不增长、媒体位置不漂；20次残差请求内第10次换模型，单NR计数162→164并记录19次缓存复用，恢复播放164→228，暂停跳转2秒后230。SR组合155→176、抗闪烁161→182，缓存命中均0，维持完整处理。实际GPU解码、公共Bridge、独立profile、无桌面自动点击。

初始真实UI v3与v4未采集进程级GPU计数；整卡nvidia-smi暂停读数14–19%不能归因于本进程。新增只读取测试PID的Windows PDH GPU Engine计数器后补做A/B/B-off。暂停NR空闲与界面/呈现GPU占用分别记录，不据NR计数0宣称整个软件GPU占用0。此节点缓存有收益，最终保留及暂停占用结论在补测后记录；R0全产品回归仍待完成。

计数器依据：[微软PdhGetFormattedCounterArrayW](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw)、[PDH_FMT_COUNTERVALUE](https://learn.microsoft.com/en-us/windows/win32/api/pdh/ns-pdh-pdh_fmt_countervalue)。读取结果只保留本轮测试PID实例，缺失计数器报告unavailable，不用0代替未测。
