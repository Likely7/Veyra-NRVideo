# GitHub实现与Veyra卡顿归因 — 2026-09-09

用户已反馈刚完成的版本实测没问题。本次按新请求只读核对GitHub源码、官方接口和已有本机性能日志；没有安装/运行竞争产品，没有改动增强管线，也没有占用采集卡进行新的受控对照。因此可以确认结构和已测预算，不能给出“软件占70%、硬件占30%”之类无依据比例。

## 结论

两类原因同时存在。5070没有被证实故障；原生4K NR与高质量SR/高倍FG的总负载确实可能超过16.7ms/源帧；另一方面，Veyra还有调度、同步、重置和诊断开销需要优化。更换显卡不能替代软件修正，优化软件也不能承诺把约23ms的原生4K NR变成6ms。

当前只读nvidia-smi：RTX5070，driver616.56，GPU5%，显存966/12227MiB，46°C，39.32W，250W功率限额，图形2625MHz。这是查询瞬间的轻负载状态，不是卡顿期间的数据；不能用它排除满载时降频或其他软件竞争。用户旧截图99%也不能定位具体阶段或证明硬件损坏。

## 固定审计版本

GitHub API返回的默认分支head，源码/文档只下载到ignored logs/github-audit-20260909；没有执行仓库脚本、下载二进制或复用实现。

| 仓库 | head |
| --- | --- |
| SAOG0721/Magpie | `ac1cc8b0f2efc78323898395cc1336bcbecdc276` |
| rigaya/NVEnc | `bf825096600e226b0923b7feed54ca5f83a38522` |
| DrC0ns0le/RTXVideoProcessor | `7ee725726025aa4f04dff178fb9346186d17c95b` |
| 2600th/dlss5-video-player | `335ddc4523e3614e6dfc507c7d271b8fe0c71ebb` |
| DaniilSokolyuk/video2dlssnr | `55a4ceb588a419b9b56497aa0b563d0c9e2b6c77` |
| jlrouzies-fr/DLSS5-Feeder | `d69d9174ef055f95a657750db814c93ac1ad6c1d` |
| Zonnery/dlss5-nr-player | `e9c37bec513991bf25a204e3e6dbfe4b8a7dffb2` |

## 同类如何处理

1. **Magpie Experimental**：SR/NR/DLSSFG分开的原生后端，估算guidance，NR可以降低内部输入分辨率。DLSSFrameGenerator在同一对源帧上用multiFrameCount/index循环生成子帧，保留有效性标志；有D3D11/D3D12共享fence和CPU等待，不是“完全没等待”。Front Edge Sync控制基础输入节奏，输出保持有界队列，卡顿后重建节奏而不积攒欠帧。其最新帧同步指南明确该次只有构建/CPU检查，不能拿它当GPU实测加速结论。它自己的切换卡顿报告记录过NR约15ms但真实速率31→7fps，因此也区分推理成本和捕获/呈现停顿。参考[帧同步](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/docs/FRAME_SYNC_GUIDE.md)、[FG源码](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/src/Magpie.Core/DLSSFrameGenerator.cpp#L676)、[其问题分析](https://github.com/SAOG0721/Magpie/blob/ac1cc8b0f2efc78323898395cc1336bcbecdc276/docs/experimental/reviews/20260905-v0.6.5-r9-dlssnr-switching-stutter-REVIEW.md)。不据他人的不同GPU/不同尺寸日志给Veyra下性能结论。

2. **NVEnc**：成熟视频处理工具，支持RTX Video VSR质量1–4及FRUC double/目标fps。FRUC采用CUDA设备指针，按目标输出PTS维护多个handle，每个handle跟踪自己的上次输入；不是重复输入同一实例直接变4X。这支持我们多实例方案的合理性，但它比我们少一层D3D11互通，值得做CUDA直连的本机对照。它是编码管线，不能直接当作实时采集延迟基准；其wrapper还忽略了FRUC重复标志，我们不会照搬这一点。参考[FRUC调度](https://github.com/rigaya/NVEnc/blob/bf825096600e226b0923b7feed54ca5f83a38522/NVEncCore/NVEncFilterNVOFFRUC.cpp#L337)、[CUDA接口](https://github.com/rigaya/NVEnc/blob/bf825096600e226b0923b7feed54ca5f83a38522/NVEncNVOFFRUC/NVEncNVOFFRUC.cpp#L185)。

3. **RTXVideoProcessor**：CUDA解码帧池→GPU颜色转换→RTX VSR→GPU编码帧，异步demux。但源码仍有GPU内部copy与cudaStreamSynchronize，README的zero-copy主要指不回CPU，并非零复制/零等待；也有CPU fallback。它只做VSR/HDR，不包含Feature18和DLSSFG，因此其“4070倍速”不能证明我们的5070同时NR+FG应有同等吞吐。参考[GPU处理](https://github.com/DrC0ns0le/RTXVideoProcessor/blob/7ee725726025aa4f04dff178fb9346186d17c95b/src/rtx_processor.cpp#L485)、[帧池/路径](https://github.com/DrC0ns0le/RTXVideoProcessor/blob/7ee725726025aa4f04dff178fb9346186d17c95b/src/main.cpp#L114)。

4. **2600th/dlss5-video-player**：先在隐藏worker逐帧NR、读回并编码成神经缓存，再播放原片/缓存，播放时可另开DLSS SR。缓存保留源尺寸、SR独立。这让文件观看不必现场承担NR全开销，但不适用于无法预知未来的实时采集。参考[setup](https://github.com/2600th/dlss5-video-player/blob/335ddc4523e3614e6dfc507c7d271b8fe0c71ebb/docs/DLSS5_SETUP.md)、[离线循环](https://github.com/2600th/dlss5-video-player/blob/335ddc4523e3614e6dfc507c7d271b8fe0c71ebb/src/OfflineNeuralRenderer.cpp#L400)。

5. **video2dlssnr**：SR→光流→NR→合成顺序与我们主链一致。README写“无CPU往返”，实际视频路径nr.cpp有stdin fread、GPU EndAndWait和ReadbackRgba8、stdout fwrite；有线程队列提高重叠，并非完整显存直通。不能按宣传推断其优于我们。参考[视频循环源码](https://github.com/DaniilSokolyuk/video2dlssnr/blob/55a4ceb588a419b9b56497aa0b563d0c9e2b6c77/src/nr.cpp#L1385)。当前无许可证，不复用代码。

6. **DLSS5-Feeder**：游戏ReShade深度和估算motion→合成DLAA contract→神经消费者；与纯HDMI输入不具有相同数据前提。当前README还明确“优化”标签不保证每台机器/游戏变快。这是注入架构参考，不是5070实时视频性能对照。参考[README](https://github.com/jlrouzies-fr/DLSS5-Feeder/blob/d69d9174ef055f95a657750db814c93ac1ad6c1d/README.md)。

官方[FRUC指南](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html)确认NVOFA+CUDA、两帧间插值和D3D11/CUDA资源，不支持“全部只用光流专用硬件所以没GPU开销”的说法。[RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk)提供视频超分/HDR；FRUC来自Optical Flow SDK，两者不是同一算法。

## 我们已定位的成本

此前同一USB3Video1080p60受控数据（不是本轮新测）：

| 配置 | SR阶段中位ms | NR阶段中位ms | 实际源处理fps |
| --- | ---: | ---: | ---: |
| 视频SR最高 + 原生4K NR | 6.617 | 22.911 | 约30 |
| 视频SR最高 + 实时NR | 6.497 | 6.100 | 51.16 |
| 视频SR低 + 实时NR | 1.505 | 6.366 | 59.51 |

见PHYSICAL_CAPTURE_DIAGNOSIS_2026-09-09.md。仅第一行SR+NR已约29.5ms，还没算flow、FG、其他pass与呈现；在现有同队列依赖路径上不能满足16.7ms。第二行余量很小，第三行能近60说明整条软件并非固定锁30或5070无法处理4K输出。历史“打开FG，20ms变6ms”已查到NR4K→1080设置串改，修复为单字段实时提交；那次不是FG让NR加速。

FRUC新短测1080p已知平移阶段区间中位2/3/4X=9.22/18.36/27.13ms，包含GPU队列等待及IPC提交，不是纯核耗时。3/4倍成本明显增加是真实结果；本轮没有与DLSS同源同配置完整A/B，不用旧DLSS2.5ms与此直接算倍数。

## 软件内部确实存在、但需量化的点

- `EngineController.cpp:180–205`：同一线程等待整批FG有效性、依PTS逐帧呈现，结束后才读下个源帧。GPU有slot ring不等于decode/处理/呈现已充分并行；捕获回调虽独立，容量1mailbox在处理/呈现落后时仍会覆盖源帧。正确优化是有界的生产/消费解耦和保留lease/fence，不能删除等待后让纹理被覆盖。
- `EnhanceGraph.cpp:1072`：FRUC在所有历史中断上重启worker，可能“掉帧→重启→更掉帧”。用户目前说实测没问题，因此这仍是压力场景风险，不能谎称已经复现其当前体验。应分别统计reset原因/次数/时间；轻量reseed必须先与fresh worker做后续像素一致性验证。
- `Log.cpp:write` 每条记录持锁fprintf+fflush；源、提交、GPU timestamp等每帧多条。确实存在同步文件写开销，但没有A/B测出其占比。保留错误/身份/事务证据，把常规逐帧事件缓冲或按需采样，比删日志更合理。
- `EnhanceGraph.cpp:956`：每源帧始终保留原图和SR底图两份GPU拷贝，保障即时同帧对比。可能优化为按需/引用复用，但需要保持对比语义及lease生命周期，不能把必要副本武断叫成bug。
- `EngineController.cpp` 的TimingWindow/late samples跨配置混合，NR表盘仅NR单阶段。这个显示问题已经有证据，必须先修，才能让用户可靠比较配置。
- 普通文件当前软件解码；可借鉴CUDA/D3D12VA硬解+GPU帧池，但原始YUY2采集并没有H264/HEVC需要NVDEC解码，硬解不会让原生4K NR的23ms消失。

## 优先顺序与可判定实验

1. 按settingsRevision隔离统计：显示输入/有效源处理/有效生成/提交fps；分开SR/NR/FG阶段、CPU解码、GPU ready、deadline、Present、reset。先消除“6ms是总延迟”的误读。
2. 同一1080p60与4K18源固定NR尺寸，做原图、VSR低/高、NR、二者组合、分别DLSS2/4和FRUC2/4。每次60–120秒，独立日志窗口，记录nvidia-smi功耗/频率/温度/负载并与时间轴对齐。不得用实时NR冒充原生4K或跨素材比耗时。
3. 相同设置测日志缓冲与原路径、解耦present前后：若阶段GPU时长稳定而deadline/CPU等待/丢帧下降，即证明软件调度收益；若GPU关键路径长期超过预算，则仍需降档/内部尺寸或更强硬件。
4. FRUC另做CUDA直接资源互通的小型对照，只在证明收益和reset/颜色正确后替换D3D11worker路径。NVEnc存在此结构是可行性参考，不是必然加速证明。
5. 文件模式硬解和可选缓存放后；不改变用户实时采集的产品定位，不偷换处理顺序或用离线缓存冒充实时。

本次结论：先修可观测性和软件调度，再用同源A/B确定收益；目前没有足够依据让用户换显卡。原生4K NR60fps仍不在本机默认实时档承诺内。测试验收不等于公开分发许可，未推送/发布任何源码或二进制。
