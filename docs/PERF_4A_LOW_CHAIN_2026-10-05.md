# 4a 多层NR低分辨率整链候选

before cefbaec / checkpoint/perf-nr-4a-low-chain-before-20261004。固定上游SAOG0721/Magpie 27c5df91177a29b33be612e98274169f3d2fca49 / v0.6.9-experimental，GPL-3.0；确切cpp/header/license及URL/SHA收据E:/项目/Veyra/deps/saog-magpie-research-20261003/4a-27c5df91177a29b33be612e98274169f3d2fca49-receipt.json。源码注释与THIRD_PARTY_NOTICES逐项标注。

按本次授权选择“保留每层独立残差参数”：仅2/3层同内部尺寸、非时域SDR列表图，第一层从原图降到L0；每层在低尺寸应用自身total/darken/brighten/color/luminance，下一层借用该低尺寸结果；出口一次LN-L0双边升采样叠回原全尺寸底图，完整全尺寸保护区域保留。现有单层、异尺寸、节点/交错、NR前于SR、时域/HDR/AMD保持原路径，不移除已支持参数。

初始入口仅隔离测试ENV VEYRA_TEST_NR_LOW_CHAIN，默认不启用，尚无生产UI。不会以需要不同画面的多层链替换单层路径。特性创建/历史依旧每层独立，保留fresh-list约束，不照搬上游一次list批量Evaluate。内存预算检查保持保守，不做显存压力。

先构建，再测：单层与不兼容组合逐帧SHA相同、真实多层debug/PTS/层次数及效果不隐失，独立残差改动可达；正常Qt2/3层A/B/B-off交错测总增强与Residual，不仅最后NR字段。多层完整图片PSNR/差异与可审查对照图保留，未经人工画质确认不默认打开。没有收益、正确性失败或不可接受画面时回退并保留失败证据。

## 完整图/控制与数值变化

`nr-low-chain.py B2d B4a-native-v2`10组合各off/on共20/20通过、800完整PNG/图像SHA/PTS保留，debug/设备移除0。全部每源帧NR Evaluate次数等于实际层数，未省掉NR求值伪装提速；尺寸由资源Desc验证。单层720p/单层NR+SR4K、混合480→720、双层时域NR四组各40帧开/关完全一致。双/三层720p、双/三层1080p+SR4K、保护区域、独立残差编辑六组各40帧均有像素变化，这是拓扑变化，不称为“画面完全等同”。各层残差在源帧10/20/30单独修改并实际查询正确。

`nr-low-chain-quality.py B4a-native-v2`逐张核验PNG解码RGBA SHA等于native CSV，alpha全部一致。保护区域中心25%–75%严格全像素一致（每帧0差异）。全帧RGB8 PSNR中位：双720 52.56dB、三720 49.25、双1080+SR 53.86、三1080+SR 50.98、保护54.45、编辑53.97；变化像素中位约36%–73%，多为细小数值变化，但最大通道差最高81/255，不能仅据PSNR判画质合格。

完整对照HTML：E:/项目/Veyra/logs/perf-nr-20261004/B4a-native-v2-quality/review.html；comparison.json含逐帧差异、alpha、保护区，原800 PNG及所有raw receipts保留。本轮视觉检查双NR+SR第39帧全图缩至2048像素的对照没有大范围黑块或几何破坏，这不能代替原尺寸细节/运动与用户人工验收。v1因fixture未初始化COM导致第一张PNG失败，v3构建修复COM/RAII后v2测试通过，原失败日志保留。

接下来正常Qt2/3层SR4K各off/on三轮交错验证实际全增强、残差及源跳过/成功软件Present；未执行计时不写收益。默认仍关闭。
