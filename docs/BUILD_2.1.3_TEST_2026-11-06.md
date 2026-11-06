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
