# #67 NR 退出卡住修复

用户停止 Lecram，改为修复 Likely7/Veyra-NRVideo#67。从 nrvideo/main 4ae285b 在 codex/issue67-nvapi-shutdown-20261021 隔离实施，存档 checkpoint/pre-issue67-nvapi-shutdown-20261021；Lecram、全屏及其他任务保持。

## 证据与边界

报告为 RTX3090、616.92、SF310.9.16，关闭后等待20秒被 watchdog终止。日志最后为 snippet module freed，但同份报告写明 architecture rewrite not installed。该版本策略早于 NVAPI 加载即返回，archLookupSlot_为空，因此 hadAmpere=false，不执行 ClearAmpereContext。作者认定的阻塞调用和跨进程遗留状态都没有堆栈证明，不能作为已确认根因。

旧兼容路径确实存在逐图 FreeLibrary(nvapi64.dll)；SF 的 NR/NGX 也可能持有该驱动模块。修复加载的生命周期隐患，并将原先未区分的 adapter结束、core保留检查、Shutdown1外调分开记录。仍复现时需对应阶段的线程堆栈，不能仅根据最后日志猜测。

## 实施

1. NR DLL 加载前，通过绝对 System32 路径一次性加载并 PIN 系统 NVAPI。并发首次调用只执行一次；后续返回借用句柄。不在析构中卸载，也不调用全局 NvAPI_Unload，不改驱动/文件/模型。加载或固定失败明确拒绝 NR，不伪报成功。
2. 旧 Ampere 上下文只借用该句柄，失败回滚/正常卸载清理本图指针及IAT，仍正常释放 NR features、参数、NR模块、NGX核心和GPU纹理。SF版本仍不安装架构改写。不会按图累积模块引用/显存。
3. 增加关闭阶段成对日志，保留20秒保护，不改变UI刷新、补帧/颜色/采集/调度。
4. 实际系统 NVAPI 并发/消费者释放/PIN回归，旧NR兼容安装/恢复循环，SF/原版实际视频与运行库切换、关闭后重新启动。单测≤300秒，构建≤900秒、并发2；本机5070不能代表3090验收。

Windows PIN契约：https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw （固定模块保留到进程结束）。只保留驱动模块映射，不保留NR feature/视频资源，也不保证第三方驱动内部Shutdown不阻塞。

所有构建、测试、日志、临时文件和存档：E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/issue67-nvapi-shutdown-20261021。仅本地修复/验证，不合main/push/发布/评论/关机，不派Agent。结果补充 WORKLOG。

## 实际验收

2026-10-21，本机 RTX5070 / 616.56，独立构建7目标通过（241.2秒、Ninja并发2）。命令：`py -3.11 -X utf8 -B E:/项目/Veyra/tmp/issue67-nvapi-shutdown-20261021/build.py`；其调用本树 `scripts/acceptance/claude-beauty-build.py --task issue67-nvapi-shutdown-20261021 --label 2.1.0-issue67 --template-cache E:/项目/Veyra/build/bilibili-2.1.0-20261019/CMakeCache.txt --parallel 2`，逐项传入下表目标及 veyra_qml_ui。

私有运行目录 `E:/项目/Veyra/tests/issue67-nvapi-shutdown-20261021/app`，只借用已发布包的运行组件，配置独立复制，不复制真实用户偏好。单次测试命令及回执在 `logs/issue67-nvapi-shutdown-20261021/checks-20261021-223100/receipt.json`（所有路径相对 E:/项目/Veyra）。执行脚本为同任务 tmp/checks.py；全部单测≤14秒。

| 测试目标 | 结果 |
| --- | --- |
| veyra_nvapi_lifetime_tests | 8线程首次竞态、512次借用、64次配对加载释放、额外释放后PIN仍有效，系统路径正确，退出0 |
| veyra_nr_ampere_tests | 实际旧310.8.0模块，兼容IAT安装/恢复3次、重复unload、系统函数不变，退出0；未在30系执行NR |
| veyra_nr_runtime_assertion_tests | 版本/架构策略断言通过，SF310.9.16仍走真实架构 |
| veyra_nr_model_settings_tests | 模型开关/预设/会话与AMD设置回归通过，failures=0 |
| veyra_nr_core_cache_contract_tests | 真实NGX参数容量失败/借用拒绝/重建/最终关闭通过，参数归零，deviceRemoved=0；Shutdown1成功、48.400ms |
| veyra_nr_runtime_switch_tests ×2独立进程 | GTA6 4K输入，SF性能/旧模型/原版切换均推理并保存实际输出；NR直接开启状态下停止233.588ms、201.799ms；同进程重新打开视频并再次NR停止成功，进程正常退出0 |

两个真实视频进程各只加载/固定NVAPI一次，分别5次NR卸载完成、4对NGX Shutdown1完整返回（35.943–43.705ms），各4次SF绕过架构兼容；无engine-hang。8张实际输出均3840×2160且非黑帧（只验证输出有效，不宣称画质提升）。供体及私有两种NR DLL SHA不变，未改任何运行库字节。

结果索引：`E:/项目/Veyra/reports/issue67-nvapi-shutdown-20261021/final.json`；构建日志 `logs/issue67-nvapi-shutdown-20261021/build-20261021-222653/`。源改动仅NR驱动模块生命周期与关闭日志，不增加逐帧处理/等待，不改变UI频率、GPU优先级或20秒watchdog。PIN保留的是固定一份NVAPI模块映射；NR模型、特征、参数、纹理和NGX核心仍释放，不是保留显存资源规避问题。

验收过程中一次汇总脚本因混合编码日志按严格UTF8读取失败（UnicodeDecodeError）；改为保留原文件、容错读取ASCII阶段字段后汇总通过，没有重跑或修改原始结果。第一次停止视频时出现FFmpeg packet corrupt/Invalid NAL size文字，第二轮未出现；两轮重开均正常，未触发引擎失败。另有既存swscaler d3d12输入警告，本轮不改解码路径，也不宣称修复这些文字。

**限制**：没有RTX3090或616.92，未在本机复现原20秒阻塞，也没有该用户dump。本改动是针对可确认的卸载风险的修复与SF路径防护，不能据此宣称已经证实原用户根因或彻底修好3090。若原机仍复现，用新增成对日志区分NR卸载、core设备健康检查、Shutdown1，并取对应线程dump，避免继续猜驱动内部行为。未发布测试包、未合main、未push、未关闭issue。
