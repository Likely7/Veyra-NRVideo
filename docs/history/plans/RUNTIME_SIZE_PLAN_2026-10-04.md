# VFG / AMD NR 瘦身与完整本地测试包

用户追加授权尽量瘦身，以稳定性为先；构建完整测试包并汇报HDR PR合并方案，完成保存后关机。VFG是按前文解释消息里的VSF。HDR仍未批准施工或合并，不扩大为发布。

基线 `fc2ca7163e986f97e53dcab51ffe1ac77bb0615a`，包含极简黑边已验修复和PR审查。独立分支 `codex/runtime-size-20261004`，开工tag `checkpoint/pre-runtime-size-20261004`，完整bundle在 `E:/项目/Veyra/archives/runtime-size-20261004/source-before.bundle`，verify成功，SHA256 `6d2b64b471cb84effd84670a07f3079b5f71f630940926338052099e4cbfd694`。

1. 按文件/压缩后贡献度统计；检查原始DLL的import/delay-import与实际运行依赖闭包、上游代码对AMD权重/kernel的读取。区分模型、诊断产物、备份/未用资产和必要fallback，保留来源/SHA/许可证。
2. 只裁剪可以证明不使用的文件。VFG必要接点限定 `src/pipeline/VfgBackend.cpp` 的依赖加载；AMD若无法在无实卡条件证明可删，则保留必要权重/kernel，不以量化、重编内核、改模型换大小。完整档位、两种RX9000架构与格式兼容保持。
3. 能力需要的runtime保留原字节身份；新产物仅在E盘runtime-size-20261004各用途目录。可评估ZIP/LZMA2等压缩并标注解压兼容性，不引入运行时自解压/联网补模型。
4. fresh/incremental构建当前QML+native VFG测试目标；裁剪候选测试2X–8X×三档、8/10bit、重建/热切换/缺失依赖与真实worker导出。AMD至少重核实际API/layout与模型/kernel清单，推理未验明示。没有收益或验证不充分时保留文件。
5. 完整测试包带现有Xbox/码率/RTSS/取消检测修复、AMD NR、全部VFG和黑边修复，生成manifest/来源/对应源码与新的交付说明。验证独立解压/Windows最小PATH/没有开发SDK环境，运行/退出/worker/页面与黑边；原候选/main/桌面不改。
6. 完成WORKLOG、瘦身数据与HDR PR审批方案、源码存档。停止本轮测试/build子进程，按用户要求关机；不杀其他用户应用来跑测试，不代发消息。
