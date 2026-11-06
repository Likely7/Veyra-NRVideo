# 2.1.3 本地整合与测试构建

用户授权合并Claude与Codex修复到main并构建2.1.3测试；不含push或GitHub发布。

## 整合

基线main f1f8298，存档checkpoint/pre-main-2.1.3-20261106；先在codex/integrate-2.1.3-20261106整合。
- NR分组网状/列表调节259ef8f：变化四轴+总量、画面手动九轴，共用原设置，其他参数原位。
- Claude菜单文字抖动05181bf：软件绘制开场弹簧首次到终点后停稳，不新增淡入淡出、不改变GPU曲线。
- 继承main已有全屏seek、RTX30 VFG、NVENC周期IDR、RX7000身份/设备选择、低延迟提示、三类RTX视频超分与混合双重超分。
- 只解决WORKLOG追加冲突，两侧记录保留；旧脏工作树保护，不合暂停Lecram、AverMedia诊断、不恢复预算或RIFE。

## 构建与组件

项目、显示版本和PE版本分别2.1.3/2.1.3/2.1.3.0。复用build/claude-screen-recording-20261022，强制reconfigure，并发2、单次880s，测试各进程不超过300s。
完整NVIDIA/AMD测试包来自已发布2.1.2不可变ZIP受控清单；替换本次EXE/worker/着色器/QML/i18n/文档。NVIDIA新增同一已用Video Effects SDK来源的VideoSuperRes DLL及其1.8.3 VSR，独立清单核验；AMD私有ZLUDA NVAPI必须用a7adb043…修复版，worker从当前源码构建。不含驱动、SDK、凭据、配置、测试媒体、PDB或测试EXE。
保留patched FFmpeg、许可证和对应源码。记录实际组件SHA，生成包/应用源码/网络源码清单；测试包不作为公开再分发授权。

## 验证与交付

新包上执行QtQuick软件完整套件、菜单软件/GPU多DPI、NR网控GPU、设置/合同/i18n，以及新运行库短测；ZIP逐项SHA/CRC、最小PATH独立配置解压冷启动和本地播放。RTX5070不代替RTX30、RX7000/9000实卡或物理显示延迟验收。
输出test-packages/codex-build-2.1.3-20261106；日志logs/codex-build-2.1.3-20261106；报告reports/codex-build-2.1.3-20261106，临时和验证目录同任务名。
本文件是计划，实际通过、失败修正、最终源码/包身份以FINAL.json和WORKLOG最终记录为准。

## 2026-11-06 已执行验证

产品源码快照4fde59a，构建日志logs/codex-build-2.1.3-20261106/build-20261106-041552，23目标105.6秒。main已包含Claude菜单05181bf及NR259ef8f；尚未合入的分支只有两条暂停Lecram与AverMedia诊断档，不作为缺失修复。

软件QtQuick172/0/0，菜单软件150/200%各6/0、GPU100/150%各5/0/1（软件专用行跳过），NR网控GPU13/0、分组11/0；设置/预设/能力/界面合同/i18n、HIP设备选择/worker诊断/跨进程GPU协议及VFG模块兼容检查通过。真实主程序NR九项逐参数读回、两组视图独立切换、自动手动保留、截图23项及重启恢复均通过，无engine/QML错误；本轮NR界面夹具效果关闭，不冒充NR推理验收。截图已查看。

七组SR生产导出各60帧（标准/高码率/流媒体及NGX/VFX四种双重组合），420帧对齐无错位；两编码各270帧，IDR在0/120/240，MP4原始stss为1/121/241。报告GPU-COMMANDS/SR-ALIGNMENT/NVENC-KEYFRAMES记录原命令与日志。没有30系、RX7000/9000实卡或HDR新超分画质验证。

额外DLSS2X生命周期短测：首次独立CUDA缓存首帧未在15秒门槛达到，随后初始化继续，8秒测量混入启动阶段，媒体速度0.644；同一缓存热重测14.3秒完成、媒体速度0.99211，暂停/seek/切档/缩放/退出通过。保留首次失败，不把热通过说成冷启动等待已修复。这是短功能测试，不是物理延迟或持续吞吐承诺。

首轮打包/夹具失败与修正：Windows PowerShell继承PSModulePath导致签名模块不可加载，按原规则只清子进程变量；新脚本误排除了整个qml目录，现只更新qml/Veyra且严格保留Qt模块；设置/协议测试应自行创建输出目录，pacing需完整参数，已纠正。首轮日志和CHECKS-FIRST保留，未放宽产品或验收断言。

本次所有命令、身份、阶段结果见reports/codex-build-2.1.3-20261106。封包后执行ZIP逐项SHA/CRC、独立解压最小PATH冷启动与保护复核；最终包/源码SHA及实际冷启动结果以FINAL.json/DELIVERY.json为准。

## 2.1.3封包与冷启动完成

源码归档快照b19846a，产品构建快照4fde59a；两者产品文件完全一致，后续提交仅补验收文档。NVIDIA ZIP 725403989字节、AMD ZIP 716398805字节，包内PE2.1.3.0；逐项SHA/CRC、原Qt模块/默认运行组件身份核验通过。应用源码73705776字节，RX7000网络源码29191字节，专有运行库/SDK/模型不进入源码Git。完整摘要在test-packages/codex-build-2.1.3-20261106/DELIVERY.json。

从两ZIP重新解压，最小PATH、独立数据/TEMP/CUDA缓存、预热/恢复关闭，本地1080p30播放8秒：NVIDIA12.5s退出0/提交209真实帧，AMD12.61s退出0/提交207真实帧，两者engine/QML错误为空。运行机器均RTX5070、默认效果关闭；不等于AMD NR或实卡验收。核验解压副本后，回收命令被自动审批拒绝（仅返回blocked by policy），两份副本仍在verify/codex-build-2.1.3-20261106原位；没有执行删除。最终包、源码、当前构建及测试证据均保留。

16棵其他工作树的原改动摘要均匹配，Claude新菜单四文件只是提交，原字节保留；main只剩Lecram两暂停分支和AverMedia诊断档不在祖先链。原发布ZIP/真实用户配置/共用测试目录未替换。未push或发布GitHub。新启动器在交付目录，使用tests/codex-build-2.1.3-20261106/interactive-NVIDIA或interactive-AMD独立设置，原用户设置仍在原位。
