# 菜单首次布局与AMD原生HDR+NR修复

用户要求暂停2.1.1封包，修菜单/下拉文字抖动和AMD原生HDR与NR不能共用；随后否决淡入淡出替换，指定2.0.7为正常动效对照。分支codex/menu-amd-hdr-fix-20261026，起点2de14fd，未合main、未封包/推送/发布。

## 菜单

直接读取本机正式2.0.7 NVIDIA ZIP，核对VMenu.qml、VSelect.qml、Theme.qml与施工前HEAD：三者内容完全一致。不能声称最近改了菜单动画导致回归，也不能凭正常弹簧位移判定文字抖动。

最终复现的是首次打开前嵌套布局未结算：在排除整体动画变换的局部坐标中，第一行文字y由14跳到42。修复只在openAt中先设定内容宽度、ensurePolished内容与列布局，再定位/打开；去掉打开后Qt.callLater重定位。动态宽高/模型/窗口边界适配仍在。enter/exit、缩放/位移曲线与时长整块代码与2.0.7一致，VSelect和Theme不改；没有新增淡入淡出方案、字体变更、layer缓存、全窗重绘或定时器。

证据：reports/menu-amd-hdr-fix-20261026/207-comparison.json；正确失败用例logs同名/menu-layout-before.txt（局部y 14→42）；修后menu.json覆盖software/d3d12×1/1.5/2缩放，分别检查菜单和下拉入口的局部布局不跳、原spring仍运行。该复现证明当前存在首次布局变化，不能进一步断言用户2.0.7与当前所有运行环境完全一致。

QtQuick全套117 passed / 0 failed / 1 skipped；跳过的是要求显式产物目录的截图用例。截图用例随后单独运行3 passed，使用QScreen::grabWindow读取已呈现的测试窗口，保存6档原缩放动效截图；人工查看150%下文字/菜单完整。不是全软件所有弹窗的长测。

撤回与失败如实保留：最初“文字全局位置必须不动”的测试只抓到了原有弹簧运动，不能证明bug；淡入淡出方案已按用户要求完全撤回，该方案的通过记录标记rejected-fade-only。软件layer原型截图出现裁剪，font hinting原型逐像素无变化，均未进入最终产品。截图第一次用例等一个已无更新的frame超时；第一次补测试未同步exe旁qml-tests导致找不到函数，修正工具后再测，均不计通过。Qt软件文字渲染限制只用于探索，不用文档推断代替本机结果，参考[Qt软件渲染说明](https://doc.qt.io/qt-6/qtquick-visualcanvas-adaptations-software.html)。

## AMD原生HDR+NR

根因是EnhanceGraph初始化AMD NR时，遇到hdrWorking直接return false，触发后端回退。不是RTX Video HDR；当前用户已明确是原生HDR。

修复复用现有HdrColor.hlsli的203nit固定参考代理及HDR残差还原：每层原始线性HDR输入保持，额外两个NR内部尺寸FP16纹理用于SDR线性代理和模型输出。模型前HdrProxy，模型后HdrRestore，再进入现有全分辨率残差/保护/时序合成。代理不是PQ、不是sRGB编码；AMD运行时继续接受原来的线性SDR FP16合同，模型不被宣称为原生HDR推理。高光、负宽色域分量和黑位保持原HDR基底；不添加CPU像素回读、逐帧CPU fence等待或额外队列。

仅AMD NR+原生HDR工作模式分配两纹理与两pass；SDR、NVIDIA路径不增加这些资源/dispatch，AMD vendor/模型/ABI校验不放松。记录amd-nr-hdr路由日志，资源随每层释放，复用已有reset和单队列producer→HIP→consumer顺序。运行库/模型/驱动字节未改；QML说明与四语目录同步。

验证：veyra_amd_nr_graph_tests使用明确标记的fake C ABI provider和实际D3D12图/着色器，128 cases / 0 failures，其中108组HDR组合：scRGB/PQ-P010/HLG-P010，HDR10与scRGB输出，1/2层、降采样开关、时序开关、恒等/改色输出、残差零强度与中途reset。恒等误差<0.003；非恒等确认中间调变化、高光/黑位保持；D3D12 debug errors=0。其余20组为既有SDR/SR/4K合成回归。

veyra_lmxxf_nr_tests ABI/模型/顺序/失败保护通过；veyra_hdr_color_tests真实GPU颜色/HDR/P010/scRGB回归通过。另用NVIDIA原版NR执行20帧真实NR+原生PQ HDR：NR=20，highlights=998.932nits，pass=1；shader identity覆盖黑位/1000/4000nit/宽色域并通过。此测试不是AMD推理或屏幕光学验收。

**本机无RX9000：HIP真实推理、AMD用户素材画质/帧率和原用户机器仍需实测。** 不因fake provider通过声称AMD硬件已经验收。

## 构建、回执与暂停交付

- 构建：scripts/acceptance/build-recording-handoff.py，task codex-menu-amd-hdr-fix-20261026，复用build/claude-screen-recording-20261022，-j2、label 2.1.1。主构建60.7秒，后续定向增量回执均在logs/codex-menu-amd-hdr-fix-20261026。
- 回归：python -B scripts/acceptance/menu-amd-hdr-fix.py gpu / menu / contracts；各次进程<=280秒。contracts包含效果链、UI/Repair、翻译、QtQuick；翻译2674条，缺译/占位符问题0。
- NVIDIA真实HDR命令：tests/menu-amd-hdr-fix-20261026/nvidia-hdr/veyra_hdr_enhancement_tests.exe 1 3，6.032秒；日志及nvidia-hdr.json在同名logs/reports。
- 路径均为E:/项目/Veyra/{reports,logs,tests,tmp}/menu-amd-hdr-fix-20261026；最终索引FINAL.json绑定提交、图源码、EXE和验收回执。源码受控，SDK/运行库/模型/私有媒体不入Git。
- 一次build-dir拼写错误误建build/claude-screen-recording-fix-20261022（多了fix），仅停止本工具所属9个进程，改回既有正确目录构建。尝试按已核实路径删除误建目录被自动审批策略拒绝（仅提示blocked by policy）；未换工具/方式绕过，目录保留，见cleanup-blocked.json。
- 2.1.1旧stage仅代表修前内容，reports/codex-build-2.1.1-20261026/PAUSED.json已标记用户暂停及过期，恢复封包时必须重建stage，不可直接seal旧目录。此轮不恢复封包、不覆盖E:/App或用户配置，不占采集卡，不动其它分支与main，Lecram暂停。
