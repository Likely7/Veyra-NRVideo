# R0：最终候选与正常功能回归（2026-10-05）

范围为性能分支的当前可执行工作，不合并、推送或发布。依据最新用户指令，不做GPU竞争、显存压力、TDR或设备移除；保留已有压力记录但不重跑。测试使用本机RTX5070/616.56，不能外推AMD/Intel、其他RTX、真实采集卡、主机或物理屏幕延迟。

节点账本已经有证据与处置：5a检测/填黑拒绝；1a/1b/1c、2b、SR缓存扩展、4a、NVOF预取候选拒绝/明确回退；2a/2c单NR/2d/3a普通默认/限定3c/5b NVENC/5c保留待最终回归；4b是既有可选配置说明；3b可选默认不选，已完成真实UI/原生/完整导出对照，画质未获主观批准。

本次收尾按以下顺序记录：

1. 当前产品与合同/Xbox/Qt Quick/easing/i18n/qml-data/效果链/preset/音频倍速/字幕/导出worker重新构建；每个测试最多300秒，scratch/TEMP全在E盘本轮目录。
2. 当前产品实际Qt启动各页、功能、布局、能力灰态、软件兼容模式，真实导出队列/lifecycle/取消/MP4/MKV。OBS游戏采集与实际RTSS只在可执行且不改变用户常驻配置的条件下验；未测不能称通过。
3. 普通播放收尾矩阵，沿用固定M1、S1/S2/S3/S4/S5配置及同驱动/运行库，三轮保存全数据；封存A和最终B的实际身份分别报告。Auto及先粗后细画质变化单独报告，不混入默认提速。原生全帧与导出一致性已覆盖默认off和Auto边界，不以性能数值代替画面检查。
4. 保留10秒附近NR CPU调用长尾、既有约2.8秒提交间隙等未解决现象，比较最终正常运行是否有新增回归；没有可靠根因不把局部降低GPU耗时称为软件丝滑或后台掉帧根治。
5. 翻译/源码边界/运行库原字节/工作树保护/最终源码存档、可运行候选路径及完成账本。

尚未运行的检查不能提前标通过。可运行测试失败先保留证据并修复；不可通过的软件候选要回退。需要用户真实来源或主观画质的缺口单独列明。

## 当前产品及已执行的功能回归

产品源码仍是 `e8f0bd1082293c88a3ca1db851667162e4cef7a9`，其后的提交只补文档/测试；当前 `B2d/veyra_qml_ui.exe` SHA256 `8c2276d68d15c2e4466befcf367439b7efc42c68defd5ef93182398c9e8d8a03`。实际NR原件 SHA256 `f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc`，Lecram 310.8.3.0 / HashMismatch；不改字节、不冒充官方签名。Qt/FFmpeg/其他增强组件来自封存完整NVIDIA依赖。所有测试独立目录，末尾校验EXE/DLL/QML未改，未写入用户profile。

| 检查 | 实际结果与证据 |
|---|---|
| 当前构建 | `build-r0-contracts-v1`44步成功；增加Auto ID6后的效果链测试v2两步、Qt Quick夹具v3/v4重建成功。产品EXE没有因此重写引擎 |
| 定向合同15项 | `R0-contracts-v2`其中14项通过：repair、Xbox、1008个链组合/原生导出/图片边界、preset Auto往返、availability、UI合同、VFG设置、Auto控制器、三语i18n、qml-data、easing、音频倍速、字幕文本/合成。Qt Quick在`R0-contracts-v5`独立通过39项、0失败/0跳过。Xbox是软件合同测试，未新增主机实测 |
| 实际Qt功能/布局 | `R0-product-ui-v1-summary`GPU及`--obs-game-capture`软件绘制各功能/布局，共4/4通过。真实内嵌字幕逗号/三行、倍速1.3/1.8/0.25/4/1媒体时间、非法值/取消、全屏菜单遮挡、AMD能力灰态；720/1280/1600宽度专业模式布局、全屏菜单保持/锁定隐藏/退出恢复及最大化恢复。无ERROR/FATAL/ReferenceError/TypeError |
| 严格页面启动 | `nr-r0-smoke.py B2d R0-qml-smoke-v4`实际home/minimal/pro/node/export/settings/capture-dialog/playback共8项通过，0跳过；3500ms每项，无测试Loader。记录`logs/perf-nr-20261004/R0-qml-smoke-v4/run-20261004T214158154Z-77eec4cd/result.json` |
| 实际RTSS | `R0-rtss-ordinary-v1`本机安装原件，低/大OSD两套profile各普通播放/单NR参数/resize-pause-seek三项，共6通过。实际hook、共享内存OSD、原生目标窗口与视频/UI截图，背景alpha=255，无重启循环；cleanup逐字节恢复全部用户Profiles并退出本轮helper。没有额外GPU竞争/高倍率/4K压力 |
| 当前导出worker | `nr-export-worker.py B2d R0-export-worker-v1`真实lifecycle、取消边界、队列、HEVC MP4/MKV共5/5通过；队列四个完整输出文件SHA相同。工作线程EXE SHA256 `ced1ca191f975a0bb661c2889774aa9386bb911c3317e36afff9cb62a61d46fc`。原生Auto导出240全解码帧另在3b证据中一致 |

上述原始证据根目录都是`E:/项目/Veyra/logs/perf-nr-20261004/`，逐案例保存命令/实际EXE/运行日志/结果，不合并失败原记录。普通播放33组已完成；OBS实际游戏采集尚未运行，不能把软件绘制启动当作OBS已抓到视频。

## 回归夹具失败与处理

- `R0-contracts-v1`效果链硬编码864组合在增加第七策略后已过时，真实组合数1008；只修期望数量。VFG测试缺scratch参数、easing缺offscreen plugin，补齐隔离目录/Qt测试依赖；只停止挂住的本轮easing PID44288，未动其他进程。
- Qt Quick原断言只允许6档；保留旧六档编号及标签，追加Auto ID6/灰态检查。注册的inline NR卡使用引擎创建context，显式注入UI-only snapshot，真实bridge覆盖仍由实际Qt进程承担。补齐snapshot.logUi后菜单关闭正常；四卡点击前等500ms accordion动画完成，v5通过。初版失败v1–v4全保留。Quick测试仍有既有独立ExportPage/PlaybackRate夹具初始undefined、focus警告；未改产品去迁就夹具，实际产品回归无对应错误。
- `R0-qml-smoke-v1`子Windows PowerShell继承了桌面bundled PowerShell的PSModulePath而找不到Get-FileHash；wrapper显式导入该子进程自己的Utility/Management模块。v2引用的参数名被当位置参数、v3缺要求的qml-tests/QtTest/offscreen依赖；按现存readonly入口合同补齐独立stage，v4真正执行8项通过。未修改验收脚本、全局环境或降低必测项。

## 普通播放收尾：33/33通过

`nr-r0-normal.py B2d R0-normal-v1`串行执行同源M1、固定1280×800、实际GPU进程普通档，各S1/S2-720/S3/S4/S5-existing封存A/当前B各3轮；S4另测同B关闭3c队列分工各3轮，共33个普通50秒进程，全部通过且sourceSkipped=0。无竞争程序；Auto默认off。最后NR的滚动P95观察中位与全增强合并区间分别报告，不冒充全帧总体P95或物理显示延迟。

| 配置 | A最后NR / B最后NR ms | A增强 / B增强 ms | A / B成功软件Present P99 ms |
|---|---|---|---|
| S1 单层原生1080 | 6.129 / 6.159 | 7.002 / 7.086 | 34.044 / 33.999 |
| S2 单层720（尺寸66.7%） | 3.670 / 3.669 | 4.273 / 4.274 | 34.066 / 34.032 |
| S3 单NR1080上限+SR4K | 6.221 / 6.189 | 10.310 / 10.237 | 34.054 / 34.046 |
| S4 双NR1080上限+SR4K+DLSS2X | 6.431 / 5.989 | 19.481 / 18.795 | 17.236 / 17.233 |
| S5 既有480/720/1080三层 | 6.272 / 6.289 | 12.796 / 12.817 | 34.038 / 34.017 |

S4完整候选对基线增强区间下降3.52%；同一B的DIRECT增强19.299ms，队列分工对其下降2.61%。其余微小差异没有稳定收益，不能冒充NR模型提速。成功Present P99几乎没变；全矩阵最长110.209ms，图提交仍约109.793ms。33组未复现此前2.8秒断档不等于根治；所有运行稳态UI active=false，不能替代点击桌面前后完整焦点对照。封存日志`R0-normal-v1-summary/{artifacts.json,completed.json,comparison.json,per-run.csv}`及逐组原始证据。

## 退出重建：阻塞交付的真实崩溃

`nr-r0-close.py B2d R0-close-rebuild-v1`普通双NR+SR4K+DLSS2X播放，请求FSR3.1，200ms后Qt.quit，第一例访问违规退出3221225477（0xC0000005）。Qt确已退出，但后台继续NGX创建，向已销毁HWND创建交换链得到0x80070005，随后清理时崩溃。`nr-r0-close.py A R0-close-baseline-v1 fsr3`封存基线也同样崩溃，证明是既有退出边界缺陷，不能作为优化收益或新增回归隐瞒。两次player/console/result/minidump原件在同名日志目录保留。

源码根因：PresentSink::shutdown未清空非拥有queue_；VideoPresenter随后释放该队列。下次initialize在交换链成功后才赋新queue_，中途失败导致shutdown向悬空旧queue_调用Signal。EngineController初始化及失败回滚缺停止检查，退出仍尝试向失效窗口建图。先封存当前回归工具/数据说明，再最小修复PresentSink清理及初始化失败、EngineController长调用返回后的取消检查；不修改NR算法、运行库或稳态调度。候选此时不可交付；所有退出场景和真实OBS检查尚待修复后验证。

## OBS（尚待执行）

OBS按官方32.1.2源码的portable配置及obs-websocket RPC1另写隔离驱动；只复制只读安装payload、临时认证服务，只选择本轮唯一标题的Veyra窗口，禁用桌面/麦克风输入和网络推流。仅待普通计时结束后执行正常录制、暂停恢复/缩放/全屏功能；未执行前无通过结论。
