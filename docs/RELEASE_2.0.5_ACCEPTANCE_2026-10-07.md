# 2.0.5 本地整合验收记录

已完成全新2.0.5生产构建与下列定向检查。源自71483d5，已完成产品修复ef2069f/4c917aa；main原578d63c，按用户授权将本候选合入。本轮相对71483d5只有版本、文档、验收和打包脚本变动；原已测引擎/QML算法保持，NV显存根因未定位且明确延期。

命令统一使用 `C:/Users/123/AppData/Local/Programs/Python/Python311/python.exe -B`。新产物根 `E:/项目/Veyra`，任务 `release-2.0.5-20261007`，源码worktrees同名。实际每项构建/测试/包收据保存到该任务logs，完整交付身份到test-packages/DELIVERY.json；不把旧版通过记录当本次执行。

27个旧工作树、495个未提交/未跟踪文件和14份发布文件已冻结；guard初检通过，main仅按本轮授权推进。公开2.0.4及其依赖源码字节保持，不创建新Release、不推送、不改用户配置/驱动。运行库沿用已批准原字节。

## 实际构建和检查

运行环境RTX5070/616.56、Windows11、Qt6.8.3。所有GPU用例串行；不派Agent、不制造压力或竞争负载。构建脚本超时890秒，每项测试进程≤300秒；私有profile与TEMP/TMP全部定向E盘，不修改用户配置。

| 实际命令（省略公共Python前缀） | 内容与结果 |
|---|---|
| release-2.0.5-build.py build-production-tests | 13个生产/定向目标，全新513步exit0；外置依赖与patched FFmpeg/libass保持 |
| release-2.0.5-tests.py units units | 效果链、258修复契约、预设、i18n、56能力、呈现时序、HDR调优7目标全部exit0 |
| release-2.0.5-quick.py telemetry | 真实VTelemetryValue，快速目标更新/收敛/隐藏/减少动效/零时长，6/6通过 |
| release-2.0.5-tests.py graph graph | AMD共享GPU合成10例，含4K输出/内部1080p，最大码值误差0；诊断恒等提供器，不是HIP推理 |
| release-2.0.5-tests.py display display-sync | 窗口/无边框全屏×低队列×Auto/Vsync/Tearing共12组，实际sync/flags/像素正确，D3D12错误0 |
| release-2.0.5-tests.py fg-pixels presentation | 实际DLSS4X/6X/4X、resize/reset与双缓冲复用，像素检查通过，D3D12错误0 |
| release-2.0.5-tests.py fsr-h264 fsr-export | FSR2X→D3D12 NVENC H264：60源+59生成+1保持=120编码，1280×720 |
| release-2.0.5-tests.py fsr-nr-hevc fsr-sr-nr-hevc | FSR SR→原版native NR→FSR2X→HEVC：120编码，2560×1440 |
| release-2.0.5-tests.py nr-stack-hevc field-stack-hevc | 双层原版native NR→HEVC，60源/60编码，1280×720 |
| release-2.0.5-tests.py xess-rejection reject-xess | 真实工作进程明确拒绝XeSS补帧导出，0编码、无最终输出，不替换为DLSS |
| release-2.0.5-ui.py gui-amd-default-seed NVIDIA seed-dlss | 生成本轮独立旧DLSS配置fixture，保留list/node分别设置 |
| release-2.0.5-ui.py gui-amd-default-migrate AMD migration | 不可用DLSS配置恢复为FSR2X，启用状态及禁用node状态保留 |
| release-2.0.5-ui.py gui-amd-xess-keep AMD xess-keep | 重启保留可用手动XeSS选择 |
| release-2.0.5-ui.py gui-nvidia-dlss-keep NVIDIA nvidia-keep | 重启保留NVIDIA DLSS6X |
| release-2.0.5-ui.py gui-amd-catalog-v4 AMD catalog | 缺NVIDIA组件时，实际Node添加补帧及list FSR/Flow预设通过；这是RTX上的缺组件验证，不冒充AMD实卡 |
| release-2.0.5-ui.py gui-telemetry NVIDIA telemetry | 原版1080p NR真实播放、暂停/恢复、全屏/返回、seek、Node接线执行、恢复List与关闭动效、停止/完整关闭，30.094秒exit0 |
| release-2.0.5-ui.py gui-fsr-export-second NVIDIA export | 实际GUI FSR2X窗口/全屏/返回与H264导出：1280×720/60fps/120帧，19.016秒exit0 |
| release-2.0.5-ui.py gui-fsr-restore NVIDIA restore | 再次启动保留FSR倍率/启用/Flow预设，4.922秒exit0 |
| release-2.0.5-export-timestamps.py | 上述四个实际视频全部逐帧解码通过，真实PTS严格CFR；FSR为120帧/60fps，双NR为60帧/30fps，均2秒 |
| release-2.0.5-perf.py 205 gta-205 normal 40 --gta | 用户GTA VI 4K30、原版单NR内部1080、普通class2，生产D3D12 UI/Auto同步，无诊断强制项；49.875秒exit0 |

最后一项32个一秒平均值的NR中位数 **6.294552ms**，UI提交中位36次/秒，视频提交30fps；媒体SHA93db6129…，原版NR SHAe16bcf15…，每次D3DKMT查询均成功/class2。这是新构建单轮功能/性能检查，不是新旧匹配A/B，也不是P95、屏幕延迟或全配置保证。约7%差距的原因与fix2匹配/反向验证仍以STABILITY_EXPORT_PRIORITY_REPORT为历史证据，不把不同轮次拼成新的收益百分比。

本轮EXE SHA256 **acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936**，PE File/ProductVersion均2.0.5.0；界面显示版本CMake明确2.0.5。NVIDIA/AMD同EXE/QML/shaders，保留215/694项原组件字节。原版/Lecram/SF-v2、TrueHDR/VFG、AMD模型/HIP文件不替换；不把SDK或运行库加入源码Git。

## 失败与对抗复核

gui-amd-catalog首轮沿用旧fixture，在List固定单例补帧已存在时再次添加，触发最多一个；v2试图删除固定尾节点也被正确拒绝。检查实际NodePage菜单与add/remove实现后，改在fresh Node上下文测试添加，未放宽产品规则。v3实际添加/预设通过且进程exit0，但Qt.quit异步退出前测试Timer再次执行，误把上一轮添加的节点当fresh draft而报FAIL；保留该失败收据。最终夹具在成功/失败时显式停Timer，v4全部通过。**这些是测试前置状态/生命周期错误，未改产品C++或QML来迎合测试。** 原失败app/console/engine日志及JSON均保留。

修复/旧配置测试真实使用缺NVIDIA组件的AMD包与RTX硬件，不伪造AMD厂商ID；AMD4K图测试仅确认共享合成契约。实际AMD HIP高分辨率导出、AMD硬编、FSR4 ML实卡、真实主机/采集长稳、用户显示器撕裂/VRR与NV显存根因仍未完成。本次短测不能代替这些现场验收。已有PR19/20与fix1保留，不复活PR13/14或先前撤回实验。

## 合并、源码和最终交付核对

此记录随候选源码冻结，不在源码内循环填写自己的提交/ZIP SHA。`logs/release-2.0.5-20261007/tested-inputs.json` 冻结全部跟踪输入、当前EXE和20份当前通过收据；旧失败收据保留但不计通过。`main-merge.json` 记录本地main从578d63c --no-ff合入实测提交及树完全一致；只有该收据和独立检查通过才说明合并完成。原分支/桌面和其余工作树保持，旧guard/start不改。

执行 `release-2.0.5-package.py refresh` 后 `finalize`，生成AMD/NVIDIA便携ZIP与应用源码ZIP、增量Git bundle、SHA256SUMS和DELIVERY.json；逐文件manifest、全部ZIP CRC及载荷SHA必须通过。对应依赖源码沿用已发布2.0.4字节，SHA4eccde63…，不新增下载/运行组件。`release-2.0.5-cold.py cold-final-zip --zip` 必须从最终ZIP独立解压、仅系统PATH/私有profile启动，核对实际包内Qt/FFmpeg模块来源；`release-2.0.5-evidence.py close` 再核main树、EXE、源码/ZIP、测试收据、27旧工作树/495修改文件/14发布文件。

完整交付以 `test-packages/release-2.0.5-20261007/DELIVERY.json` 与同任务 `logs/final-check.json` **实际passed=true** 为准，不以本流程文字代替最终执行。此次只本地测试，不替换公开2.0.4资产、不推送、不发布、不代发消息、不关机。NV显存问题明确延期至下一版本。
