# Dolby Vision P7 双层合成（BL+EL）算法笔记

来源：**ETSI GS CCM 001 V1.1.1 (2017-02)** 第 5.4 节（公开规范，本地副本
`D:\DSH\.dsh-tmp\dovi-follow\etsi\gs_ccm001.pdf`，提取文本 `etsi\alg.txt`）。
FFmpeg 的 `AVDOVIRpuDataHeader` / `AVDOVIDataMapping` 已经把 RPU 解析成这些参数
（`coef_data_type` 注释："lavc always converts to fixed"）。

## 0. 输入

- BL：基础层图像，三个分量 `S0`（luma）/`S1`,`S2`（chroma），`BL_bit_depth` 位
- EL：增强层图像，同样三个分量，`EL_bit_depth` 位；EL 分辨率通常是 BL 的水平/垂直各一半
- RPU 参数（每帧）：`curves[3]`（piece-wise）、`nlq[3]`、`coef_log2_denom`、
  `bl_video_full_range_flag`、`disable_residual_flag`、`ext_mapping_idc_0_4`

## 1. BL 逆映射（5.4.2）

### 1.1 pivot 索引（5.4.2.2）

对每个样本 `s`，找到 `pivot_value[cmp][i] <= s < pivot_value[cmp][i+1]` 的 `i`，
即 `pivot_idx`。`s` 超出范围时 clip 到首/末 pivot。

### 1.2 多项式（luma，cmp=0）（5.4.2.3.2）

```
if (s <  pivot_value[cmp][0])                        s =  pivot_value[cmp][0];
if (s >  pivot_value[cmp][num_pivots_minus2[cmp]+1]) s =  pivot_value[cmp][num_pivots_minus2[cmp]+1];

ss = 1; shift = 20;                 // 2*(maximum BL_bit_depth)
vv = 0;
for (i = 0; i <= poly_order_minus1 + 1; i++) {
    vv += poly_coef[i] * (ss << shift);
    ss *= s;
    shift -= BL_bit_depth;
}
vv = (vv < 0) ? 0 : vv;
v  = vv >> (4 + coefficient_log2_denom);
v  = (v > 0xffff) ? 0xffff : v;
```

### 1.3 MMR（chroma，cmp=1,2）（5.4.2.3.3）

需要**先把 BL luma 下采样到 chroma 分辨率**（边界按像素复制）。特征向量 `tt[]`
（20 位定点，order 1~3）：

```
tt[0] = 1<<20;
// order>=1
tt[1]=s0<<(20-BL_bit_depth); tt[2]=s1<<...; tt[3]=s2<<...;
tt[4]=(s0*s1)<<(20-2*BL_bit_depth); tt[5]=(s0*s2)<<...; tt[6]=(s1*s2)<<...;
tt[7]=(tt[4]*tt[3])>>20;
// order>=2
tt[8]=(s0*s0)<<(20-2*BL_bit_depth); tt[9]=(s1*s1)<<...; tt[10]=(s2*s2)<<...;
tt[11]=(tt[4]*tt[4])>>20; tt[12]=(tt[5]*tt[5])>>20; tt[13]=(tt[6]*tt[6])>>20;
tt[14]=(tt[7]*tt[7])>>20;
// order>=3
tt[15]=(tt[1]*tt[8])>>20; ... tt[21]=(tt[7]*tt[14])>>20;

rr = mmr_constant*tt[0];  cnt=1;
for (i=1; i<=MMR_order; i++)
    for (j=0; j<7; j++) { rr += mmr_coeff[i][j]*tt[cnt]; cnt++; }
rr = rr < 0 ? 0 : rr;
v  = rr >> (4 + coefficient_log2_denom);
v  = v > 0xffff ? 0xffff : v;
```

## 2. EL 逆量化 NLQ_LINEAR_DZ（5.4.3.2）

输出 `r` 是 **17 位有符号**：

```
T = fp_linear_deadzone_threshold[cmp];
S = fp_linear_deadzone_slope[cmp];
R = fp_hdr_in_max[cmp];

rr = e - nlq_offset[cmp];
if (rr == 0) { r = 0; }
else {
    sign = rr < 0 ? -1 : 1;
    rr <<= 1;  rr -= sign;  rr <<= (10 - EL_bit_depth);
    dq = rr * S;
    TT = (T << (10 - EL_bit_depth + 1)) * sign;
    dq += TT;
    RR = R << (10 - EL_bit_depth + 1);
    if (dq >  RR) dq =  RR;
    if (dq < -RR) dq = -RR;
    r = dq >> (coefficient_log2_denom - 5 - EL_bit_depth);
}
```

## 3. HDR 信号重建（5.4.3.3）

`out_bit_depth`：BL 的传输特性是 BT.1886 → 14；否则（ST 2084）→ `hdr_bit_depth`
（典型 12）。**我们的 P7 片源 BL 是 PQ，所以走 12 位分支。**

```
MAXOUT = (1 << out_bit_depth) - 1;
h = v;
if (!disable_residual_flag) h += r;
h += (1 << (15 - out_bit_depth));
h >>= (16 - out_bit_depth);
h = h < 0 ? 0 : h;
h = h > MAXOUT ? MAXOUT : h;
```

## 4. 转换到 ST 2084（5.5）

**BL 已经是 ST 2084 时整段跳过**（规范原文："this conversion process shall be
omitted"）。我们的 P7/HDR10 片源正好属于这种情况 —— 省掉 RGB 往返与 BT.1886 反
EOTF，是这条链路最省的一处。

## 5. 对我们的实现含义

- 每帧需要的参数全部可从 FFmpeg 的 `AVDOVIMetadata` 取：
  `rpu_data_header`（bit depth / coef_log2_denom / disable_residual_flag / ext_mapping_idc）
  + `vdr_dm_data.mapping`（curves[3] / nlq[3]）
- 多项式与 MMR 都是 20 位定点，**适合直接搬进 compute shader**（int64 需降到 32 位或
  用双 32 位模拟；`tt[]` 的 20 位定点乘法要防溢出）
- luma 走多项式、chroma 走 MMR 且需要下采样 luma —— 意味着合成 pass 不是逐像素独立，
  chroma 需要 luma 的 2×2 邻域
- EL 是 1080p、BL 是 4K：EL 需要 2× 上采样，且 Dolby 的 EL 是"半分辨率残差"

## 6. 未取到的细节（下一步要补）

- `pivot_value[]` 的具体推导（pivot 编码位宽与对齐），第 17 页开头提到但提取不全
- `fp_hdr_in_max` 与 EL 的对应关系（在 5.3.4 的语法定义里）
- `ext_mapping_idc` 的非零分支（扩展映射）是否出现在我们的样本里

（2026-10-17 更新：`pivot_value` 在本样本里就是 RPU 的 `pivots[]` 原值，见下节实测。）

## 7. 本样本的实测参数与原型结果（2026-10-17）

样本：`D:\share\Dolby Vision\[Dolby] ... Full Enhancement Layer Test`（1471 个 AU）。

**参数**（`VEYRA_DOVI_MAPPING_DUMP=<path>` 导出的前 12 帧，逐帧不同）：

```
coefLog2Denom=23  blBitDepth=10  elBitDepth=10  vdrBitDepth=12
disableResidual=0  extMappingIdc=0  blFullRange=0  nlqMethod=0 (LINEAR_DZ)
curve[0] luma  : 9 pivots [0,128,256,...,1023]，8 段全部 polyOrder=1、polyCoef=[0, 8388608, 0]
                 → 斜率 1.0，逆映射恒等（12/12 帧都是）
curve[1] Cb    : 2 pivots，单段 MMR order 3，mmrCoef=[0,6391320,0,0,0,0,0 | 3195660,0,...]
curve[2] Cr    : 同上，主系数落在 tt[2] 位置
nlq[0..2]      : offset=512, inMax=1048576, slope=2048, threshold=0
```

**关键结论**：P7 的 BL 就是标准 HDR10，**luma 逆映射是恒等**，所以合成可以简化为
`h12 = (s_bl << 6 + r_el + 8) >> 4`，EL 的贡献全部来自 NLQ 之后的那一项。

**原型验证**（Python，luma-only；脚本留在 `D:\DSH\.dsh-tmp\dovi-follow\compose_luma.py`）：

| | 值 |
|---|---|
| EL luma 非零比例（第 3 帧） | 74.7%（504..589，零点是 512） |
| BL-only 均值 | 267.32（12bit） |
| 合成后均值 | 283.60（**+6.0%**） |
| 差异 | max +77 / min −7，74.5% 像素有变化 |
| 差异分布 | 光滑区整体提亮 + 细节区高频补足 + 上下黑边完全不动 |

**一个必须在产品实现里处理的问题**：EL 的前两个 AU 解码输出**全 512**（零残差），第 3 帧起
才有真实残差；与 dovi_tool 的 EL 流错误数完全一致，说明这是 EL 流自身的参考结构问题
（不是提取错误）。所以实现里需要"EL 帧不可用时按零残差回落"，而不是丢弃整帧。

**还没做的**：chroma 的 MMR 合成、EL 的 2× 上采样用双线性（原型用的最近邻，会出块效应）、
用 Control 片做正确性对拍。

## 8. 实测：EL 的覆盖率与解码策略（2026-10-17）

Dolby FEL 测试片全片统计（`D:\DSH\.dsh-tmp\dovi-follow\stats_el.py`）：

```
blocks=2878  (= BL 帧数，每块 1 个 VCL NAL)
withEl=1471  (51.1%)
elFirstSlices=1471         每个带 EL 的 block 恰好一整帧
每块 EL slice 数：{0: 1407, 8: 1471}
节奏：111000 111000 …      前 3 帧有 EL、后 3 帧没有
```

**结论：EL 不是逐帧都有的**。所以：

1. "EL 前两帧全 512" 不是解码失败，而是那两帧**本来就没有 EL**
2. 解码器会看到**不连续的参考链**，默认策略是把后续帧全丢弃 → 必须容错解码：
   `err_recognition = 0` + `AV_CODEC_FLAG_OUTPUT_CORRUPT`
3. "空包"是常态（连续 3 帧没有 EL 是设计），**不能**因为空包就禁用 EL 解码
4. 合成时：没有 EL 的帧按零残差处理（等价于 `r = 0`）

实测（`VEYRA_DOVI_EL_DECODE=1`，12 秒播放）：解码器用 EL 自己的 SPS 打开
（722 字节 extradata，1920x1080），`decoded` 持续增长到 100+，无崩溃，
默认路径（不带开关）行为完全不变。

对应代码：`MediaFileSource::feedEnhancementLayer`（提取 type 63 → 剥 `7E 01` →
只送 VCL NAL → 容错软解），开关 `VEYRA_DOVI_EL_DECODE`。

## 9. 性能与回落（2026-10-17）

**性能**（4K FEL 片，12 秒，EL 软解 2 线程）：

```
[source-dovi-el] decoded=2   avgCost=5.54ms over 4 packets     ← 解码器预热
[source-dovi-el] decoded=100 avgCost=0.47ms over 104 packets   ← 稳定后
```

24 fps 每帧预算 41.7 ms，EL 平均只占 **约 1%**，而且只有一半的帧带 EL。
GPU 侧只是多一张 1080p R16 纹理和每帧一次行拷贝上传。结论：性能不是问题。

**回落**（开关打开但片源/参数不适用）：

| 情况 | 行为 | 验证 |
|---|---|---|
| Profile 8 片源（无 EL） | 条件 `profile==7 && el && 开关` 不成立，第二条解码器根本不开 | 打开开关播 p8_30s：**0 条 EL 日志**，exit=0 |
| RPU 无法表达（非恒等 luma 曲线 / 扩展映射 / 非线性 NLQ） | `doviP7ConstantsSupported` 返回 false，`dv6.w` 保持 0，走原路径 | 单测 `DV P7 constants refuse a non-identity luma curve` |
| 该帧没有 EL（EL 稀疏，51%） | 不上传纹理，`dv6.w` 保持 0，走原路径 | 与"零残差合成"数值等价，差 <0.1% |
| EL 解码器打不开 / 送包被拒 | 累加后禁用整条 EL 路径，不影响 BL | 代码路径 |
| 开关关（默认） | 完全不进这条路径 | 默认冒烟 exit=0 |

**结论**：性能可忽略，回落是分层的（片源级 / 参数级 / 帧级 / 解码器级），任何一层出问题都退回
已验收的 BL+RPU 播放，不会只剩黑屏或错误画面。

## 10. 画面验证（2026-10-17）

做了两次运行对齐的亮度对比（各连拍 10 帧，播放位置已清空，避免"继续播放"错位）：

```
关闭增强层: 17.8 18.3 18.7 19.1 19.5 19.8 20.1 20.6 20.9 21.2
开启增强层: 17.6 18.0 18.5 18.8 19.2 19.6 20.0 20.3 20.7 21.0
逐帧 |差异| = 0.23~0.29，整体 −1.2%
```

**结论**：增强层确实在改变画面（关闭时应当逐帧完全相同）。方向偏暗是因为该段
EL 残差偏负；残差本身围绕零点对称（实测 −13…+17），不同片段方向不同。

### 验证过程中踩到的三个坑（都会伪装成"功能没生效"）

1. **`dv6.w` 写到了 `params[24]`（dv6.x）**，着色器读到的永远是 0 → 合成从未执行；
   而单测断言写的是同一个错槽位，所以测试一直绿。
2. **着色器从未部署**：只复制了 exe，`.dxil` 还是旧的（20596 vs 21808 字节）。
3. **播放位置记忆**：两次运行第二次会从上次位置继续，画面根本不同步，对比毫无意义。

### 一个仍未定论的判断

离线统计 mkv block 得到 EL 覆盖率 51%（`111000` 节奏），但运行时
`decoded=1200 / 1238 packets` ≈ 97%，采样时也几乎每帧都有 EL。两者矛盾，
说明当初的 block 统计口径有问题（很可能把 BlockGroup 漏了）。**当前实现按
"这一包是否解出 EL 帧"判断**，这在残差诊断里表现正常，但覆盖率数字仍待重新统计。

### 诊断开关（都默认关闭）

- `VEYRA_DOVI_P7_DUMP=1`：着色器把 EL 残差直接画成灰度（黑 = 该帧无残差），
  配合 `shaders/YuvToLinearRgb.hlsl` 里的 `p7Dump` 分支使用
- `VEYRA_DOVI_TRACE=1`：逐帧 RPU 与合成绑定计数
- 界面开关：专业页 → 杜比视界设置 → 合并增强层（默认关）
