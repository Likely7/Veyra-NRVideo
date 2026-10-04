# 4a 多层NR低分辨率整链候选

before cefbaec / checkpoint/perf-nr-4a-low-chain-before-20261004。固定上游SAOG0721/Magpie 27c5df91177a29b33be612e98274169f3d2fca49 / v0.6.9-experimental，GPL-3.0；确切cpp/header/license及URL/SHA收据E:/项目/Veyra/deps/saog-magpie-research-20261003/4a-27c5df91177a29b33be612e98274169f3d2fca49-receipt.json。源码注释与THIRD_PARTY_NOTICES逐项标注。

按本次授权选择“保留每层独立残差参数”：仅2/3层同内部尺寸、非时域SDR列表图，第一层从原图降到L0；每层在低尺寸应用自身total/darken/brighten/color/luminance，下一层借用该低尺寸结果；出口一次LN-L0双边升采样叠回原全尺寸底图，完整全尺寸保护区域保留。现有单层、异尺寸、节点/交错、NR前于SR、时域/HDR/AMD保持原路径，不移除已支持参数。

初始入口仅隔离测试ENV VEYRA_TEST_NR_LOW_CHAIN，默认不启用，尚无生产UI。不会以需要不同画面的多层链替换单层路径。特性创建/历史依旧每层独立，保留fresh-list约束，不照搬上游一次list批量Evaluate。内存预算检查保持保守，不做显存压力。

先构建，再测：单层与不兼容组合逐帧SHA相同、真实多层debug/PTS/层次数及效果不隐失，独立残差改动可达；正常Qt2/3层A/B/B-off交错测总增强与Residual，不仅最后NR字段。多层完整图片PSNR/差异与可审查对照图保留，未经人工画质确认不默认打开。没有收益、正确性失败或不可接受画面时回退并保留失败证据。
