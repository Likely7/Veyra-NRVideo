# VFG 专项优化实测（2026-10-07）

状态：本机VFG专项短测通过，已达到部分主要档位目标；更高倍率仍有明确限制。

本轮只修改VFG。DLSS/XeSS/FSR实现、默认选择、准入策略、NR算法、shader、运行库及用户配置保持。核心源码ddf71b1，隔离分支codex/vfg-optimization-20261007；main、已发布2.0.5及其它工作树不变。最终候选显示版本2.0.5-vfg-test，不是新正式版本。

## 实现与原因

源30fps给每对输入约33.33ms计算预算，4X送显却需要约8.33ms一次。原版虽已有独立GPU呈现队列，CPU同一线程仍整组调用VFG后才推进呈现。分段实测NvVFX_Run平均约6.2ms/次，Medium4需三次，参数设置只需微秒。原19ms左右的增强总耗时低于源帧预算，并不能阻止已就绪插帧被22ms左右的CPU提交挡住。

先按固定官方样例减少重复输入绑定与倍率设置，Medium4仍83fps，未证明提速。随后仅实时VFG启用有界工作线程，CUDA推理仍在一个stream串行；图owner只排producer/consumer fence及输出GPU copy，独立呈现队列与原PTS、过期阈值、音频一倍速、批次租约和command-slot上限均保留。最多14个接受但未完成的SDK子任务，覆盖两组8X输出，队列满时在加入GPU等待前明确拒绝。

每个输出槽独立记录Pending/Succeeded/Failed，GPU复制完成后还要核对SDK成功；失败槽不允许变成Valid。已接受任务在拒绝、退出或重建时必须释放其等待，异常路径先排空CUDA写入；停止worker并join后才能销毁SDK图像、导入映射、semaphore、stream及DLL。正常帧不加CPU fence wait或像素回读。导出保留同步SDK判定路径。

官方API/来源：[VFG 1.3.0](https://docs.nvidia.com/maxine/vfx/1.3.0/Filters/VideoFrameGeneration.html)、[固定MIT样例52011f89](https://github.com/NVIDIA-Maxine/VFX-SDK-Samples/blob/52011f89c1741d06b40ea312af1f20be8be9ec62/apps/VideoFrameGenerationEffectApp/VideoFrameGenerationEffectApp.cpp)。既有THIRD_PARTY_NOTICES已登记同一来源；未新增SDK/模型/DLL进Git。

## 条件和口径

本机RTX5070 12GB、驱动616.56。原GTA6视频3840×2160/30fps，单层原版NR内部1080p、强度1、风格0、零运动，调控/抗闪/SR/HDR/调色关，GPU普通实际class2、呈现queue Priority0，窗口1280×800、Auto同步、输出限帧关。**VFG仍处理4K，1080p仅是NR内部尺寸。** 每质量/倍率独立进程、正常播放、约5秒预热+20秒观察，串行且单次低于300秒；未启动竞争GPU压力或修改系统驱动/显示/用户配置。

原视频14115273308字节与mtimeNs1789034773254693400核对既有身份，复用之前完整SHA256 93db61292353f19aaf6bfa2fbd3a094d1140300785ab2d93b0c06aa7881f8b3f，本轮不声称重算14GB散列。原版NR SHA256 E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E；随包VFG五文件与官方2.0.5 manifest完全一致，SDK1.3.0/nvidia-vfx0.2.0.0。

FPS是1Hz观察窗口中应用实际呈现提交读数的中位数，非物理屏幕FPS或屏幕端到端延迟。CPU P95为各日志滚动P95的中位数，不能称整段合并P95；GPU阶段含跨队列交接及实际对应工作，非纯模型内核时间。脚本passed表示采样、后端、退出契约，不等于每个倍率跑满。系统日常桌面应用未擅自关闭，单轮GPU占用/时钟会变化，因此另做相邻原版/候选和同步提交对照，不用非相邻GPU耗时计算算法提速。

## 全档位与相邻对照

| 倍率 | 源30目标fps | Low 前→后 | Medium 前→后 | High 前→后 |
| --- | ---: | ---: | ---: | ---: |
| 2X | 60 | 60→60 | 60→60 | 37→60 |
| 3X | 90 | 90→90 | 82→90 | 30→60（降档） |
| 4X | 120 | 112→120 | 84→120 | 30→58（降档） |
| 5X | 150 | 123→150 | 74→150 | 33→52.5（降档） |
| 6X | 180 | 130.5→180 | 57→174（降档） | 33→38（降档） |
| 7X | 210 | 131.5→210 | 34.5→178（降档） | 32.5→36（降档） |
| 8X | 240 | 128.5→240 | 30→156（降档） | 32→40（降档） |

“前”为上一轮正式2.0.5诊断参考，“后”为本轮最终候选21组。非相邻矩阵不用于精确归因百分比；下面相邻对照才用于计算收益。

| 对照 | 原版fps | 候选fps | 提升 | 原版CPU滚动P95中位ms | 候选CPU滚动P95中位ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| medium4 | 80 | 120 | 50.0% | 23.450 | 1.198 |
| high2 | 35.5 | 60 | 69.0% | 30.940 | 1.079 |

同一候选仅强制同步提交的控制组Medium4为76.5fps；对应正常异步组为120fps，支持“送显线程被SDK调用占住”的因果解释。

首轮Medium4为120fps（前83–84），High2为60fps（前37）；Medium8首轮191fps但最终矩阵可能波动且持续降档，不能宣称240fps达标。Medium6以上、High3以上需逐项按目标与降档状态判断，不能由性能球颜色推断。优化没有减少VFG模型的推理复杂度；更及时送显会增加实际呈现工作，GPU利用率/阶段时间不保证下降。

## 验收

- 最终候选构建candidate-build-v3 exit0，EXE SHA256 `751e1032cd3b2c00d1e95bf9c80bd4e5dba746b9b63fe4f16fba03a955b78653`，核心源码ddf71b1。
- 21组原片正常播放、5组相邻/同步控制均采样与退出通过；是否跑满按上表判定。
- 8/10位、三质量、2X–8X、切镜/相同CUDA地址刷新/反向运动/parity与2X→4X→8X恢复：720p同步、720p异步、4K异步各913项0失败，最终EXE再做720p检查通过；168张同步/异步插帧GPU像素哈希一致。
- GUI热切换质量/倍率、VFG↔DLSS6X、暂停/seek/缩放、全屏进入/返回、列表/节点预设与重启通过；缺VFG运行库沿用原有启动迁移到FSR2X并继续播放，未修改该共享策略。异步Run拒绝后实际FG关闭、原帧继续，无等待悬挂。GUI独立导出worker保持同步SDK判定，8源帧输出64帧/240fps。
- VFG设置/预设/会话及倍率/PTS单元测试331项0失败。
- 21组720p三质量×2X–8X实际NVENC导出及4K GTA短片原生NR+VFG4导出通过；每组源8帧、输出8×倍率、HEVC/CFR与严格递增PTS核对，无源帧丢弃。边界hold仍按原导出合同计数，不能把所有输出都称神经生成帧。取消和缺运行库导出失败清理通过。
- 失败记录保留：初次两个异步拒绝断言误把QML保存的fgEnabled请求当实际后端；首次缺运行库断言误以为旧策略必定关闭FG，实际已迁移FSR；首次八帧MP4导出断言未考虑微秒time_base取整（16帧、标称60fps，平均59.999925fps）。按实际接口和时间刻度修正测试后通过，没有为迁就测试改产品。settings-tests首次漏传必需的绝对输出目录而exit2，补齐参数后331项通过。

4K短测与像素检查不代表所有40/50系、所有驱动、真实采集/串流或全片长稳验收。10位测试验证SDK编码布局、运动与切镜，不是HDR观感验收。NVIDIA显存增长未修复；本轮不恢复已延期的泄漏排查。高倍率完整成本样本老化后的恢复策略仍沿用旧版，未以修改共享准入策略扩大到其它补帧。

## 证据与使用

命令均在E:/项目/Veyra/worktrees/vfg-optimization-20261007运行：python -B scripts/acceptance/vfg-opt-build.py candidate-build-v3；vfg-opt-run.py matrix及匹配case；vfg-opt-native.py；vfg-opt-lifecycle.py；vfg-opt-export.py；vfg-opt-report.py；vfg-opt-control.py及vfg-opt-run.py audit。

日志/原始JSON在E:/项目/Veyra/logs/vfg-optimization-20261007；构建build/vfg-optimization-20261007；独立测试app/profile、输出视频、GPU像素结果在tests/vfg-optimization-20261007；缓存/TEMP/TMP在tmp/vfg-optimization-20261007；不可变基线/Git bundle在archives/vfg-optimization-20261007；最终回执verify/vfg-optimization-20261007。没有修改旧guard/start来放行新文件，没有merge/push/Release或关机。

本地候选从正式NVIDIA包独立复制，替换本轮EXE并重建包manifest，QML/运行库/模型原字节保留，测试loader、日志、私人profile及媒体不进入候选包。对应源码随本地候选提供。建议先用原片、普通GPU优先级、单层1080NR验证Medium4/5或High2；更高档按实际输出状态判断。
