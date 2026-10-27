# 2026-10-25 NR视频导出尺寸：社区补丁接入与百分比兼容

已从038beac在`codex/export-nr-scale-20261025`接入并验证，最终输出尺寸保持导出设置，NR内部按逐层固定档或25%–100%处理。例如4K选1080p/50%时NR按1920x1080运行、输出仍4K；8K选25%时NR同样1920x1080、输出仍8K。不能把内部NR档位当作视频编码尺寸。旧代码是三处历史策略主动强制Native，不是预设字段没有保存。

## 社区贡献与本地适配

附件原样保存在`E:/项目/Veyra/archives/export-nr-scale-20261025`，原patch SHA256 `E4D92E28A35DFE64AE309D6B238B1061CBCDF85A1D25EA04A797C9F9F2480598`，说明SHA256 `35FB5FC6F5BA9ECF95CC58D5AEA08A700B9515D04E1A891956E503A8296DD04A`。没有提供作者姓名，不虚构署名；来源已记THIRD_PARTY_NOTICES。

`git apply --check`确认GraphDescription/EffectChain与已有手动比例接点冲突，其余文件可接入。保留社区对固定档、Auto、导出日志、四语说明与旧测试预期的修正，手动适配冲突。直接套补丁会把AMD手动策略（nrHeightLimit返回0）重置为1080p，还会把非法策略变成合法AMD档，均已修正。

- `ExportFeaturePolicy.h`保留固定档/手动125..200编码，Auto在任务快照固定最高1440p，AMD固定档仍受1080p限制。手动在AMD预算内输入上乘百分比；非法策略保持非法。`freezeExportSettings`不改原预设，也不增加POD字段或改变导出IPC协议。
- 新共享`exportNrProcessingExtent`同时用于GraphDescription与逐层编译，避免描述缩小但实际仍创建Native。保留离线base/flow/output语义；NVIDIA光流仍源尺寸，AMD维持原1080p指导预算，各层NR尺寸独立。图片全尺寸，列表导出保留既有SR先行规则；节点视频导出仍按原UI限制拒绝，没有冒称节点导出已接入。
- `VideoExportJob.cpp`新增实际逐层input/internal/output日志；`ExportJobManager.cpp`更新冻结说明。QML及四语说明同步手动比例也适用于视频导出。`ResolutionPlan.h`和NR/光流/着色器核心实现未修改，FG1后台VRR查询与全部补帧调度未改。

## 真GPU、真实子进程导出

本机RTX5070、现有SF runtime（Ampere槽、性能模型），SDR合成短片，每例6帧。`NrExportSizeTests.cpp`只调用产品ExportJobManager；子进程调用原产品runExportWorker/exportVideo、D3D12 NVENC、mux，测试没有NR或编码替代实现。逐例核对NR模型Create尺寸、逐层执行尺寸、成功CreateFeature18 `result=0x1`、源/编码帧数、ffprobe完整解码6帧及中间帧非黑。下列9例全部通过：

| 输入/输出 | 所选NR | 实际每层NR尺寸 | 编码 |
| --- | --- | --- | --- |
| 3840x2160 | 固定1080p | 1920x1080 | H.264 |
| 3840x2160 | 手动50% | 1920x1080 | HEVC |
| 3840x2160 | 手动37% | 1420x798 | HEVC |
| 3840x2160 | 手动100% | 3840x2160 | HEVC |
| 3840x2160 | 两层720p/50% | 1280x720、1920x1080 | HEVC |
| 3840x2160 | Auto | 2560x1440 | HEVC |
| 7680x4320 | 固定1080p | 1920x1080 | HEVC |
| 7680x4320 | 手动25% | 1920x1080 | HEVC |
| 3840x2160 | 手动37%＋光流 | 1420x798，指导源3840x2160 | HEVC |

两份8K输出逐字节相同，SHA256 `ff4de85bf384342daeebb602fefe91b479978fbaf1e9970ec1f02458b7bebe40`。它们使用零运动输入；另一个4K光流导出实际启用了NVOF，成功执行5次，非黑输出通过，不能把零运动样本冒充8K光流通过。

还通过真实QML/桥接/预设路径：保存37%列表预设，选择为导出预设，再将当前预览改100%并关闭NR；启动导出后又将当前滑条改25%。worker仍创建1420x798 NR并输出4K/6帧，导出没有篡改当前预览25%/关闭NR。实测应用EXE SHA256 `251ed807f2a45fcd5b2274d61797e4f6e71c9b62cffe8aece5cc57f0915d9905`。

`NrVideoQualityProbe`真实GPU两例：`stack1-percent-still`的50%策略仍按1920x1080全尺寸创建/执行NR2次；`stack2-percent-export`按两层独立百分比创建纹理、执行NR4次，逐层同帧对照相等。两例D3D12 debug errors=0。前者证明图片模式完整处理的GPU合同，没有据此声称实际PNG编码或HDR全链验收。

## CPU/界面与失败记录

六项合同最终全部通过：EffectChain（含1008尺寸/上下文组合与8K/超宽/小输入/SR/禁用层）、NrModelSettings（全部76个整数百分比的NVIDIA/AMD 8K导出尺寸及冻结/存档兼容、非法值拒绝）、UI i18n、Repair272、UI384布局、QtQuick110通过/0失败。未改变旧0..6档ID或125..200手动编码。

首轮两个测试失败，均保留证据：社区补丁新增长结构体局部夹具使EffectChain测试的默认1MiB栈溢出（0xC00000FD），仅将此测试目标栈扩到32MiB，与已有大型合同测试一致；新测试误读单层冻结后的已清空数组，改用既有nrLayer(0)访问规范化后的单层字段。产品逻辑未通过放宽尺寸/校验来迁就测试。修正后两测试通过；原contracts-initial.json和失败日志保留。

命令：`python -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-export-nr-scale-20261025 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.0-nre1 ...`，首轮65.5秒；定向测试目标构建19.8秒，GPU探针21.5秒，最终QML/界面目标5.9秒，最后单独增加光流测试目标3.2秒，均-j2、单次远低于900秒。

验收脚本`nr-export-size.py`（首次8例，再`--case 4k-flow`）、`nr-export-preset.py`与`NrVideoQualityProbe`两例。单测试均<=300秒、全部顺序执行GPU，不停止其它进程、不连真实采集/串流/配对主机、不改真实用户配置或运行库。旧Win32脚本只更新尺寸合同，未在本轮执行。

证据根`E:/项目/Veyra/reports/export-nr-scale-20261025`：contracts.json、gpu-exports-all.json、gpu-exports-4k-flow.json、ui-preset-export.json、image-flow-probes.json。原日志`logs/export-nr-scale-20261025`，worker日志在唯一私有app的`tests/export-nr-scale-20261025/app/logs`。QtQuick结果110项；日志使用UTC，验收发生于用户America/Los_Angeles的10月25日。

## 范围与交付

AMD预算/百分比仅CPU合同通过，未做AMD实卡；没有用户原8K黑场素材，合成6帧短片不能证明其黑场闪烁已根治、长期画质或稳定性通过。HDR、其它显卡/NR运行版本、音轨、8K NVOF未在本轮验收。降低NR内部尺寸可能改变效果，不等同原生尺寸画质。

本地更新`Veyra-2.1.0-nre1-NVIDIA-update.zip`需要完整2.1.0 NVIDIA底包：关闭播放器，将文件解压覆盖原包根目录。含手动比例、NR视频导出修复与此前fg1/旋转/录制改动；无DLL/模型/SDK/配置/测试媒体。源提交、ZIP SHA与逐文件核验见update-package.json。未合main/push/Release，Lecram暂停。共用一份私有测试app、没有复制整工程，收尾保留当前构建/交付与必要证据。

本任务tmp子目录约105MiB（含CUDA缓存与私有进程状态，非运行库实体副本）。曾请求原生PowerShell仅删除本任务下名为cuda的生成缓存，带绝对路径边界/重解析点/活动进程守卫，自动审批在执行前以blocked by policy拒绝，未提供详细原因；未执行、未用其他方法删除或移动绕过，缓存保留，见cache-cleanup.json。验收脚本后续共用任务级CUDA缓存，避免逐例重复缓存。
