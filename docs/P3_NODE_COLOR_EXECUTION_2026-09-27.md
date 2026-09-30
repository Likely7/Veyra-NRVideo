# P3 节点与调色执行记录 — 2026-09-27

> 续接：用户已明确同意另开后端分支补齐节点执行、独立配置与恢复。Goal已恢复active，施工转`codex/p3-node-backend-20260927`，见`P3_NODE_BACKEND_EXECUTION_2026-09-27.md`。下方blocked与冻结为原UI切片的历史结论，不代表新授权下继续阻塞；原候选与证据保留。

## 状态与范围

Goal blocked，P3未完成。2026-09-27同一接口/范围冲突连续三轮核实后，已调用update_goal并确认返回blocked，停止自动续跑，不标complete。调色接线与真实控件通过本机定向验收；节点仅处理既有共享列表链的UI缺陷。用户最新UI-only冻结规则优先，后端接口缺口明确停工，不自动扩范围。恢复完整P3须用户明确解决授权冲突，按现行要求另开后端分支和验收；旧active字样仅代表历史状态。

工作区：`E:/项目/Veyra/worktrees/p0-p1-r53-20260927`；分支沿用 `codex/p2-nr-20260927`；开工HEAD `53a2d31c6b34f74b7b6d2953ee2caaec63d44e1d`。

P3-a基线：`E:/项目/Veyra/archives/p3-node-color-20260927/scope-baseline.json`；SHA256 `e78c75f6716a7fc59f8a7610b4e11468ec2820bff4dbf631e185f074514b5c9a`；写前快照同目录 `before/`。18条白名单仅UI/bridge/直接测试与文档。其余文件冻结，尤其engine/pipeline/shader/播放、采集、解码、音频、Present。后端接口缺口登记，不能悄悄扩白名单或改旧guard。

## 分阶段计划

| 切片 | 工作 | 验收 | 状态 |
|---|---|---|---|
| A | 调色值/总开关写回真实Color实例；范围/finite/默认值；曲线、8色混色器、4区色轮复用 | 构建，生产控件鼠标操作，真实bridge读回及实际输出对照 | 本机定向通过，非完整P4-g |
| B | 节点入口可达、弹窗视频避让、确认/取消、参数实例选择与模式隔离 | 有/无视频操作，失败保留状态，保存/重启不串改 | 共享链入口/取消/返回/实例定位定向通过；独立模式及其保存恢复缺接口，未完成 |
| C | 核对R5.4固定顺序兼容，再合法单链真实有序执行 | 记录接口缺口与授权边界；代表链纹理/顺序/reset/性能证据 | 接口缺口，当前冻结，停止此路径 |
| D | R5.5编辑/运行/恢复，旁置不分配资源，布局不重建；收口文档 | 200固定种子建图属性、50次模式切换及代表链实测（非全矩阵重跑） | 依赖C及独立状态接口，未完成 |

## 已发现问题（静态证据，不等同运行验收）

1. `QmlPlayerBridge::Impl::commit`先fromChain，调色setter只改flat settings，旧节点payload覆盖刚改值及enabled。修复必须写到真实节点，不另造第二份状态。
2. ProPage调色页目前只有标量Repeater且明确写“曲线编辑器、混色器、颜色分级轮…尚未接入”。应实现真实点曲线、8色HSL/B&W以及4区色轮，不能改标签冒充。
3. resetColourParameter统一写0错误：gradingBlending默认50，lutStrength默认100；输入必须拒绝NaN/越界而非污染settings。
4. arbitrary node order仍未执行；节点页面可见不代表其画布实际生效。

## 执行纪律与证据

### 本轮实现与实际证据

- `QmlPlayerBridge`选中Color实例事务写回、失败恢复；曲线预览用现有`ColorGradeTables::curveValue`129点采样，未另造插值；正确保留split参数属于曲线bit2的合同。新增四个生产QML组件并复用进ProPage。重置/载入预设清除旧实例选择，增删/移动维护索引。
- 构建001/002/006均exit0、post_guard pass；006为选中实例恢复修复后的EXE。Qt测试005为14 passed / 0 failed / 0 skipped，报告`E:/项目/Veyra/tests/p3-node-color-20260927/quick-a2.txt`。
- `009-real-colour`：真实播放器总开关/曝光/曲线鼠标加点/八色混色器/四区色轮写回，NaN/Inf/范围/索引/曲线排序拒绝不污染状态；默认值重置；保存、清除、同进程恢复、第二进程恢复通过。`real-a3/result.json`两进程exit0。
- `010-color-pixels`：22.655秒，合成SDR灰阶/色带图，由OS截取原生D3D窗口而非Qt grabToImage。相对中性基准的平均绝对RGB差（0..255）：曝光59.7262、曲线65.2859、蓝色混色器7.3546、全局色轮29.7341；关闭总开关差0.0。原始图、截图、坐标和结果在`pixels-a1/`，已目视核对neutral/mixer确为色带画面。仅本机RTX5070/固定SDR图，不代表实屏HDR精度、所有显卡或完整导出。
- 节点UI静态修复：确认弹窗视频避让；去掉“两套独立配置已实现”的错误承诺；`nrEnhance`错误类型改为实际`nr`；三项快捷参数绑定明确实例索引，不写全局NR；完整参数跳转同一列表编辑器；初始零坐标展开、拖动累计位移修正。该视图与列表共享链，任意拓扑/独立状态未实现。

### 最终节点与候选验收

- 节点013最终复测（`node-a3/result.json`）有源/无源两进程均exit0，31.227秒，post_guard pass。通过真实鼠标确认/取消进入节点页、取消保留整条链、修改第一NR快捷强度不串第二层、NR/Color完整参数返回正确列表面板。播放用例实际日志显示弹窗期间`SetWindowRgn covers=2 ... result=1 error=0`，关闭后恢复covers=1。`node-a3/video/nodes.png`已目视核对两层独立卡片，第一层0.22、第二层1.0；本用例不代表任意拓扑或NR渲染资源隔离验收。
- 节点012断言通过，但`nodes.png`异步截图抓到了跳转后的列表页，不能作节点布局证据。测试改为等待截图成功保存后才导航，013通过且截图正确；保留012原证据。Qt截图只用于UI，原生视频区域黑色不表示实际画面失败，也不作为像素证明。
- 最终014控件测试exit0：`quick-final.txt`为14 passed / 0 failed / 0 skipped，1.917秒。`staging-final.json`核验源码、build、candidate各45个模块文件一致；补同步build中3份旧QML，没有改产品源码或CMake。候选Main无测试注入，EXE和控件测试EXE与006构建一致。
- 当前候选`E:/项目/Veyra/tests/p3-node-color-20260927/candidate-a1/veyra_qml_ui.exe`，SHA256 `c9b4ceafe5e8950a3ef27aad850a0aeb76a6c1ebdb1237cd01657d380ee1f5db`。它是本轮验证候选，不替换P2手测包、桌面原checkout或用户配置。弹窗仍有原生浅色标题/按钮条，未宣称设计稿1:1或完整动效验收。


- 收口原区scope guard通过（474个冻结文件，failures=[]）；独立P3 guard通过（原区1457、隔离1055文件，18条白名单），`git diff --check`通过。证据为本轮logs下`canonical-scope-guard-closeout.json`及`isolated-scope-guard-closeout.json`。只证明本轮未新增越界修改，不能推论此前混入的底层改动全部正确。

### 失败与修正，不能省略

- 大补丁transport封装和换行转义失败，未写入目标；随后拆为单层小补丁成功，不将失败请求计入实际实现。
- 首轮QuickTest使用不存在的`mouseDoubleClick`；查本机QtTest后改`mouseDoubleClickSequence`，保留旧失败报告。复制测试到不存在的build/qml-tests目录曾失败；stage实际从源码测试目录复制，未篡改CMake/stage掩盖问题。
- 真实应用测试首两次失败在测试侧查找：Window的静态QObject树和Repeater视觉树不同；分别补QtTest.findChild与root.contentItem递归后009通过。没有删除产品断言。
- 节点空会话首测把无播放源的pending设置当作需要等待出帧应用而超时；空会话改为验证pending/界面，播放用例仍等待applying结束，012/013复测通过。没有修改引擎掩盖pending。

### 已确认且不得自动绕过的接口缺口

1. `EffectChain::toChain`把设置重建为List固定顺序，`fromChain`仅投影到已有字段，不是任意拓扑执行入口。
2. `setNodeMode`只改mode，Main此前只换page；独立列表/节点会话及持久化恢复未接通。
3. 因用户当前冻结engine/pipeline/source/shader及R5.4/R5.5，后续执行器、旁置节点资源合同、200链属性/50切换压力验收不在本切片可执行范围。记录后停止该路径；不得重建baseline洗白、反复刷同样测试或标Goal complete。
4. LUT导入/文件选择、撤销重做/复制粘贴/按住看原图及完整设计动效属于剩余P4-g/P5，当前保留已有LUT引用与参数，不填假数据。

- 保全P2最终候选、旧基线与所有既有dirty变更；原checkout/main/1.4.4/用户配置/设计稿/运行库不动。防闪与1080p240继续暂缓。
- 不派子Agent，不commit/tag/merge/push/Release，不删除旧UI。测试≤300秒、构建≤900秒；同因最多两次有依据修复；20分钟无新证据停止该路径。
- 测试、日志、临时文件分别写入 `E:/项目/Veyra/{tests,logs,tmp}/p3-node-color-20260927/`。复用bounded runner时其TEMP位于已合规的 `E:/项目/Veyra/tmp/p0-p1-r53-20260927/`。
- 构建后的QML必须明确同步并逐文件验SHA；不能假设CMake POST_BUILD会因仅QML修改触发。
- 2026-09-27开工guard通过：原checkout1457、隔离1055文件；无产品源码写入前已留写前快照。
