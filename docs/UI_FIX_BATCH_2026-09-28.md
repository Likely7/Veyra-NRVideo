# P4前修复批次（2026-09-28，a30）

> 用户手测 a29-v2 后提出9条问题，裁定“可以写好直接开干”。本批只修这9条及其直接回归，不进入P4新功能，不改引擎算法/shader/CMake/采集/解码/音频/Present。不commit/push。
> 扩展基线 `E:/项目/Veyra/archives/p3-fixbatch-20260928-a30/scope-baseline.json`（a29哈希原样继承，仅新增本批QML/文档路径）。日志 `E:/项目/Veyra/logs/p3-fixbatch-20260928-a30/`，测试 `E:/项目/Veyra/tests/p3-fixbatch-20260928-a30/`。

## 已定位根因与修法

| # | 问题 | 根因（已读代码确认） | 修法 | 验收 |
|---|---|---|---|---|
| 1 | 暂停后无法继续 | `QmlPlayerBridge::togglePlayPause()` 调 `engine.pause(snapshot.running)`；暂停时工作线程仍 running，永远发“暂停” | 照旧版AppShell：采集/图片/打开中/停止中忽略；已结束或未运行且有文件→重开；否则 `pause(transport==Playing)` | 真实app：播放→暂停→继续，位置前进；空格键同路径 |
| 9 | 节点页胶囊显示“极简”；点专业回到列表UI但开关显示节点 | TopDock只有 min/pro/exp/set，node页找不到→落到索引0；dock/其他入口直接 `page="pro"`，不看 nodeMode | Main统一 `goPage()`：pro且nodeMode=1→node；dock高亮node视为pro；nodeMode变化时pro/node页随之切换 | 节点模式下点胶囊专业仍在node页且高亮专业；列表↔节点切换页面与开关一致 |
| 4 | 极简“自定义”文字错位；“去专业模式管理预设”变全屏 | VPill的状态点是pill直接子项（落在左上角），空key仍占间距；窗口模式下FullscreenBar的 `onRequestPage` 无条件 `toggleFullscreen()` | VPill加行内前置槽、空key隐藏；仅全屏时才退出全屏，再 `goPage()` | 点预设菜单“去专业模式管理预设”→专业页且非全屏；截图核对文字居中 |
| 3 | 窗口不能移动/缩放，无最小化/最大化/关闭 | 无边框窗口未实现系统移动/缩放 | 胶囊右侧加最小化/最大化(还原)/关闭；胶囊空白及顶部热区拖动移动窗口（`startSystemMove`），双击最大化；四边四角 `startSystemResize`；最大化时不套极简画幅高度规则 | 真实窗口：拖动位置变化、边缘缩放尺寸变化、三按钮生效 |
| 5 | 列表模式无光流、HDR入口 | ProPage补帧/画质页未放 | 补帧页加“光流算法”（复用 `setOpticalFlowChoice`）；画质页加 RTX Video HDR 卡（复用 `videoHdr` 与状态文字） | 列表模式真实选择后设置生效、保存重启保持 |
| 7 | 切换节点确认框不符UI；文案过时 | 两处用系统 `Dialog`+标准按钮；文案仍写“自由连线和任意顺序执行尚未接入” | 新 `VConfirm` 自绘确认框（主题色、VButton、视频遮挡cover）；更新文案 | 截图核对；确定/取消行为不变 |
| 8 | 节点面板操作 | 左键拖空白平移、双击空白加节点；无工具栏 | 右键空白→添加菜单（落点即新节点位置）；中键拖动平移；左键点空白只取消选中；按设计稿底部工具栏：适配视图、自动排列、添加节点、链路摘要；提示文字同步 | 真实鼠标：右键加节点、中键平移、左键不平移、归位/自动排列生效且保存位置 |
| 2 | 列表耗时面板超出 | 172px卡片纵向8行溢出 | 改两列×4行、裁剪 | 截图核对不溢出 |
| 6 | 滚动条系统样式 | DialogHost/SettingsPage使用默认 `ScrollBar{}`，VMenu/设置子区无样式 | 统一 `VScrollBar`（沿ProPage已有细滚动条） | 截图核对管理预设/设置页 |

## 顺序与停止条件
第一档 1→9→4→3，第二档 5→7→8，第三档 2→6。每档改完构建一次、真实app验证，失败同因最多两次有据修正；最终全量回归（P3回归集）+ 新候选 + 存档 + 四份文档。测试≤300秒、构建≤900秒。

## 结果（2026-09-28，全部完成）

| # | 结果 | 证据 |
|---|---|---|
| 1 | **通过**：暂停后位置保持；真实Space键恢复（0.983→1.950 s）；API第二轮暂停/恢复 | tier1-v2 |
| 9 | **通过**：节点模式点胶囊“专业”仍在node页且高亮专业；极简↔专业往返一致；列表模式回pro | tier1-v2 |
| 4 | **通过**：窗口极简下预设菜单“去专业模式管理预设”→pro且未全屏；状态点行内、文字居中（pill.png） | tier1-v2 |
| 3 | **通过**：最大化/还原/最小化；真实鼠标拖顶部条移动窗口(+138,+83)、拖右边缘缩放(1280→1373) | tier1-v2 |
| 5 | **通过**：补帧页光流选择生效；画质页HDR开关+对比度/饱和度/中灰/峰值（鼠标922、越界5000拒绝）；重启保持 | tier23-v3 seed/restart |
| 7 | **通过**：自绘确认框，取消不切换、确定进入节点页；文案更新（confirm.png） | tier23-v3 |
| 8 | **通过**：右键空白添加（新节点落在点击处）、左键拖空白不平移、中键拖动平移、左键空白取消选中、适配视图/自动排列、输出锚点右移不重叠（node.png） | tier23-v3 |
| 2 | **通过**：两列×4行，网格底121px ≤ 卡片172px（pro-quality.png） | tier23-v3 |
| 6 | **通过**：全部10个滚动条为VScrollBar样式，管理预设长列表可滚动（manage.png） | scroll-v1 |

回归：单测、QML组件21 passed、data/easing、S2合同、导出门禁、右键边界、深复制、复位、实播重启全过；逐节点计时实播复测通过（徽标正常、生成帧呈现100、untracked=0）。最终候选`E:/项目/Veyra/tests/p3-node-backend-20260927/candidate-a30-v2`（EXE SHA256 `163eb9ea9feed1172b9daf3e8f39c5cea120880ef6fbdc759a93da0c20f82ae6`），存档`archives/p3-fixbatch-20260928-a30/fixbatch-slice`（251文件+1398候选manifest）。

测试过程记录：①Space首轮用QtTest keyClick因窗口非激活不触发快捷键，改外部真实按键通过；②外部助手ALT解锁前台会让无边框窗口进入键盘菜单模式，随后真实鼠标延迟约2秒送达（诊断3轮），已改为仅在非前台时使用ALT；③画布真实鼠标一次落到用户正在手测的另一实例窗口（pid 61652）上，为避免干扰用户，画布改用Qt合成事件（走同一事件分发/按钮过滤），窗口移动/缩放仍为真实OS输入。存档receipt把CineBar/FullscreenBar列为“相对a29存档新增”，仅因a29存档未含这两个路径，哈希与a29候选一致，未修改。已知未改：旧s1-ui-check夹具仍调用Dialog.standardButton，不在回归集。

## 追加 a31（用户第二轮）
补帧开关、DLSS 6X、XeSS“无效”、Alt 处理见 WORKLOG a31 节；候选 `candidate-a31-v2`，存档 `archives/p3-fgalt-20260928-a31/fgalt-slice`。

## 追加 a32（节点编辑器，P3）
见 WORKLOG a32 节；候选 `candidate-a32`，存档 `archives/p3-nodeedit-20260928-a32/nodeedit-slice`。
