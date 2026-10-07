# AMD NR 开关无变化：已复现的资源交接错误和测试候选

1080p 文件和 XSX 的日志都存在同一条确定错误：NR 已执行并合成到源尺寸结果，但普通 NR → FSR 顺序让 FSR 重新读原始画面，最终输出丢弃了 NR 的变化。没有有效光流的首帧/断点回退缩放也读了原图。本轮已修复这两处并通过共享 GPU 链路像素回归；RX 9070 XT/HIP 与真实 XSX 串流仍待用户复测。

## 证据与原因

用户 veyra-qml(37).log（SHA256 b0e8ce013adf600fd033133e26235fc2cf195612e9f39085ed04bb3f71ac90bf）记录 RX 9070 XT、文件1920×1080/23.976与串流1920×1080/60。两者均为 NR → residual(source) → FSR SR(3840×2160) → XeSS FG，FSR枚举选中4.1.1。NR开关触发了设置版本应用和graph重建；AMD session 1–5有成功enqueue、HIP=1、modules_ok=62、共享合成记录，文件NR计时约11.1–11.47ms。这说明有工作执行，不说明结果已经送显。

EnhanceGraph::runSr 中，非split且无独立pre-SR调色时，FSR color参数被指向srcRgba_；应使用已包含NR残差、保护和边界调色的srStageInput()。普通blit也只在split或调色存在时选择该边界。代码在已发布2.0.5/main af5bfc3中已经存在；git -S追溯至4fbc2b0的2.0.0整合，当前VFG/Blackmagic候选没有引入它。此追溯只说明Git首次出现，不猜测历史作者的具体编辑时刻。

生产改动仅 EnhanceGraph.cpp：FSR color统一取srStageInput；普通缩放统一读已有描述符18并设置读取状态。NR推理、模型、FSR provider选择、XeSS/DLSS/FSR/VFG补帧、解码器、采集及着色器均未改。该修复纠正结果路径，不宣称AMD推理耗时降低。

旧AMD测试provider原样复制输入，原图与NR输出相同，无法检测“后续读错原图”。新增测试marker为确定的线性FP16颜色(0.5,0.125,0.0625)，GPU一次上传后按原ABI交付，与源(32,96,160)的sRGB代码明显不同；最终送显纹理必须保留该变化。测试provider只用于测试，不能进入产品包。正常播放没有新增像素回读或CPU等待。

## 本机验证

本机为RTX5070，真实FSR provider为3.1.5；AMD NR使用仅供测试的C ABI provider，测试专用graph只替换AMD厂商准入条件。生产厂商准入保持原样。以下结果不能替代AMD/HIP、FSR4.1.1画质/性能或真实Xbox网络串流验收。

| 检查 | 结果 |
| --- | --- |
| 修复前同一非恒等测试 | 6个NR-before-SR用例失败；NR和FSR均有执行，最终仍是原图；期望marker的最大8bit代码误差156 |
| 修复后10个交接用例 | 全通过，所采样位置最大代码误差0；包含23.976/60fps合成时间戳、单/双层、时域、NR零强度、NR关闭、SR→NR对照、普通缩放及光流首帧/断点回退 |
| 1080p→4K交接 | 合成1920×1080/60fps PTS输入、3840×2160输出、2次reset、6次NR/4次真实FSR执行通过；不是实际XSX播放性能测试 |
| 原有7个GPU恒等合成用例 | 单/双/四层及时域开关全通过；逐像素最大误差0 |
| 原有3个4K导出图用例 | NR关闭/单层/双层，完整3840×2160输出及内部1080p预算全通过；这不是实际编码导出测试 |
| D3D12 debug | 修复前后均0个ERROR/CORRUPTION，设备存活 |
| C ABI/失败处理回归 | 62项，0失败，含旧ABI、厂商拒绝、缺失运行库、坏函数表、enqueue失败 |
| 既有effect chain检查 | 264条PASS，最终all checks passed |
| 产品冷启动/基础文件播放 | 13.140秒，退出0，26个Qt/FFmpeg模块均从候选自身目录加载；无QML/引擎ERROR，独立E盘profile，默认效果关闭 |

命令（均在本轮E盘worktree，python为本机3.11，-B不创建源码pycache）：

```text
python -B scripts/acceptance/amd-nr-fsr-build.py build-before veyra_amd_nr_graph_tests veyra_lmxxf_nr_tests veyra_effect_chain_tests
python -B scripts/acceptance/amd-nr-fsr-tests.py pixels-before before
python -B scripts/acceptance/amd-nr-fsr-build.py build-after veyra_qml_ui veyra_amd_nr_graph_tests veyra_lmxxf_nr_tests veyra_effect_chain_tests
python -B scripts/acceptance/amd-nr-fsr-tests.py pixels-after after
python -B scripts/acceptance/amd-nr-fsr-tests.py abi-after abi
python -B scripts/acceptance/amd-nr-fsr-tests.py chain-after units
python -B scripts/acceptance/amd-nr-fsr-package.py prepare
python -B scripts/acceptance/amd-nr-fsr-package.py cold cold-after
python -B scripts/acceptance/amd-nr-fsr-package.py final
```

两次构建分别77.094/81.829秒exit0；修复前GPU测试3.812秒exit1为明确预期失败，修复后2.500秒exit0；ABI0.390秒、chain0.031秒。全部串行GPU测试，无竞争负载，每进程≤300秒，构建≤900秒。日志、各步JSON回执在E:/项目/Veyra/logs/amd-nr-fsr-handoff-20261007；构建在build/同任务，测试/profile在tests/同任务，临时目录在tmp/同任务。原始日志副本含私人会话数据，只留私有证据目录，不进入源码和便携包。

## 候选与实机复测

- 本地便携目录：E:/项目/Veyra/test-packages/amd-nr-fsr-handoff-20261007/Veyra-2.0.5-amd-nr-test-AMD-win64-portable
- 便携ZIP：上述目录名.zip；对应源码：同目录下Veyra-2.0.5-amd-nr-test-source.zip。
- EXE显示版本2.0.5-amd-nr-test，SHA256 c57ef5fa2db6d297d5f9393b45c5866faa1504a7ffabe9621d52f91d2ecc6b9b。
- 沿用正式2.0.5 AMD运行库/模型/许可证/patched FFmpeg原字节；包内保留先前VFG cc22e88和Blackmagic4294341候选改动。更新EXE、既有Blackmagic界面/notice及本轮说明/manifest，没有替换NR DLL、模型或FSR运行库。包内shader与本次GPU测试使用的build/shaders逐文件相同。
- 最终审计由amd-nr-fsr-package.py final在打包后写入E:/项目/Veyra/verify/amd-nr-fsr-handoff-20261007/final-check.json，包含源码对应、ZIP CRC与每项payload hash、候选/原包隔离和测试provider不入包的检查。主分支和其它32个工作树保持。

请完整解压到新目录，先用原1080p视频和相同预设保持NR→FSR顺序，确认打开/关闭NR能改变同一段或暂停帧；再复测XSX1080p60。需先确认NR该层和FSR已启用，总变化强度大于0。对比时可暂关自动画面调控并设总变化强度2，减少保护参数抵消变化的干扰；测试后恢复个人设置。开关需要graph重建，等待状态完成再判断。

## 另一个独立记录与限制

XSX日志另有6次D3D12VA send_packet -22，硬解重启仍失败后回退软解并恢复帧。这不能解释正常本地文件同样无NR变化，本轮未改该解码路径；若测试时串流出帧异常，需要按该独立故障继续取证。

自查确认修复没有绕过生产AMD准入，没有以identity输出或Create/Enqueue成功代替增强像素证明；NR关闭、零强度和SR→NR对照均保持原图/预期结果，原多层和4K输出未截断。没有独立Reviewer或AMD实卡验收记录。物理Blackmagic兼容、此前NVIDIA显存持续增长和较高VFG倍率限制仍保留原候选边界。本轮只交本地测试候选，不改变正式发布版本。
