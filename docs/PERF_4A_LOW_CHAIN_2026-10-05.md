# 4a 多层NR低分辨率整链候选

before cefbaec / checkpoint/perf-nr-4a-low-chain-before-20261004。固定上游SAOG0721/Magpie 27c5df91177a29b33be612e98274169f3d2fca49 / v0.6.9-experimental，GPL-3.0；确切cpp/header/license及URL/SHA收据E:/项目/Veyra/deps/saog-magpie-research-20261003/4a-27c5df91177a29b33be612e98274169f3d2fca49-receipt.json。源码注释与THIRD_PARTY_NOTICES逐项标注。

按本次授权选择“保留每层独立残差参数”：仅2/3层同内部尺寸、非时域SDR列表图，第一层从原图降到L0；每层在低尺寸应用自身total/darken/brighten/color/luminance，下一层借用该低尺寸结果；出口一次LN-L0双边升采样叠回原全尺寸底图，完整全尺寸保护区域保留。现有单层、异尺寸、节点/交错、NR前于SR、时域/HDR/AMD保持原路径，不移除已支持参数。

初始入口仅隔离测试ENV VEYRA_TEST_NR_LOW_CHAIN，默认不启用，尚无生产UI。不会以需要不同画面的多层链替换单层路径。特性创建/历史依旧每层独立，保留fresh-list约束，不照搬上游一次list批量Evaluate。内存预算检查保持保守，不做显存压力。

先构建，再测：单层与不兼容组合逐帧SHA相同、真实多层debug/PTS/层次数及效果不隐失，独立残差改动可达；正常Qt2/3层A/B/B-off交错测总增强与Residual，不仅最后NR字段。多层完整图片PSNR/差异与可审查对照图保留，未经人工画质确认不默认打开。没有收益、正确性失败或不可接受画面时回退并保留失败证据。

## 完整图/控制与数值变化

`nr-low-chain.py B2d B4a-native-v2`10组合各off/on共20/20通过、800完整PNG/图像SHA/PTS保留，debug/设备移除0。全部每源帧NR Evaluate次数等于实际层数，未省掉NR求值伪装提速；尺寸由资源Desc验证。单层720p/单层NR+SR4K、混合480→720、双层时域NR四组各40帧开/关完全一致。双/三层720p、双/三层1080p+SR4K、保护区域、独立残差编辑六组各40帧均有像素变化，这是拓扑变化，不称为“画面完全等同”。各层残差在源帧10/20/30单独修改并实际查询正确。

`nr-low-chain-quality.py B4a-native-v2`逐张核验PNG解码RGBA SHA等于native CSV，alpha全部一致。保护区域中心25%–75%严格全像素一致（每帧0差异）。全帧RGB8 PSNR中位：双720 52.56dB、三720 49.25、双1080+SR 53.86、三1080+SR 50.98、保护54.45、编辑53.97；变化像素中位约36%–73%，多为细小数值变化，但最大通道差最高81/255，不能仅据PSNR判画质合格。

完整对照HTML：E:/项目/Veyra/logs/perf-nr-20261004/B4a-native-v2-quality/review.html；comparison.json含逐帧差异、alpha、保护区，原800 PNG及所有raw receipts保留。本轮视觉检查双NR+SR第39帧全图缩至2048像素的对照没有大范围黑块或几何破坏，这不能代替原尺寸细节/运动与用户人工验收。v1因fixture未初始化COM导致第一张PNG失败，v3构建修复COM/RAII后v2测试通过，原失败日志保留。

## 普通播放实测与拒绝决定

`py -3.11 -B scripts/perf/nr-low-chain-normal.py B2d B4a-normal-v1 2-sr 3-sr`：12/12普通播放进程完成。M1自然1080p30、NR内部1080p、DLSS SR4K、FG关闭、普通GPU优先级；每组50秒，三轮交错off/on。无额外GPU程序、无人工显存压力，固定EXE/DLL/QML前后SHA全同。收据：E:/项目/Veyra/logs/perf-nr-20261004/B4a-normal-v1-summary/comparison.json、artifacts.json及12组原始日志。

| 三轮中位，ms | 2层off | 2层on | 3层off | 3层on |
|---|---:|---:|---:|---:|
| 增强整体滚动均值 | 18.319 | 18.120 | 25.029 | 24.873 |
| 最后NR GPU P95 | 7.237 | 7.110 | 7.352 | 7.227 |
| 最后残差 GPU P95 | 0.824 | 0.615 | 0.836 | 0.607 |
| GPU就绪 P95 | 20.094 | 19.664 | 27.374 | 26.509 |
| 成功软件Present间隔 P99 | 33.9865 | 34.0154 | 33.9853 | 34.0306 |

整体仅降1.09%/0.62%，最后残差降25.36%/27.39%，不能将残差局部降幅写成整链提速。三层整体off范围25.001–25.043、on24.642–24.914；跨轮变化已超过中位收益。所有源跳过0，但尾部并未改善：两层最大间隔off69.739–75.9344/on70.7844–75.4162ms；三层off78.7401–113.9344/on110.5396–113.4787ms。显存约两层2512→2433MiB、三层3039→2831MiB，属节省内存，不能代替流畅度收益。

决定拒绝将此候选保留为产品路径并明确回退。理由是完整耗时收益小且呈现没有改善，伴随全图数值变化（最高81/255）及独立参数执行语义变化，尚无人工画质通过；不是声称所有数字均负优化。800 PNG、完整质量报告、正常播放日志、候选源码及EXE封存供追溯，不删除失败证据。

另定位R0：三层on-r1 source304的graph CPU109.950ms，其中最后一次snippetEvaluateFeature108.279ms；对应最后NR GPU约6.7ms。此处是运行时CPU调用长帧，不能把GPU执行时长冒充整个调用或断言根因已修复。
