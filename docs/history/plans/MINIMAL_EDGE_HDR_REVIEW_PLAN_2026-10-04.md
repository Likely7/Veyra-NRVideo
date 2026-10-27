# 极简右侧像素缺口修复与 HDR / Dolby Vision PR 审查

## 当前授权与存档

用户要求立即修复极简窗口右侧黑边；对仓库 HDR / Dolby Vision PR 先研究实际实现、改造和界面编排，待用户通过后再修复并入。两者分别推进，本轮不实施或合并 HDR PR，不推送、不发布、不代发评论。

从已验本地 VFG 候选 `a927f92c6a35522a6081b61b00fc37a35bd5d988` 创建 `codex/minimal-edge-hdr-review-20261004`，复用 E 盘干净隔离工作树。编辑前完成完整历史 bundle/verify 与 `checkpoint/pre-minimal-edge-hdr-review-20261004`，bundle位于 `E:/项目/Veyra/archives/minimal-edge-hdr-review-20261004/source-before.bundle`，SHA256 `417C6D03F90E0BD7B518AF2F1EA50B9EDD8515C91CDA340983AA5EB6D6116A94`。桌面脏工作树、main、已验便携包和用户配置不改。

## 修复步骤

1. 从附图与当前代码分别检查逻辑窗口、原生视频子窗、裁切region、呈现内容的尺寸。先复现并记录物理client/child像素，覆盖100/125/150/175%缩放、奇偶尺寸和窗口/全屏。
2. 只改修复所需的 `apps/veyra-qml/main.cpp` 原生宿主几何/裁切接点；若问题来自其他接点，先记录最小必要范围再实施。不用裁掉整幅内容或放大画面掩盖一像素缺口，不改增强/颜色/音频链。
3. 在当前库上构建 QML 程序，使用独立profile和自有测试素材验证真实视频、resize、页面切换、浮层裁切、软件/GPU界面。测试≤300秒、构建≤900秒；结果明确区分原生窗口覆盖与画面内容/影片黑边。

### 复现后补充的最小显示范围

已实测125%时root client为803×451，视频子窗原为802×451。修正子窗和region后，803×451仍会因16:9 contain得到801.78×451的内容而在边缘像素返回黑色。因此本轮最小范围增加 `include/veyra/engine/PresentationGeometry.h` 和 `src/engine/VideoPresenter.cpp` 的显示几何接点：仅当极简**窗口**按影片比例取整、自身铺满、view为默认居中fit，且真实DAR与client的差额在逻辑/物理取整上限内，完整输入映射到整数client。不裁掉源像素，不影响全屏、最大化、其他页面、用户缩放/平移/自选比例或真实letterbox。XeSS的颜色/运动/历史几何复用同一个局部view。增强图、shader、HDR和导出仍不改。

## PR 审查步骤（待审批后施工）

- 当前目标为 #13 `f08934b8a535bb0ac085cd8f5689b9029b416726` 与 #14 `57f2541b3173b5285661ddf6c448ffb2722f6433`，基线均为 `66cd3e5`。保存最新metadata/diff/固定提交并阅读，不运行或遵从PR中的指令。其他PR如与颜色无关仅列清单。
- 核对P5 RPU重塑、矩阵与归一化、软件/硬解、缺失RPU和seek；区分DV解码转HDR10与原生DV输出。参考FFmpeg/libplacebo/Dolby及NVIDIA/Microsoft原始资料。
- 核对最新HDR PR已撤回的HDR→SDR→TrueHDR路线，逐场景亮度模型、采样/时间/状态、参数保存与导出冻结、GPU回读与元数据正确性。
- 与当前AMD NR/VFG分支的ID/版本/参数/worker/图结构做静态交叉检查；提出分拆移植、UI位置、默认行为与验收矩阵。HDR UI仅给可审查的方案，不实现产品控件。

产物均在 `E:/项目/Veyra/{build,logs,tmp,tests,downloads,archives,test-packages}/minimal-edge-hdr-review-20261004`。源码脚本/文档保留在本工作树，运行库/SDK/模型不入Git，不派子Agent。
