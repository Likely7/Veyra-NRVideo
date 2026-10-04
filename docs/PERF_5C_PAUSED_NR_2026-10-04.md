# 5c：暂停GPU与残差编辑候选

现有Engine暂停循环不重复调用EnhanceGraph，仅每50ms重新呈现保留帧；普通暂停不跑NR已实现。仍需实际30秒计数/GPU测量与设置刷新回归，不能仅从代码推断整卡GPU占用为零。

前置实测 `pause-reset-M1-v1`：M1第一张自然图像、300次相同输入，每次明确reset，单层NR三轮输出均逐字节相同；NR+DLSS SR三轮各262张不同。各轮native安全通过、debug0、设备正常，原始数据 `E:/项目/Veyra/logs/perf-nr-20261004/pause-reset-M1-v1-*`。因此不能将暂停复用推广到超分链路。

候选只在NVIDIA单层、无抗闪烁/稳定器、无SR/FG/HDR/颜色节点、相同不可变暂停AVFrame/源序号/PTS情况下，复用一次成功“暂停刷新且reset”的NR结果；首个暂停编辑完整处理，不复用此前连续播放的历史。只改残差参数（其余完整EnhancementSettings逐项相同）才复用；模型/颜色/源/seek/普通播放/失败/重建全部失效。保留原残差着色器和输出/租约/fence路径，无GPU像素回读。

`VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE=1` 在同EXE回退；`paused-nr-cache event=reuse` 与真实NR Evaluate计数分别记录。原生实验每组300次暂停残差编辑，150次时改变NR模型以验证失效；保存完整输出SHA和关键帧。优化前EXE已封存 `E:/项目/Veyra/tests/perf-nr-20261004/A5c-paused-v1-app/`，SHA066ac43c6df06819b181e564fb2a42ed2b1d82441f3fc31837bdbf0d07986045。

当前为待构建/待A-B-B-off候选；没有实际收益或保留决定。SR、抗闪烁、多层组合必须保持完整处理并验证输出。真实Qt暂停30秒、连续编辑、模型/源切换和恢复播放仍待回归。不设置灰色空壳开关。
