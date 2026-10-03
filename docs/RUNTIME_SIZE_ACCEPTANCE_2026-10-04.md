# VFG / AMD NR 瘦身验收（2026-10-04）

本轮在 `codex/runtime-size-20261004`，基线 `fc2ca716`，先存档/方案再施工。
只修改 VFG 依赖加载与本地打包/验证脚本，没有改模型、HIP 内核、HDR 算法或功能档位。
包含极简窗口像素缺口修复；HDR/DV PR #13/#14 仍等待审批，未合并 main、未推送、未发布。

## 大小与可删范围

| 部分 | 原运行目录 | 新运行目录 | 处理 |
| --- | ---: | ---: | --- |
| VFG | 497,894,432 bytes（474.83 MiB） | 210,788,400 bytes（201.02 MiB） | 移除九个未使用的 NPP DLL，共 287,106,032 bytes（273.81 MiB，57.7%）。 |
| AMD NR | 615,032,656 bytes（586.54 MiB） | 同原件 | 186 权重、2映射、62 HIP内核及原有回退保留。 |

VFG 原包把通用 CUDA/NPP 图像处理库一起打入。当前后端直接包装编码好的共享 CUDA buffer，
没有调用 NvCVImage_Transfer/NPP 转换；PE import/delay-import 与实际进程模块检查一致。
五个保留文件为 cudart64_12.dll、NVCVImage.dll、nvngxruntime.dll、NVVideoEffects.dll、
nvVFXVideoFrameGeneration.dll，原 SHA/签名/字节不变。小型 nvngxruntime 保留，未为 84KB 扩大裁剪。
207MB 的 VFG 插件是必要运行组件，不拆 PE、不量化模型。全部 2X–8X 和三种质量保留。

AMD 的 `noise.f32`（201,326,592 bytes）是模型输入噪声表；`normalized-output.f32`
（33,554,432 bytes）是时域采样 reciprocal 表。上游生产路径实际读取，删掉会破坏运行。
仅发现两对 7KB shader 完全重复；它们的别名是现有路径合同，保留，不为几KB改上游加载。
默认 skip blocks 也不能据此删权重：其他设置/回退仍可能使用，本机无 RX9000 无法证明安全。
不引入运行时自解压、下载依赖、裁剪显卡架构或降低精度。

无损压缩样本 450,593,392 bytes（AMD两大表/一权重/VFG插件）：ZIP9 277,853,065 bytes，
7z LZMA2/32MiB 166,108,485 bytes，比同样本 ZIP 小40.2%。样本不能冒充完整包大小。
最终提供相同载荷的完整 7z 和 ZIP；正式数值/SHA/独立解压结果在交付目录 `DELIVERY.json` / `DELIVERY.md`。
7z 只减传输大小，AMD 解压后的资源量没有减少；每个文件还原后必须匹配原 SHA。

## 当前真实检查

实际机器 RTX5070 / 驱动616.56；不是反馈者616.92。全部 child TEMP/TMP/CUDA cache 在E盘，
VFG/native/export/UI child PATH 限 Windows，无 SDK 路径、无 NPP 文件。

- fresh build 481步：QML、native VFG、NVENC probe、settings，exit0。已有 C4244 警告未掩盖。
- native 720p 和3840×2160，各463 checks / 0 failures：RGBA8/RGB10A2、三质量、2X–8X、
  七张输出不同、切镜 identity、参数拒绝、设备存活、debug errors检查通过。
  两进程观测只从精简运行目录加载五个 VFG组件，没有 NPP。
- settings 331 checks / 0 failures，保存/恢复与原枚举合同保持。
- 720p30 的21种真实 D3D12 NVENC HEVC导出全部通过，16–64帧、60–240fps、时长/音轨准确。
- 2K30 AVI + 原生4K NR + 最高SR4K + VFG8、24→192 / 60→480fps、取消、缺失全部runtime通过。
- 单独缺 cudart 的真实导出：记录 LoadLibrary失败、exit1、无最终文件，这是正确拒绝，不是成功导出。
- GUI热切换/list↔node/DLSS6↔VFG8/seek/resize/暂停恢复、预设与重启、缺库基础播放恢复通过。
  独立 High8 worker在父进程改 Low2 后仍输出64帧/240fps；日志证实冻结质量=2。
- AMD原529记录逐文件哈希、真实C ABI144 bytes/ABI1、62模块布局验证通过。
  caps.hip=0，本机没有RX9000；没有执行 HIP 推理，不能称AMD画质/性能/稳定性验收。

极简新EXE的6例分数DPR回归通过，右/底native gap0。最终2076载荷+manifest独立解压SHA通过，
官方7-Zip26.03 CRC/兼容性通过；Windows-only PATH下包内VFG真实run/restore/missing与High8 worker通过，
GPU/软件各六页背景alpha255和正常退出通过，运行前后所有载荷哈希一致。
本文件包内版本为组包前快照，最终报告/交付索引在包同目录，避免为自身哈希反复改包。
当前应用 SHA256 `88597f61c8d01105533927859726815aaf3c67034e71bddf17852fb13c341bfc`。

## 最终完整包

| 产物 | bytes | MiB | SHA256 |
| --- | ---: | ---: | --- |
| 首选7z / LZMA2 256MiB字典 | 523327604 | 499.08 | dcb38d16f59d9578b66d1f759092c2d5dd5c95606c3411c863cd5eb5fea5d94f |
| 备用ZIP / deflate9 | 943265095 | 899.57 | 40b44bfea5d358fa94cc9cb6bbd303e11c18cc861471847c2bed9086bd3c4b98 |

原完整VFG ZIP1151602649 bytes / 1098.25MiB → 首选499.08MiB，传输体积减少54.56%。
这是裁未用依赖再换无损压缩格式，不能说AMD模型本身缩小了54.56%。
32MiB字典7z728771789 bytes；256MiB字典进一步省205444185 bytes（28.19%）。
两份NR DLL对齐1MiB chunk中146966128/165840496 bytes相同（88.62%）；大字典跨文件复用，
不合并、patch或省略任何运行DLL。解压额外约256MiB字典内存，运行显存/内存不因此变化，ZIP备用保持。

产品代码存档 `2df6dd729b9fe7d32ed41753ab3422e5cc7eda0c` / `checkpoint/runtime-size-code-20261004`。
最终报告/验收脚本源码commit、项目源码ZIP和verify过的完整bundle见DELIVERY.json；产品代码保持相同。
桌面status/working与index patch/11未跟踪源码、main66cd3e5、原2077载荷哈希全部复核一致。
清理本轮中间压缩包与解压/测试APP副本的单一PowerShell命令被自动审批拒绝，仅给blocked by policy无细节。
命令未执行，未重试或绕过，副本保留在E盘task目录，不影响交付/核验；不能报告它们已删除。

## 未验与复现位置

未验：RX9000实际推理/颜色/性能/长稳，Xbox真机有声/长稳，RTX40实卡、616.92，
HDR色度、实显示器DPI移动、物理显示节奏/端到端延迟、VFG影片画质AB与长稳。
软件短测通过不外推这些项目。原高倍率High预览预算/跳帧行为保留，离线导出不丢源帧。

运行 `python -B scripts/acceptance/runtime-size-*.py`；命令/结果在 WORKLOG。
所有 build/tests/logs/tmp/test-packages/verify/archives 位于 `E:/项目/Veyra/<用途>/runtime-size-20261004`。
模型/DLL/SDK不进源码Git；固定来源/许可证原样保留，完整第三方源码公开发布审计不在本地交付范围。
用户要求完成存档后关机，只有所有交付与必要检查完成后才执行。
