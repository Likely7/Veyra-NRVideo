# RX7000 NR接入执行记录

用户2026-10-29授权加入RX7000支持，包含Small性能模型。基线e7a9ae1，分支codex/amd-rdna3-nr-20261029，checkpoint/pre-amd-rdna3-nr-20261029；不合并main、不push/Release。保留RX9000 lmxxf路径，不改真实配置/驱动/已有SDK与运行组件。

采用RedDukeDev/dlss5-nr-amd-custom MIT网络层（固定ab455d179922adc11f65675d399e1d553bf423f0），不直接接游戏代理、不引入ReShade/OptiScaler。网络运行在独立的Veyra worker进程，隔离ZLUDA/NGX/NVAPI替身与NVIDIA链路；D3D12共享纹理/fence，不回读图像，不使用上游异步旧残差重投影。每次推理对应当前源帧；主图继续使用既有NR尺寸、HDR代理、残差、保护和历史合成。

步骤：核验依赖与网络层合同；实现有界共享GPU协议及Small/Original选择；接入现有AMD NR接口与硬件能力/组件错误；构建和CPU/D3D12/设置/QML回归；制作私有配置测试入口、保留源代码与许可/manifest/必要证据。每测试≤300秒、构建并发2/≤900秒。

所有产物统一E:/项目/Veyra下amd-rdna3-nr-20261029任务目录，复用构建与测试运行目录，不复制完整工程。外部固定开源源码与运行组件在deps/amd-rdna3-nr-20261029；专有DLL/模型/编译缓存不入源码Git。最终按实际验证记录；本机无RX7000，模拟合同/本机D3D12测试不冒充实卡推理、画质或性能验收。

## 实现结果（2026-10-30）

AMD runtime 数字 ID 4 不变，根据实际适配器将 RX 7000 路由至 RedDuke worker、RX 9000 保持 lmxxf 0.44。没有把文件存在当成显卡支持，也没有顺手向7000开放FSR4。性能模式对应 Small（preset 2），关闭对应 Original（preset 1）；每层模型与百分比独立保存，列表和节点共用现有编辑器。内部尺寸仍遵守现有AMD上限：像素≤1920×1080，宽≤2560，高≤1080；支持手动25–100%，超出预算按既有尺寸策略处理。输入可以高于1080p，内部预算不等于最终显示尺寸。

每层独立子进程，主程序不加载ZLUDA替身。linear RGBA16F经Veyra既有parity编码进入上游要求的RGBA16F显示代理；零depth/motion、每帧reset，既有HDR基底还原及残差/保护/历史由Veyra处理。强度在解码中混合，不随滑条反复重建网络。一个原版310.9.16.0 DLL承载两种模型，切模型时重建worker。

GPU顺序：源队列signal → CUDA外部semaphore wait → network enqueue → CUDA私有完成fence → worker D3D12 queue wait → GPU有效标记 → 公共done fence → 源队列wait并解码。默认流完成顺序沿用上游gate事件同步所用顺序；本机无法验证ZLUDA与RX7000实际驱动互操作，初始化明确要求原生共享纹理和GPU semaphore，不允许CPU像素回读兜底。稳态不加CPU GPU-completion等待，控制消息只确认提交。有界8槽结果队列；初始化最多180秒，预热完成才ready。稳态5秒worker看门狗只终止本层子进程，解除自有等待；无有效GPU标记时输出精确保留当前源图，不能用旧结果冒充成功。

## 来源、组件与构建

开源来源和修改详见 `third_party/redduke/VEYRA_ORIGIN.md`。源码准备：

```
python -X utf8 -B scripts/amd-nr/prepare-rdna3-source.py --out E:/项目/Veyra/deps/amd-rdna3-nr-20261029
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-amd-rdna3-nr-20261029 --root E:/项目/Veyra/worktrees/screen-recording-fix-20261022 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.1-test --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_qml_ui veyra_amd_nr_worker veyra_amd_nr_test_worker veyra_rdna3_nr_contract_tests veyra_lmxxf_nr_tests veyra_repair_contract_tests veyra_effect_availability_tests veyra_nr_model_settings_tests veyra_ui_i18n_tests
```

后续仅重建受影响目标，最终产品/worker/GPU测试批次6.1秒退出0，日志 `logs/codex-amd-rdna3-nr-20261029/build-20261030-005622/compile.log`。QtQuick另目标 `veyra_qml_quick_tests`。没有声称既有编译警告消失。

上游1.3.0下载包502830084字节，SHA256 `779b7d375bbe6d2419bcbd68e3d229c84acf09c74206837cfb0e12a253da1b00`。筛选24个组件470120771字节，追加两份ZLUDA许可证及本地native worker；stager审核原始网络Valid NVIDIA签名、版本和SHA后形成27项manifest。原始模型、11个NR内核、缓存及开源兼容层均留E盘任务deps，GPU解码shader单列SHA；驱动/HIP SDK不随包复制。既有运行组件保持原样，正式发行白名单不扩展。本地app中的cache为独立可写副本，其余只读组件可硬链接到本任务deps。

```
python -X utf8 -B E:/项目/Veyra/tmp/claude-sr-stage-20261028/stage_app.py
python -X utf8 -B scripts/amd-nr/stage-rdna3-runtime.py --dependency-root E:/项目/Veyra/deps/amd-rdna3-nr-20261029 --components E:/项目/Veyra/reports/amd-rdna3-nr-20261029/runtime-components.json --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --app E:/项目/Veyra/tests/claude-sr-stage-20261028/app --report E:/项目/Veyra/reports/amd-rdna3-nr-20261029/staged-runtime.json
```

## 验证与失败记录

执行脚本 `reports/amd-rdna3-nr-20261029/run.py`，GPU/CPU/lmxxf分别以 `gpu final`、`cpu final`、`lmxxf final` 运行；QML以 `qml rdna3-ui-fixed` 运行。实际应用夹具 `scripts/acceptance/amd-rdna3-ui.qml` 由本轮 `ui.py` 以独立data-dir装载，随后 `--verify` 重启复验。

| 检查 | 实际结果及边界 |
|---|---|
| D3D12跨进程协议、颜色、模型并存、错误顺序/崩溃/无结果保护 | 54项0失败；明确copy测试worker，不是NR推理 |
| GPU能力/路由/协议 | 72项0失败 |
| 既有repair合同 | 300项0失败 |
| RX9000 lmxxf原有ABI与GPU合同 | 66项0失败；fake provider，不是RX9000推理 |
| NR设置、旧配置、每层Small/Original、全部76个百分比及导出冻结 | 0失败；不是AMD导出实卡验收 |
| QtQuick | 123通过，0失败，1项既有跳过 |
| 实际应用列表/节点保存和重启 | 通过，无QML错误；原用户配置SHA未变；NVIDIA不会因新增组件被误判为AMD可用 |
| 翻译 | 2712条，缺失0，占位符问题0 |
| RTX5070原生NR＋SR＋DLSS 2X＋Boost回归 | 22.47秒，通过；149处理帧，源时间速率0.987573，干净停止 |

NVIDIA实测复用旧runner，真实回执位于 `reports/nvidia-low-latency-20261028/gpu-native-effects-2-2-rdna3-regression.json`；没有把日志伪装成新目录。其余回执位于 `reports/amd-rdna3-nr-20261029`，原始日志 `logs/amd-rdna3-nr-20261029`，失败也保留。

初始编译遇到WinRPC small宏和共享句柄接口类型错误，修正后构建通过。初始颜色夹具误把既有8bit parity当成恒等映射；改独立CPU golden，并采用上游要求的RGBA16F代理。FP16转换舍入差异使一轮过严的夹具失败；最终分别用CPU双精度转移检查编码在1 FP16 ULP内，再用实际量化代理检查解码，未放宽无标记回退（误差严格0）。QML mock初次重复声明自动生成信号导致测试程序退出，修正夹具后通过。实际UI夹具又遇到Item自带layer属性遮蔽函数、空节点图未添加NR，两次失败独立保留；改夹具后列表/节点及重启通过。CPU runner最初漏传设置输出目录，补参数后通过。以上没有修改历史失败日志或产品预期来伪造通过。

## 本地交付和验收边界

入口：`E:/项目/Veyra/tests/amd-rdna3-nr-20261029/启动RX7000-NR测试.cmd`；复用app：`E:/项目/Veyra/tests/claude-sr-stage-20261028/app`。入口使用专用interactive-profile，不自动打开媒体，首次效果关闭，不碰真实用户配置。选择实际RX7000适配器和AMD NR；先用1080p内部Small测试。目标机需要可用HIP7驱动组件（System32或绝对HIP_PATH），缺少时会给出原因；初始化必须实际导入共享资源成功。

当前机器是RTX5070，没有RX7000实卡，因此不能声称7000全系已跑通。RX7000的实际推理、Small/Original画质和性能、原生HDR代理还原、真实导出、采集、VRR及长时间稳定性未验收；不把GPU合同或上游数据代替现场验证。原8K最终输出＋NR＋VFG危险组合没有重跑。没有合并main、push或Release；正式2.1.1包不包含本分支新增功能。收尾文件身份、保护、清理及提交信息见 `reports/amd-rdna3-nr-20261029/FINAL.json`。
