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

初始真实UI v3与v4未采集进程级GPU计数；整卡nvidia-smi暂停读数14–19%不能归因于本进程。新增只读取测试PID的Windows PDH GPU Engine计数器后补做A/B/B-off。暂停NR空闲与界面/呈现GPU占用分别记录，不据NR计数0宣称整个软件GPU占用0。补测细节见下节；R0全产品回归仍待完成。

计数器依据：[微软PdhGetFormattedCounterArrayW](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw)、[PDH_FMT_COUNTERVALUE](https://learn.microsoft.com/en-us/windows/win32/api/pdh/ns-pdh-pdh_fmt_countervalue)。读取结果只保留本轮测试PID实例，缺失计数器报告unavailable，不用0代替未测。

## 呈现与界面空闲补测

仅靠暂停NR不求值，自身GPU仍约2.3%，不能标验收通过。定位到两条实际开销：Engine每50ms重新Present相同输出；专业页性能球每秒数据变化触发600ms Canvas动画。暂停呈现现在按成功生产者fence、帧身份、slot、view、对比模式/分割、实际client与buffer尺寸、显示/隐藏、显示器判断保留；互动拖动/DPI与尚未落实resize不能命中，设备检查/失败缓存失效保持。仅明确暂停调用启用；普通播放、导出没有去重。暂停性能球仍每秒更新数值，取消数值动画，正常播放保持。

`nr-present.py B5c B5c-present-v1`：真实产品Graph、VideoPresenter及本轮拥有的HWND，启用/关闭各三轮。300次相同暂停调用时实际Present分别1/301次；zoom、compare/split、resize、NR残差生产者变更、隐藏/恢复各迫使正确刷新。每轮八张完整RGBA8图像、对应启用/关闭共48张，全部SHA一致，debug错误0、设备正常。此项不是仅检查函数返回。

真实Qt `B5c-paused-ui-v6` 与同EXE回退 `B5c-paused-ui-v6-off`：单NR、NR+DLSS SR、抗闪烁各一次，每次30秒暂停、稳定区24个PID专属PDH样本、20次含模型变更的残差编辑、恢复播放及暂停跳转全部通过。单NR/超分/抗闪烁进程最大引擎占用的稳定样本中位数：B为0.010828/0.011633/0.010752%，B-off为2.287588/2.318904/2.250025%；优化前A旧EXE+旧QML单NR为2.360637%。这些是进程GPU引擎利用率，不是整卡利用率、视频帧率或实屏刷新率。单NR三轮数据如下。

取消重复Present但保留性能球动画的中间版本v5仍2.261%，没有把它算作完整暂停优化。v6单NR Qt真实frameSwapped每10秒约8–10次，A约1356–1734次；此计数只描述Qt渲染窗口。GPU解码、publicBridge、独立profile，没有桌面自动点击。`B5c-paused-ui-v6-fg` 的实际DLSS/XeSS/FSR3/VFG 2X四组30秒暂停、恢复、跳转均通过，未把后端初始化回退算通过；NR残差复用在这些组合保持关闭。

同EXE关闭开关：`VEYRA_TEST_DISABLE_PAUSED_PRESENT_REUSE`、`VEYRA_TEST_DISABLE_PAUSED_UI_IDLE`、`VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE`。产品EXE bf4952347424f500df45a29ab3f7289742e4ecae188ac66b71805b9743978568；原生呈现实验EXE 0184961e015605a919bf1cd6cfb4641d6f5db56ed6ddd49723afe57527478b75。原始证据均在 `E:/项目/Veyra/logs/perf-nr-20261004/` 对应标签目录。

单NR最终三轮（每轮24个稳定样本）GPU最大引擎利用率的中位数再取三轮中位数：A 2.237185%（2.236872–2.360637），B 0.010999%（0.010828–0.011035），同EXE B-off 2.237174%（2.193295–2.287588）。B约为A的0.492%，下降99.51%；这是暂停软件自身GPU引擎占用的变化，不是GPU整卡空闲保证。九轮暂停/残差/模型/恢复/跳转全通过。A为本节点开工前已保留2a的B2a构建及旧QML，EXE b92c47b64a36036a9c708fc55b018512f90c7945606236c7f5aca4b5f213679b；最初全项目A便携包继续封存，未用新QML改写基线。JSON `B5c-paused-ui-v6-three-run-comparison.json`。四个实际FG后端暂停稳定区GPU样本中位数0.009983–0.010625%，各24样本；该四组各一轮，不扩大为三轮长稳。

结论：暂停残差缓存与呈现/性能球空闲优化保留；首次暂停残差刷新和模型变更仍完整NR求值，随后纯残差编辑不求值。只对已测窄范围缓存NR，通用暂停呈现适用于已回归后端。R0仍需验证长期播放、导出及最终完整包，不宣称整个优化方案完成。

## 收尾决定

最终保留以上实现及原测量，最终产品9403521与合同/实际UI/原生/worker回归通过，详`PERF_R0_ACCEPTANCE_2026-10-05.md`。OBS诊断曾恢复暂停呈现旧路径并关闭残差复用，仍不能修复GPU UI采集；这些无效恢复候选已拒绝，原优化恢复。严格封存A也复现全屏只抓Qt背景，不能将阶段相关性称5c根因；所有失败数据和回退档保留。OBS软件UI兼容真实六场景正常，GPU UI游戏采集限制仍未修复。正常运行与物理显示/真实主机长稳分别报告，不把暂停GPU低占用扩展为所有负载丝滑保证。
