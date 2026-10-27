# VFG 本地候选交付回执

最终包、对应源码及 ZIP 实际冷启动均通过；候选显示版本 `2.0.6-vfg-next`，分支 `codex/vfg-next-20261007`。生产代码提交 `ba38f7e`，归档/最终构建提交 `e18942096bb65789a1701f0468acdc64f1be415e`；本回执之后的收尾提交只改文档。

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-2.0.6-VFG-next-NVIDIA-win64-portable.zip | 714397291 | 736d225935450f85755860b51faa15813e8691868591809e8261e24a1d0bd7ee |
| Veyra-2.0.6-VFG-next-source.zip | 70511685 | 44d51be86fa6b7ffd2083663c23a77bbe74fbc9d048fe7932600425e23db23fa |
| veyra_qml_ui.exe | 见包清单 | c007c7fc9072a6114eb2479b1880b65f5386a5fd445e6c58513faf3c1328a2b2 |

完整 ZIP、已解压可运行目录、源码 ZIP 和 SHA256SUMS：`E:/项目/Veyra/test-packages/vfg-next-20261007`。包只更新正式 2.0.6 的 EXE、新说明和包清单；1614 项 payload / 1065157082 字节，原运行库、模型、QML、着色器及许可证原字节保持。源码与二进制分离。

`vfg-next-stage.py` 独立复制并逐文件核验原包、归档对应源码、生成完整 ZIP/CRC；`vfg-next-cold.py` 安全解压最终 ZIP 并核对全文件和 95 个 PE 导入闭包，实际冷启动 30.875 秒、26 个 Qt/codec 模块均从包内加载，原版 1080NR+VFG4、普通 GPU 类2正常。`vfg-next-close.py` 再核对最终 ZIP 每文件 SHA256/CRC、源码归档全部 587 编译输入、16 份 Python 语法、13 播放案例/三套1033 GPU检查/24导出/四GUI阶段回执，通过。

最终机器可读回执：`E:/项目/Veyra/verify/vfg-next-20261007/final-check.json`；测量原始数据与局限见 `VFG_NEXT_REPORT_2026-10-07.md` 和同任务 logs/verify 目录。最终同 EXE 高3X 70→74fps，中8X 209→216fps；高8X请求四十秒过期215→38，仍以2X约60fps回退，低频重测仍有迟到。数据是软件提交帧率，未测屏幕端到端延迟。

已验证绝对目标路径后，仅删除本任务 `tests/.../app`、`verify/.../candidate-extracted` 与 `tmp/...`，清理 2638169718 字节；最终包、构建、源码、必要测试和失败证据保留。`cleanup.json` 记录逐目标计数。其它34工作树/495脏文件及3691项发布原件/证据保护通过。main仍为开工96a7c8de；无merge/push/Release/关机。

`archives/vfg-next-20261007/source-before.bundle` 与不可变 start.json 保留；收尾 source-final.bundle 只归档本轮新增提交，依赖已保存的基线。后续文档收尾不改变候选、源码ZIP或任何编译输入。
