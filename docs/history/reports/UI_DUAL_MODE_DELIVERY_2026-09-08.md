# Veyra 双模式原生界面交付记录

日期：2026-09-08。用户授权先建立本地Git存档、完成已批准UI0–UI9方案，并在完成后关机。基线存档：`ddc515d`。本次不使用用户拒绝的设计技能，不修改专有运行时，不占用采集视频流，不发布或上传项目。

状态：UI0–UI9限定本机软件施工与短测完成，用户视觉/实卡验收待进行。受保护旧delivery gate仍因23帧断言失败，未改文件或冒充PASS。旧Phase 0–7字段保留历史含义，不用UI改版宣布公开首发完成。

## 1. 实际界面与交互

- 默认日常模式：深蓝渐变背景、单一影院画面、橙色细进度条、居中圆形播放键和矢量音量/字幕/全屏图标。提供视频、采集配置、最近文件、总增强和预设选择。
- 专业模式：黑色外壳、窄图标栏、圆角视频与参数卡片、底部播放/尺寸信息卡、独立NR阶段耗时仪表。增强、补帧、预设、导出集中在右侧。
- 模式切换保持同一个video HWND和source session，不重新打开媒体、不加载另一套增强预设。专业比较状态单独记住；返回日常恢复正常增强显示。
- 800×600窗口可展开参数栏，展开时预览收窄，参数与画面各有独立区域。右栏可滚动，期望/已应用版本和错误区固定。窗口控件使用原生编辑/选择语义和统一深色绘制。
- 全屏支持F11、Alt+Enter、视频双击、按钮及Esc恢复。编辑框内V、空格保持输入；专业V比较不会占用输入框。
- 总开关真实旁路NR/SR/补帧，恢复时使用配置快照；失败按来源会话与被拒绝的确切事务恢复UI，不把任意新版本号误判为失败。
- 独立字幕层在FG后的桌面合成阶段显示，中文和英文两行透明字幕已在实际窗口验证。应用内音量通过真实PCM增益渐变生效。

本次复现参考图的配色、空间关系、卡片形态、控制条与图标语言。它是实际播放器界面；未把参考图贴进程序冒充实现，也不宣称无人机仪表、影视素材和每个像素均与参考图相同。

## 2. 已补齐的工程功能

| 区域 | 实现与边界 |
|---|---|
| UI组织 | `main.cpp`只保留入口，壳、布局、主题、采集配置、字幕、偏好各自独立；继续使用Win32/D3D12共享引擎 |
| 参数 | 7项模型与5项变化量参数、严格值域/整数验证、120ms滑杆去抖、整套应用及回滚；隐藏/换页取消未发出的提交 |
| 偏好 | UI偏好独立版本化文件；确认成功的增强快照另存；坏文件保留；正常重启始终日常模式 |
| 采集配置 | 设备与格式查询在独立COM工作任务中进行，连接需要明确选择；本次只看配置/枚举，未开始采集 |
| 音量 | 文件WASAPI浮点PCM约5ms增益渐变；采集使用自身DirectShow图音量能力，不修改系统总音量 |
| 后台导出 | 同EXE无界面worker、匿名映射冻结完整设置、受限继承句柄列表、Job Object随父进程关闭清理；继续调用共享VideoExportJob |
| 导出控制 | 暂停/继续、取消、观看优先、封装/验证状态；源帧/生成/Hold/编码实数计数和作业ID/冻结版本；取消保留partial |
| 进度 | 使用源PTS减视频起始PTS；编码阶段最多99%，IPC最多99.9%，确认子进程成功退出才给100% |
| 诊断 | 在专业区域显示实际GPU/CPU/帧身份和失败信息；先脱敏预览再复制，无自动上传 |

观看优先只在完整导出源帧之间让出时间，不丢源帧、不暗中降低导出质量。两个作业仍共享物理GPU，不保证任意4K/4X组合同时实时。NR仪表显示单阶段GPU测量，不是整图耗时或显示延迟。

## 3. 文件与构建

主要改动：

- `apps/veyra/main.cpp`、`apps/veyra/ui/*`、`SettingsWindow.*`、`TelemetryWindow.*`、`veyra.manifest`。
- `include/veyra/engine/*`和`src/engine/*`中的EngineController、ExportJobManager、VideoExportJob。
- `FFmpegVideoDecoder.*`、`MediaFileSource.cpp`、`EnhanceGraph.cpp`：有上限的文件解码并行和上传拷贝优化。
- `include/veyra/sink/AudioGain.h`、`WasapiAudioSink.*`、`CaptureCardSource.*`。
- `CMakeLists.txt`、`tests/unit/UiContractTests.cpp`、`tests/integration/MediaFileSourceTests.cpp`、`scripts/acceptance/ui-dual-mode.ps1`及`ui-4k-regression.ps1`。
- 本报告、使用指南、交付状态、WORKLOG和loop恢复记录。

构建命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
```

最终应用构建日志：`logs/ui-dual-mode/build-upload-direct.log`，exit0；随后仅增加解码集成检查，`build-threaded-test.log`构建exit0、约2.48秒，未改变应用二进制。EXE SHA256：`2DE33230CD3A6556BC8E1399DD93BB8959B8EF37B2BACEF23D1486590AA4D3C6`。入口仍为项目根目录`Veyra.cmd`，不是安装包，不应移动项目路径。

透明子窗口需要现代Windows兼容性声明；已补入manifest，并对PARGB画布直接绘制，记录UpdateLayeredWindow结果。技术依据：[Microsoft Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)、[应用兼容性manifest](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/application--executable--manifest)。

## 4. 实测与证据

每次测试调用最多300秒，构建单独计时；历史累计保留而不作为后续调用剩余额度。所有视频来自项目合成素材或由它生成的测试音视频；没有使用用户采集内容。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-dual-mode.ps1 -Root . -MaxRuntimeSeconds 300 -Case all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/repair-v2.ps1 -Root . -MaxRuntimeSeconds 300 -Case all
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root .
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/ui-4k-regression.ps1 -Root . -MaxRuntimeSeconds 300
```

| 调用 | 真实结果 |
|---|---|
| UI全套 `de337338b71e43618c4beaa488d54afb` | PASS，15子检查exit0，109.583秒；EXE为上列2DE332… |
| Repair v2 `joint-91436d0b0f4145c2b6fe6794ff146c68` | 18项全部exit0；子进程计时合计70.712秒（不是整个脚本墙钟）；整次调用受300秒限制 |
| 受保护delivery `39c5c9d100234c92beaf7eb5d5cab1c0` | **FAIL**，34.736秒；14检查先通过，旧23帧断言失败，后续HEVC/cancel未在该gate执行 |
| 当前4K合同补测 `4k-f764b1a5ef894f46b050cbbf728103ec` | PASS，23检查，14.178秒；两codec原生4K CFR、AAC、实际解码、取消，以及4K B帧多线程输入的EOF/seek均通过；不替代受保护gate |

模式切换40次（20往返）：P95 **4.200ms**，最大13.591ms；区间446次提交、唯一epoch1、最大提交间隔20.3844ms，无Feature创建/reset/source open。GDI13→13、handles718→718，播放切换期间private bytes不增长。暂停中40次切换保持PTS和源帧计数。CPU命令耗时和主机提交间隔不是物理显示延迟。

后台导出：冻结revision3/intensity1，前台后续成功应用0.65，实际worker仍使用冻结值；前台累计540帧，导出source120/generated0/hold0/encoded120。导出暂停时前台位置0.333→2.967秒且session1不变。ffmpeg全流解码和ffprobe确认H264/AAC。取消/父进程退出均已有活动worker和源帧，未留下孤儿PID或伪成功文件，partial保留。

当前RTX/NVIDIA runtime真实执行：`player.stdout.log`记录Feature18 Create `0x1 / Success / handle=non-null / seh=0`，DLSSG Create `3840×2160`同样成功；最终NR248、NVOF247、有效generated247，FG Evaluate返回`0x1`。4K输入/底图/光流/FG/输出保持3840×2160，内部NR1920×1080，源处理59.55fps、绝对落后P95 1.77ms。1080诊断60次NR、59次NVOF、2294个非零motion、D3D诊断0错，正常路径回读0；native4K12次NR只证明短片正确运行，不证明4K NR实时或高质量motion（该4K样本nonZeroMotionCount为0）。诊断输出PNG实际查看非黑。

历史Repair计时账本累计487.669秒，失败与此前运行均保留；并未清零来绕过限制。该累计不是本轮单次测试上限。

4K输入检查使用24帧真实B帧H264/HEVC合成素材：首遍读取和EOS后seek0重读，共两遍；每帧有效YUV像素的Adler32和PTS与1线程基线均匹配。脚本明确断言实际4线程日志与9项检查零失败，也断言导出图全部尺寸为原生4K、source/generated/hold/output分项计数及AAC编码，避免只靠进程exit0放行。

UI套件覆盖默认状态/384组合的纯布局数据与四种DPI比例、真实PCM增益、偏好与坏文件保护；播放和暂停各40次模式切换；真实增强关闭/恢复及故障注入后的UI恢复；音量；边看边导出/暂停/继续/冻结设置；取消及活跃worker退出清理。纯数据DPI检查不等于多屏物理缩放验收。

成功导出会同时核对保存的完整冻结快照和真正worker日志中的revision/intensity，并检查ffmpeg解码、ffprobe音视频流。取消和父进程关闭检查明确活动标记、无孤儿PID、无成功文件、已有partial保留。

## 5. 实际窗口截图

目录：`logs/ui-dual-mode/visual-candidate/`。

| 文件 | 实际观察 |
|---|---|
| `daily-current.jpg` | 当前2DE332… EXE的日常影院式界面和透明双语字幕 |
| `professional-current.jpg` | 当前2DE332… EXE的黑色四区工作台；结束后保留画面及最近处理样本 |
| `export.png` | 专业导出页、输出选择和任务控制 |
| `diagnostics.png` | 指标/脱敏区域覆盖正确，无底层播放器控件穿透 |
| `capture-setup.png` | 实际采集配置窗口；未选择格式、未连接设备 |
| `empty.png` | 空白首页提示可见，已去掉灰色占位覆盖 |
| `fullscreen.png` | 实际2560×1440窗口画面；Esc恢复原窗口 |
| `small.png` / `small-drawer.png` | 800×600窗口和修复后的参数栏布局 |
| `keyboard.png` | 编辑框内实际V/空格输入，未触发播放或比较 |

其余状态截图来自本轮较早视觉候选，用于记录相同主视觉；最终EXE的日常/专业复查另有上述明确文件。`subtitles.png`及`professional-final.png`是上一候选DBAD5B…的字幕检查，并非当前二进制截图。截图未经合成或美化。早期失败的`small-drawer-before-fix.png`仅是缺陷记录，不是交付截图。

## 6. 本轮暴露并修正的问题

1. 最初的普通Win32控件排布不符合用户参考。重新实现了整体壳、影院控制条、矢量图标、圆角卡片、原生控件主题和仪表。
2. 延迟布局重复登记同一HWND，造成按钮缺失；修正布局清单。
3. GDI+依赖头文件顺序导致一次编译失败；补COM头文件。一次链接失败因为自己的视觉测试EXE仍运行，等待测试退出后再构建。
4. 增强开关失败后UI仍显示新状态；增加事务归属与确切拒绝版本，修正合法后续调参/换源的竞态。
5. 隐藏页120ms定时器仍可能提交；隐藏/换页取消定时器。空闲停止状态、活跃导出退出提示、导出终态文案一起修正。
6. 诊断面板被视频/控件覆盖；修正同级裁剪与z序。空白首页提示也恢复到视频上方。
7. 模式耗时P95约23ms超出16ms目标。分段计时定位到EndDeferWindowPos触发隐藏面板重复布局；隐藏页改为保留尺寸，批量失效交由绘制消息处理。修复单项实测P95 3.754ms、最大14.208ms；446次提交最大间隔20.1665ms，epoch不变，无Create/reset/source open。
8. 800×600参数抽屉与视频/仪表重叠；改为独立列，三组尺寸文字自适应列宽，真实窗口复查无重叠。
9. 字幕初次实际窗口检查不可见；补compatibility manifest、显式PARGB绘制与隐藏字幕几何初始化。最终日志`subtitle-run.log`记录成功1112×108，截图显示透明双行文字。
10. 非零起始PTS可能提前显示100%；按相对视频时间计算并限制成功前进度。
11. 真4K底图/光流/FG/输出的实时档初测45.95fps、落后P95约895ms。历史通过日志的光流/FG/输出实际1080，不能直接比较；4K分辨率修复在本轮基线ddc515d中已存在。本次只对高分辨率文件解码使用最多4线程，直接将8位YUV转换写入已受fence保护的上传slot，并缩短有上限的FG就绪轮询间隔。最终59.55源fps、落后P95 1.77ms；保持4K输出及1080内部NR，不是原生4K NR实时承诺。
12. 受保护delivery脚本随后在旧的23帧断言停止。本轮基线已使用12 source + 11 generated + 1 tail hold = 24帧，120fps/0.2秒才保持原时长。该gate保持FAIL，文件hash未改；单独补测当前CFR合同，两codec真实解码/音轨/取消通过，不能用补测宣布旧Phase门禁通过。

没有删除失败日志，也没有放宽门槛让失败通过。

## 7. 独立复核与未测范围

独立新上下文只读Reviewer `review_dual_mode`最终结论：**UI0–UI9限定的软件实现与短测范围通过复核，未发现尚未关闭的代码阻断。** 已核对代码、实际窗口截图、同一EXE的UI15项/Repair18项/4K23项证据，前次防漏断言已关闭。Reviewer未写文件或启动测试/GPU/采集；不是独立重跑证明。完整归档见[独立复核记录](REVIEW_UI_DUAL_MODE_2026-09-08.md)。

七个受保护文件与R0基线hash一致，根目录/暂存NR和addon身份一致，用户删除的测试视频仍不存在，运行时/SDK/日志与图像仍被Git忽略。索引：`logs/ui-dual-mode/evidence-index.json`；保护核对：`logs/ui-dual-mode/protected-final.json`。

当前Phase仍为历史Phase7本机候选，UI0–UI9单独记状态。已有NVOF+confidence路线、DLSSNR实验身份及NR实时档边界不变；Depth provider、原生游戏输入等未实现能力不会因为UI更新被标成完成。

尚未验收：本版本实卡视频/声音/断线恢复、多屏物理DPI穿越、实际扫描/光子延迟、长时间稳定，以及所有4K SR+4X组合的实时性能。采集设备驱动若长时间卡住查询，底层驱动兼容性仍需实卡判断。本次不把无设备或仅枚举算采集端到端通过。

下一条唯一用户验收任务：用`Veyra.cmd`启动新界面，在实际采集卡上验收日常/专业切换、声音和体感延迟。专有runtime分发权仍未解决，仅本机研发交付。
