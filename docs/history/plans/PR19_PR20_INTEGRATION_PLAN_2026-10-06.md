# PR #19 / #20 审查、适配及合并

用户已测试2.0.4-fix1并确认9000系NR可用，随后要求检查PR19/20并做好适配。该反馈只覆盖用户实际测试设备，不代表全部RX9000型号。本轮从已交付现场修复 `63ce3964d9bca0baca70b619b294f6a0ef25e9d9` 开始，保留OBS软件UI重绘、Native多层NR导出及AMD合成器修复。

工作树 `E:/项目/Veyra/worktrees/pr19-pr20-20261006`，分支 `codex/pr19-pr20-20261006`。archives/build/tests/logs/tmp/verify/test-packages均为E:/项目/Veyra下同名任务目录。不可变start SHA256 `f3d49a940e731ef7412d83666525cc37f08464f7a48ad226b9f9a286e947c771`；已保存24个原工作树的HEAD/status/修改SHA及原字节ZIP、四个原发布ZIP身份、source-before.bundle和两个PR的原diff/提交/评论/评审元数据。bundle verify成功。原桌面、用户配置、其它工作树及运行库字节保留。

## 固定审查对象

| PR | 固定head | 内容与初始状态 |
| --- | --- | --- |
| [19](https://github.com/Likely7/Veyra-NRVideo/pull/19) | b14dc5a43dc008a1a8f4fa521d5a5a0fd41cc95c | 静态HDR输出曲线、HDR10 metadata、七预设、强度和显示峰值；有冲突，不能直接并入 |
| [20](https://github.com/Likely7/Veyra-NRVideo/pull/20) | 979ee3f46e64430120acc14bf2d5a61acd41a729 | MF编码器宽字串UTF-8日志和关闭RemotePlay时PIN接点；GitHub可合并 |

正确远端nrvideo=Likely7/Veyra-NRVideo，开工远端/本地main均为f8045fb53a7d800b053f0631a9b44dc9f5f1aacb。PR描述及作者测试是待核实输入，不能代替本轮证据。现有PR13/14继续暂缓。

## 实施和通过条件

1. 先审查两原diff及当前配置版本、呈现颜色契约与metadata所有者，再按原始head合并，保留作者历史。先PR20后PR19；冲突按当前产品语义逐项解决。
2. HDR曲线只在最终预览输出的线性nits域生效。默认恒等、SDR旁路、导出不继承显示器调优；检查参数非有限值、强度插值、峰值、单调性和root constant尺寸，不能破坏其它8常量图形pass。
3. 更新配置版本并支持此前真实版本。HDR参数/强度/显示峰值保存与恢复保持一致，不能丢NR强度5、三风格调控、独立抗闪烁、效果顺序及列表光流/节奏设置。源切换/reset/resize/SDR↔HDR不得沿用前一来源的metadata；检查swapchain重建和显示器变化。
4. 标准生产构建及RemotePlay关闭构建，定向HDR单元/真实GPU shader/预设和会话回归；正常UI及多层导出、AMD identity共享图短测，顺序执行GPU场景。不改Windows HDR全局开关，不把软件读数/RTX夹具冒充物理HDR或HIP实卡验证。
5. 对最终源码/EXE/证据冻结SHA，按改动完成对抗性复核、记录失败及修复。只有实际通过的完整实现才合入main。合并前复查main和PR heads未被其他人更新；禁止force push。保留原始PR heads为main祖先，让GitHub识别合并，并核实远端树/PR状态。

本轮允许必要提交、源码bundle、main合并及普通源码推送；不创建Release或修改v2.0.4资产，不代发评论、评审或任何贡献者消息，不关机。测试≤300秒/进程、构建≤900秒，无压力或竞争程序。所有命令、日志及未验边界记录WORKLOG和下方实际进度。

## 实际进度

开工保全已完成，guard和计划已建立。PR20已在隔离分支按原head合并；PR19合并待验证提交。原19有三项内容冲突（catalog、PresetLibrary、VideoPresenter），已保留主线NR/Flow格式和暂停缓存接口逐项解决。新增HDR为library v11 / session v6，旧v8/9 NR及v10 Flow不变；新HDR另存.hdr-output兄弟文件，旧配置原字节保留。

HDR10图输出是已经编码的PQ BT.2020，原PR却当线性scRGB处理。已改为仅调优时解PQ到线性nits、按亮度映射并保持signed RGB比例、再编码PQ；scRGB直接在线性域处理。强度/显示峰值只在最终presenter解析一次，初始化与live更新一致；自动峰值按当前输出monitor缓存/刷新，SDR和默认关闭路径不查询峰值或发送新metadata。metadata和曲线默认关闭，标准档保持原样，避免新hint让旧HDR用户的显示器行为改变。可选HDR10 hint不保证Windows转发或显示器使用，界面改为真实启用状态。

修复metadata按source epoch/SDR/close清空、同epoch保留可选源字段、合法MaxFALL≤MaxCLL与DXGI亮度单位、失败限频并记录HRESULT、resize重发。暂停保留帧cache加入实际显示曲线，live编辑能立即重绘；旧player_probe的PresentBlit常量补满16，防未初始化范围。额外测试接点仅HdrColorTests/HdrNativeRoundTripCases的显式输出格式，避免把现行默认HDR10当FP16读回，产品的默认格式没有为测试改变。

build-review-v1因新增metadata状态缺显式HdrOutputTuning include失败，修正后v2/v3标准构建通过。units-review-v1的翻译上下文合并遗漏off/play ctx，旧关闭被翻成Close；按(zh,ctx)重合并后units-review-v2四目标全通过。hdr-review-v1的新真实HDR输出检查通过，旧HDR色彩夹具16例失败，日志证明其“FP16”分支却实际请求默认HDR10；补显式ScRgb后hdr-review-v2两目标全通过，原失败日志不改。

RemotePlay-off-v1在CMake生成阶段失败：Moonlight/Xbox独立使用C源码，但C语言原来只在PS5依赖启用时初始化。将project语言显式声明C/CXX/RC后v2全构建通过。末次自审发现ResizeBuffers保留swapchain对象，而原refresh把metadataSent清零，紧接着关metadata时无法清除旧hint；改成dirty状态请求重发、保留已发送状态，增加真实DXGI resize→disable用例，接受/清除HRESULT均0。这个问题在合并前修复并重新构建/验证，不称未经执行的旧版对比通过。

## 最终定向验证

统一前缀 E:/项目/Veyra；日志均在logs/pr19-pr20-20261006，逐命令收据包含exit、时间及实际EXE SHA。GPU场景顺序执行、无压力负载；每测试进程≤300秒，构建≤900秒。

| 实际命令（python -B scripts/acceptance/ 前缀） | 结果与证据 |
| --- | --- |
| pr19-pr20-build.py build-final-v6 +11目标 | 生产QML、HDR/预设/链/i18n、导出、AMD图/ABI及player_probe通过 |
| pr19-pr20-build.py build-hdr-resize-v8 veyra_hdr_output_tuning_gpu_tests | 最终resize诊断补充目标通过；生产输入不变 |
| pr19-pr20-build.py build-remote-off-v3 --remote-off | RemotePlay关闭的真实生产QML构建通过，Moonlight/Xbox保留 |
| pr19-pr20-tests.py units-final-v4 units | HDR124、效果链239、预设482项PASS；i18n 0 failures |
| pr19-pr20-tests.py hdr-final-v4 hdr | 新HDR GPU23项及现有HDR色彩目标通过；无D3D12 ERROR/CORRUPTION |
| pr19-pr20-tests.py graph-final-v1 graph | 7例AMD共享合成器identity回归通过，四层/temporal最大码值差0；不是HIP推理 |
| pr19-pr20-tests.py abi-final-v1 abi | lmxxf ABI/identity 62项通过；拒绝输入夹具的ERROR日志是预期结果 |
| pr19-pr20-tests.py worker-stack-final-v1 field-stack-hevc | 真子进程双层NR+HEVC完成source=encoded=60，ffprobe60帧 |
| pr19-pr20-ui.py gui-export-final-v1 export | 生产QML NR关闭/单层/双层风格1+2/NR+4K四导出均60帧，ffprobe核对；30.375秒 |
| pr19-pr20-ui.py hdr-ui-final-v2 hdr hdr-final-profile | 七档HDR、手动/非法值、NR5风格2自动调控、Chain+Flow/Chain-only及节点预设保存；ProPage与SettingsPage绑定通过，14.375秒 |
| pr19-pr20-ui.py hdr-ui-restart-final-v2 restore hdr-final-profile | 真进程重启、列表/节点独立恢复、预设应用、未勾选节奏保留及两页绑定通过，13.937秒 |

最终生产EXE SHA256 `d8803feb58a60a3cecc32641c3851b03c3e56059f1eef28dd13a1ad169e1a701`。标准/RemotePlay-off构建、测试依赖均沿用已验Qt6.8.3/MSVC14.44/带PS5 slice补丁FFmpeg，不引入新SDK或运行库。真实UI为自有ui-app及新profile，加载当前源码QML；测试Loader在finally恢复，不改用户配置。旧GUI回执、全部失败回执及子进程日志保留。

新HDR灰阶knee误差scRGB0.0779% / HDR10 0.4415%，保持单调、有界，关闭/0%原像素精确一致，图/导出surface原字节不变。BT.2020色块归一化RGB误差scRGB0.00013806 / HDR10 0.00101177，负scRGB分量保留。上述是GPU/DXGI像素回读，不是物理显示器亮度/色差测量。系统报告240nit只显示为系统信息，不作为真实峰值仪器验收。HDR10 metadata接口可接受不证明Windows转发/屏幕应用；Microsoft明确提示其可能被忽略，见[SetHDRMetaData](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata)及[HDR10单位](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10)。

本轮自审结论：两PR可在上述适配后整合；保留PR19/20作者原始head作main祖先，不对贡献者分支force push。先前OBS repaint、Native多层导出、AMD上下文合成修复保留；RX9000实卡可用性来自用户本轮反馈，本机是RTX5070/诊断identity，未重新进行AMD HIP/全部RX9000或物理HDR/其他显卡验证。既有PR13/14仍暂缓，无新Release或v2.0.4资产改动。最终源码/证据冻结及main/远端状态以logs/.../tested-inputs.json、main-advance.json、remote-after.json为准；下方记录实际合并结果。

## 合并及收尾结果

`pr19-pr20-finish.py freeze/commit/merge/push/verify` 已实际执行。冻结565产品输入、161测试/脚本输入、92证据文件，scope guard及四原发布ZIP SHA通过。PR20合并提交 `2e3d2397945f5a22bcbdefa87d1990e83a91de1e`；PR19适配合并提交 `357c13679cf193ad7e48f51218b678d6aef53b6c`，父提交为2e3d239与作者原head b14dc5a。8个新增可达提交已审查，没有新增DLL/SDK/模型/编译产物进入Git。source-integration.bundle 73856888 bytes，SHA256 `7bd38da97fce41739dcf3d19ae436ca501a075bc86421a63125e737ec6ff6c60`，bundle verify成功。

本地main从 `f8045fb53a7d800b053f0631a9b44dc9f5f1aacb` 快进到357c136，无冲突，Git树 `8976fc731aab0e55467e39b42bfe4402ed6ccee3` 与实际冻结树完全一致，普通推送nrvideo成功。REST独立核实两PR均closed/merged：19 mergeCommit=357c136，20 mergeCommit=2e3d239，原heads不变。v2.0.4的tag、Release正文/状态/全部assets对象及本机四ZIP原字节保持；没有新Release、上传、贡献者评论或消息。最终收尾提交只补这三个记录文档和自有清理脚本，产品输入/EXE及以上测试证据不得改变，最终main SHA见final-check.json。

`pr19-pr20-cleanup.ps1` 核对自有测试进程退出、E盘任务绝对边界、无reparse，并与已批准fix1组件逐字节SHA比较后删除431项重复根DLL/完整runtime副本，共1812729736 bytes。当前标准/RemotePlay-off构建、完整测试ui-app、源码/bundle、所有失败/成功回执、worker日志、exports/profiles与identity fixtures保留。首轮因PowerShell5.1把无BOM UTF8中文路径误解码而在枚举前失败、无删除；根路径改用原生PSScriptRoot解析及固定E盘校验，重新执行成功。没有删除用户文件、旧工作区或原发布包。23个其他原工作树的HEAD/status/修改SHA不变，main仅按当前授权推进，开工24个工作树仍全部保留。
