# 极简右侧像素缺口：本地验收

基线 `a927f92`，隔离分支 `codex/minimal-edge-hdr-review-20261004`；开工存档与bundle见本轮方案。立即修复黑边、HDR只审查，未 merge/push/Release。

## 复现与实现

真实播放自有1280×720 cyan H.264，GetClientRect/ClientToScreen测主窗与native child，再截取本测试进程的无遮挡client并检查边缘像素。不用Qt grabToImage证明原生视频内容。测试子窗临时topmost且不激活其他应用；capture前检查采样位置所属PID，避免其他程序遮挡。fixture与独立profile/日志/TEMP均在E盘。

旧包EXE `be4da2ff173aa74de8e2e1d5ecc73d40a23584ba8898ffd534e636a8793b3d6b`，`before-v3` 6个真实GUI进程重测：

| Qt scale / 逻辑宽 | 主窗 / 视频子窗（物理像素） | 结果 |
| --- | --- | --- |
| 125% /642 | 803×451 /802×451 | 右缺1列，最右像素黑、内侧cyan |
| 150% /643 | 965×543 /964×543 | 同上 |
| 175% /641 | 1122×632 /1121×631 | 右与底各缺1像素 |
| 100% /642、200% /643 | client一致 | 无该native缺口 |

先修子窗/region后，125%仍有contain亚像素黑线，所以不能用“client一致”冒充已修视觉问题。最终：

- 满窗宿主使用parent HWND实际client，region使用child实际client，部分/动画viewport保留原稳定floor。
- 仅自动按影片比例取整的极简窗口、默认居中fit启用有界像素映射，完整输入映射到整数client。阈值来自逻辑高度和物理窗口的取整误差；全屏、最大化、其他页面、原始像素、zoom/pan、自选比例和真实黑边保留原变换。
- 颜色、XeSS motion及previous-view使用同一局部变换；不改shader、增强图、HDR、导出、源/音频或设置格式。

## 当前证据

- `python -B scripts/acceptance/minimal-edge-build.py build-v1.log veyra_qml_ui`：完整472步骤构建exit0；`build-v2.log`：最终geometry增量5步骤exit0。已有PreviewView等C4244仍在，不宣称无警告。
- `minimal-edge-tests.py after-v2 after full`：30个独立真实GUI用例，100/125/150/175/200%×641/642/643宽×GPU/软件。root/child右与底差为0，所有最右列与内侧/中心为 `[0,231,255]`，全部exit0。
- `minimal-edge-tests.py behaviors-v1 after behaviors`：12用例，resize、pro↔min、source-menu、原始像素、全屏4:3、专业页；边缘铺满与故意的比例黑边均保持，source-menu日志true。原始像素和全屏4:3两侧黑边保留，pro上下letterbox保留。
- `minimal-edge-tests.py page-cycle-v3 after page-cycle`：补充GPU/软件两例，跨页后确定性642×361逻辑窗口、803×451物理窗口，子窗一致，右列cyan。
- `hdr-pr-review.py`：固定head、缺失依赖、RPU offset、array view静态检查与独立PQ反例完成，结果为`hdr-pr-static-findings.json`；未构建或运行HDR贡献者代码。方案 `HDR_DOVI_PR_REVIEW_2026-10-04.md` 等待用户批准。

GUI每例子进程有独立短时限，不把所有用例总时长当作单次长稳测试。机器实际RTX5070/616.56；Qt scale factor模拟分数DPR，未改变Windows全局缩放，没有做跨不同物理显示器DPI/长稳/FG或HDR新画质验收。没有扩展为AMD/Xbox实机通过。

## 保留的失败与纠正

`before-v1`的C++ --size在QML启动后覆写cinema高度，不是有效影片贴合fixture；改为真实QML fit。早期before-v2的中间采样受控件覆盖、after-v1只检查native尺寸不足，未作为最终视觉通过。之后将测试进程无遮挡检查与真实右列像素纳入验收。

`page-cycle-v2`期望451高，但同一JS回调跨页改宽后用到旧pictureHeight，实际506高，断言失败；fixture改为下一QML tick设宽并显式派生高度，v3两例通过。该失败不通过延长等待或放松断言掩盖。产品代码未为测试期望改布局。

隔离audit初次漏掉归档status的branch行而失败，working/index patch、11份源码hash和原包hash当时已一致。补上与开工一致的`--branch`后重跑，不能删除差异行来洗白真实源码变化。

## 交付与隔离

最终EXE `8a2197ccd9b1716df1dad21169b49611c60ae65461c16171d580995da34922b0`，17,299,968 bytes，在 `E:/项目/Veyra/build/minimal-edge-hdr-review-20261004/`。
只打本地EXE补丁，依赖已有`2.0.2-vfg-20261003`完整便携包；不是独立程序、不是公开2.0.2补丁。补丁CRC/SHA、source commit和基包要求写入`test-packages/minimal-edge-hdr-review-20261004/DELIVERY.json`。
原完整包、桌面working/index patch、未跟踪源码和main在`minimal-edge-delivery.py audit`复核；具体结果在`logs/minimal-edge-hdr-review-20261004/source-isolation.json`。运行库/SDK/模型和测试媒体不进Git。

本轮必要测试副本/截图/失败证据留在对应E盘目录；模型/DLL用只读硬链接复用原包，没有再复制一整套模型压缩包。没有重试此前被拒的清理操作。
