# 补帧＋源时间戳闪黑：当前证据与现场诊断

**整幅黑闪已由录像确认；根因尚未锁定，未提交产品修复。** 不应由60→30有效推断PTS错误，也不能由FPS或NGX/Present成功推断像素正常。

## 原始证据

`veyra-qml(41).log` 为42,664,677字节、141,315行，SHA256 `4714f8ee048f12965bd8ccd8c546191c5659cc7d40f40702d816faf670bd7d96`。手机录像 `ebdc538711ace23c8edc69cc11a57bfc.mp4` 为612,092字节、720×1280/30fps/146帧，SHA256 `9481aaddfe254ad46e8c8a53caf650b9702c4841f7af4846bff5771c09ad5ee6`。

录像第104帧（约3.467秒）和129帧（4.300秒）屏幕接近全黑，邻帧恢复；黑帧画面ROI平均码值分别约1.97/255、2.09/255，周围正常画面约89–92/255。不是局部运动残影。每次只占一张手机录像帧，不据此声称屏幕黑了33ms。视频创建时间06:24:11Z附近应用使用DLSS 2X/transport并有全屏切换，但手机和电脑时钟未校准，不能硬对齐两个事件。

现场日志：RTX5090/32GB、驱动617.42、Magewell Pro Capture HDMI4K+、3840×2160/P010/PQ/约59.94fps，LG TV SSCR2约143.988Hz。存在DLSS、VFG、XeSS切换，不能把全部时段归给VFG。光流常为AMD FidelityFX，NR常为两层1080p、NVIDIA原版。

## 日志和代码核对

- 18条ERROR为17次QML几何操作失败和1次配置保存失败；没有NGX/FG/Present/device-lost错误。错误和黑闪的关联没有被证实。
- 最后DLSS/transport稳定段约927秒、924个FPS样本，中位源60/提交120。55399个有效FG、invalid=0/providerDisabled=0；存在72次新增生成前跳过、11次过期等，不是“完全没有丢帧”。只有一次场景边界以及开段设置重建，没有反复reset证据。这些计数没有包含逐帧像素。
- 60→30保留真实PTS并减半源工作量、扩大每对帧处理窗口。它缓解故障不能单独证明源PTS有错。
- 核对有界作业、纹理奇偶复用、生成帧warmup/validity、producer/consumer fence、present slot和HDR生命周期；没有找到能够据现有日志定罪的资源竞争或“跳过生成帧就清黑”的路径。
- 正式2.0.6→当前main的产品差异集中在VFG预算；未改DLSS/XeSS graph、VideoPresenter或PresentSink。当前没有版本二分证据证明哪个改动引入黑闪。

## 实际本机验证

本机RTX5070/616.56；GUI日志显示本机显示器约320Hz，与现场5090/617.42/LG144Hz不同。原生诊断没有实卡输入，也没有HDR面板扫描验收。复用已构建`71723e0`产品库，只有外部VideoPresenter副本加入取样；确定非黑输入下，分布九点均非黑。样本统计不作为性能验收，诊断窗口、额外GPU拷贝、debug layer及过期策略会影响负载。

| 原生用例 | 原帧样本 | 生成帧样本 | 九点全黑 |
| --- | ---: | ---: | ---: |
| HDR4K、FG关 | 480 | 0 | 0 |
| HDR4K、DLSS2X、NVOF、源60 | 794 | 673 | 0 |
| HDR4K、DLSS2X、NVOF、隔帧30 | 294 | 293 | 0 |
| HDR4K、DLSS2X、AMD光流、静止60 | 647 | 441 | 0 |
| 同上＋两层Lecram NR1080 | 477 | 219 | 0 |
| 同上＋两层NVIDIA原版NR1080 | 628 | 242 | 0 |

六组均exit0、GPU错误0、运行组件前后SHA保持。早期两层用例用了枚举0（Lecram），没有冒充NVIDIA原版；修正为`NvidiaOriginal=3`后另做最后一组。初次缺少EXE旁shaders的初始化失败保留，不计通过。

真实GUI最终诊断EXE SHA256 `4c38d320585ff545dac231910fe7f64b71a2930af5e164f6c40fb32fce9804a1`：

- GTA6原片＋NR1080＋VFG4，最终EXE/PowerShell入口取得203张原帧、608张生成帧，两处取样全非黑，正常退出。
- DLSS2X/源60，已知黑场输入共480原帧/479生成帧，准确检测到13张黑原帧/12张黑生成帧，呈现输入与buffer一致，exit0、无ERROR。
- 最终Windows PowerShell5.1入口/已知黑场＋VFG4：449原帧/1345生成帧，检测到13/36张黑帧，exit0、forcedStop=false；原配置源文件SHA保持，自动证据ZIP CRC通过且不含profile。
- 首次PowerShell5.1入口取得像素但ExitCode为null，不记为退出码验收通过。保留原收据；保活Process.Handle后另做上述独立用例通过。QML测试注入第一次因CRLF匹配失败，尚未写文件/启动进程，保留失败记录后修正。

已知黑场是故意生成的测试素材，不是现场故障复现。最终取样器能检测黑图，仍不代表现场bug已修复。原生VFG/HDR全矩阵、XeSS内部生成帧、现场实卡/全屏/VRR/长稳未验。

## 现场诊断程序

交付 `E:/项目/Veyra/test-packages/fg-source-flash-20261008/Veyra-2.0.6-black-flash-diagnostic.zip`，只追加命名不同的诊断EXE和入口；包含完整对应基础源码及诊断补充源码/许可证。没有新增或替换DLL/模型，不是正式修复版。

1. 关闭现有播放器，将ZIP顶层文件放在原2.0.6 **NVIDIA**便携版根目录（与`veyra_qml_ui.exe`同级）。不覆盖原EXE。
2. 双击`Start-Flash-Diagnostic.cmd`。启动器将当前链/界面配置复制到独立profile，只修改副本。
3. 使用原采集源、同一补帧后端/倍率及“采用源时间戳”。在原来容易黑闪的静止场景复现15–20秒，看到黑闪后关闭诊断播放器。
4. 程序最多90秒自动退出；取样最多10000次提交，120fps约83秒，高倍率更早到上限。需要正常关闭才会保存取样；将`logs/flash-diagnostic-时间.zip`交回分析。

每次在同一GPU呈现命令中采样9个位置，分别来自最终增强原帧/生成帧纹理和Draw后的buffer。CPU只在收尾fence完成后读取一次；约88MiB有界readback，不是持续增长的图像存储。CSV保留码值、帧ID、PTS、wall time、epoch/revision、buffer index、fence及Present结果，不包含整幅抓帧。正常电影黑场同样会被检测，需要与相邻原帧及现场时点区分。XeSS在代理swapchain内部生成的帧不在此probe覆盖范围。

像素正常而屏幕黑闪只能把方向移到后续呈现/窗口/合成/驱动/显示，不能直接宣布电视或驱动有问题。取样会带来额外命令和屏障，现场阴性也不能彻底排除时序竞争。下一步唯一关键证据是**实际闪黑那次的双阶段像素及事件CSV**。

## 复现命令与保全

外部脚本目录 `E:/项目/Veyra/reports/fg-source-flash-20261008`：`analyze-log.py`、`segment-log.py`、`analyze-video.py`，输出log/segmented/video分析JSON、亮度CSV与检视图片；`build-probe.py`、`run-probe.py`构建/运行原生诊断；`build-field.py`重链接实际GUI；`run-field.py`隔离GUI用例。实际编译/链接argv与产品库SHA见`logs/.../build-receipt.json`和`field-build/receipt.json`，每组实际参数/EXE SHA/退出码见对应`receipt.json`，汇总`reports/.../test-summary.json`。

原生末组实际命令：`python reports/.../run-probe.py hdr-dlss-amd-static-original-nr2 dlss transport hdr 2 20 3840 2160 amd static`。GUI末组用`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/diagnostics/start-fg-flash-diagnostic.ps1`并明确指定E盘共享app、private配置源、独立输出、15秒及已知黑场源；完整参数在收据/WORKLOG。

Phone视频解码使用本机FFmpeg8.1.1作离线检视，未替换产品带PS5补丁的FFmpeg9.0.1。全部GPU用例串行、无竞争/压力程序；没有改驱动、用户配置或其它工作树。main仍dbfceab，产品源码无改动，不合并/推送/发布/关机。保全和新增占用以本轮`preservation-check.json`、`delivery.json`及`storage.json`最终收据为准。
