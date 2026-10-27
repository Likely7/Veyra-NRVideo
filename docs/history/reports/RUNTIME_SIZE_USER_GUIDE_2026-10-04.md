# Veyra 2.0.2 本地完整测试包 · 2026-10-04

这是完整便携包，解压到一个新目录，双击 `veyra_qml_ui.exe`；无需安装Python或开发SDK。
保留原测试包作回退，不直接在旧包运行时覆盖文件。7z为较小下载，ZIP为备用，相同功能与载荷。
7z可用支持LZMA2的解压软件完整解压；不能仅运行压缩包里拖出的EXE。

已包含Xbox音频/重连、导出码率编辑、RTSS兼容背景与NVIDIA App误检测修复，AMD NR、
VFG全部2X–8X/低中高质量，以及极简窗口右侧像素缺口修复。历史2.0.2版本号未作为正式新发布升级。

- VFG：专业列表“补帧”选择 NVIDIA VFG；节点模式同源，默认中档。仅RTX40/50范围，本机仅5070短测。
  高档8X开销大，预览可能按预算降档/跳帧；离线导出完整处理。提交FPS不是物理屏幕FPS。
- AMD NR：RX9000的NR版本显示“RX 9000 · lmxxf（实验）”，资产位于 `runtime/amd-nr`。
  NVIDIA显卡继续显示三个NVIDIA NR版本；AMD模型不会在5070上获得运行支持。
  AMD内部最高1080p预算，不支持HDR/超预算原生4K NR导出。需要AMD驱动自身的HIP运行支持。
  资源/ABI已校验，RX9000推理和长稳仍需实卡反馈。
- VFG仅去掉当前路径未用NPP，不改插件/质量档；AMD生产表/权重/两种HIP架构保留原字节。
  无损压缩只减少传输大小；模型解压后仍占原空间。GPU、模型和主机未验项见验收文档。

本轮HDR/Dolby Vision PR #13/#14仅审查，没有实施或并入。方案在
`docs/HDR_DOVI_PR_REVIEW_2026-10-04.md`：输入自动识别、处理区唯一HDR效果卡、输出状态与显示校准。
先修P5精确颜色/亮度统计与reset，再做预设/worker兼容和UI，取得参考及实机证据后按用户批准范围合并。

这是本地实验测试候选；未merge main、未push、未GitHub Release。许可证与逐文件manifest随包。
交付目录 `DELIVERY.md` / `DELIVERY.json` 是最终完整包大小、SHA、验证结果与源码存档索引。
