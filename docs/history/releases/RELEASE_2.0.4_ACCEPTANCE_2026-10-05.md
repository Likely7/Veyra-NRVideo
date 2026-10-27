# 2.0.4 本地整合验收

2026-10-05 开工，2026-10-06 凌晨完成生产程序回归。本轮整合 Claude 最新 UI、NR 三风格调控与 Xbox 恢复，既有 main 性能/现场修复保留。公开版本仍为 2.0.3，未执行新 push/Release。起点及所有分支判断见 RELEASE_2.0.4_PLAN_2026-10-05.md、BRANCH_MAP_2026-10-05.md。

所有新产物按用途写入 `E:/项目/Veyra/{build,tests,logs,tmp,test-packages,verify,archives}/release-2.0.4-20261005/`。下述命令从该任务 worktree 执行，日志为同任务 logs 内对应 label.log/.json。每个测试进程有≤300秒超时，构建≤900秒；GPU工作串行，无压力负载。

## 构建及实际身份

- `py -3.11 -B scripts/acceptance/release-2.0.4-control.py`：其余19个已有工作树 HEAD/status/patch/未提交文件字节不变，原桌面及 Claude 修改保留；main 仅接受本轮 merge 回执所记状态。全引用 before bundle 已 verify，SHA256 `41787718148cee716ab790a4d12386a61409547eb260b60dc0d893f33491bf85`；start.json SHA256 `d7e95a9341a90b1550f5764246fe18376d01785727b383bc53a5038135f007f8`。
- `py -3.11 -B scripts/acceptance/release-2.0.4-build.py build1`：518步/exit0，生产及测试目标构建成功。build2 修正补帧预设返回值，10步/exit0；build3 修正导出布局及补齐翻译，7步/exit0。使用已验 patched FFmpeg+dav1d、Qt6.8.3、静态 libass 及既有 SDK，不下载新运行库。
- 最终主程序 SHA256 **`dadb7cd28022a4ebff9291b4fc6b282e6253bad397fff7ae6f81ff2bf069322d`**，FileVersion/ProductVersion 均 **2.0.4**。下述生产 app 用例均实跑该 EXE；后续只改验收文档/脚本，不改变产品输入。源码冻结哈希见外置 `logs/.../tested-inputs.json`。
- `py -3.11 -B scripts/i18n/extract.py --check`：2010条，zh-TW/en/ja 缺失均0，placeholder problems=0；编译后的 i18n2 同时退出0。

## 本轮实际通过

测试命令格式：`py -3.11 -B scripts/acceptance/release-2.0.4-tests.py LABEL CASE [FIXTURE_OR_APP]`。APP 为 `E:/项目/Veyra/test-packages/release-2.0.4-20261005/Veyra-2.0.4-NVIDIA-win64-portable`；AMD 用对应 AMD 目录。

| Label / CASE | 实际结果 |
|---|---|
| xbox1 / xbox | 97检查、0失败；本地协议/恢复测试，不连接远端账号/主机 |
| scene1 / scene | 18检查、0失败 |
| timing1 / timing | 78 PASS，采集真实PTS/节奏/断点及期限等回归 |
| repair1 / repair | 246检查、0失败 |
| chain1 / chain；preset1 / preset | 退出0，处理图与完整预设往返 |
| legacy2 / legacy | 退出0，schema1–21/当前NR参数/容量/损坏保护等往返 |
| i18n2 / i18n | 退出0，最终编译目录翻译 |
| hardware1 / hardware | 56检查、0失败 |
| vfgsettings1 / vfg-settings | 331检查、0失败 |
| audio1 / audio | Xbox float、独立RTP原点、250音频块通过；非真实Xbox连接 |
| codec1/codec2 / codec | 真实H.264软/硬解（无B/B2）、坏包flush/等待未来IDR、丢包像素/PTS、重开及软件替换、旧D3D12帧lease全部通过 |
| temporal1 / temporal | 实际GPU时域回归通过 |
| correction1 / correction；correctionfp161 / correction-fp16 | FP32/FP16各45检查：原始0/1/2/5一致、三风格、独立控制/HDR边界、debug错误/警告0 |
| qml2 / qml | 50 passed、0 failed、0 skipped；真实Qt鼠标/键盘，含新数字输入/边界/小数/百分比/Enter/Esc/失焦，以及已有导出四窗口布局等 |
| ui2 / ui | 6.141秒PASS；完整补帧预设保存/应用/重复拒绝/删除，模式与列表宽度/播放进展；GPU请求5、实际5/实时 |
| uirestore2 / ui-restore | 3.641秒PASS；第二进程恢复并逐字段核对，故意加入flowQuality=99后拒绝完整成功并提示部分未生效，最后清理测试预设 |
| hot2 / hot | FSR3.1→XeSS→DLSS→FSR3.1→关闭，各实际active后继续出帧；两层原版NR强度5，风格1自动/2手动，继续播放及PNG通过 |
| nrexport2 / nr-export | 35.766秒PASS；原始5/自动/手动/关闭、四张PNG、列表/节点、预设/复制/重置、HEVC NVENC D3D12 4K导出；ffprobe确认60帧、3840×2160、HEVC |
| nrrestore2 / nr-restore | 第二进程恢复列表/节点九项控制及力度，PASS |
| smokeAMD1、smokeNVIDIA1 / smoke | 各Windows-only依赖PATH、隔离profile启动/播放/退出通过；libass内嵌5事件、1字体附件 |
| smokeNative1 / smoke-native | 默认D3D12 UI（日志明确renderer=d3d12）、播放/字幕/退出通过；无强制软件渲染参数 |

codec1/2 使用 `E:/项目/Veyra/tests/capture-xbox-field-20261005/media` 下无B及B2夹具；完整实际 argv 和 fixture SHA 见各 JSON。应用测试只临时向本任务候选 Main.qml 注入受控夹具，finally 原字节还原；用户配置、原包及其他工作树不改。

## 失败与整合修复

- legacy1 首次使用没有父目录的 `legacy-presets.v1`；既有保存接口创建空父路径失败，造成后续往返失败。测试改为隔离目录内 ASCII 相对路径 `presets/legacy-presets.v1`，legacy2 通过；不修改产品来迁就夹具。
- nrexport1 的旧 MPEG4 人工 AVI 先触发既有硬解 -22 回退；同时真实导出过程中发现 Claude 新滚动条布局依赖 `visible`，宽度/换行高度/滚动条状态形成重排循环。保留失败日志，改为固定滚动条留白；使用新2秒/60帧H.264无B夹具后，nrexport2 正常导出且无错误/重排循环。不把 -22 隐藏为通过。
- Claude 原补帧预设仅凭后端名称判断成功，其他字段拒绝或倍率截断仍会返回true。本轮逐字段核对实际设置；跨进程注入非法值的 uirestore2 已证实返回false与错误提示。
- qml2 有继承测试 stub 缺少 exportStopsPlayback/playbackRate 属性的告警及焦点告警；50项均通过。生产 app 的上述用例没有对应 TypeError/ReferenceError/错误，不将测试告警说成产品全部无告警。
- export-worker 将日志写到候选运行目录。仅将本任务新候选内的 export-worker 日志完整移到 `logs/.../candidate-worker-logs/`，记录原位置、目标和SHA；不进入交付包，不动旧包或用户文件。

## 主线、组件与封存核验

NVIDIA/AMD 使用同一最终 EXE；stage/refresh 的逐文件SHA、PE直接/delay imports与依赖闭包、厂商边界均通过。NVIDIA 215项、AMD 694项既有组件文件原字节保持（计数含许可证/manifest/模型，非DLL数量）。AMD包括此前补齐的8份libass许可证；不新增/修改运行二进制，不混入SDK/驱动、配置、测试媒体、日志/PDB/LIB。

生产回归通过后，于2026-10-06 00:13 Asia/Taipei完成本地 `main --no-ff` 合并：main从 `de18fc4` 合并实测整合提交 `270aaaa`，merge为 **`2a39a8bc9a626b084edc96c5edd398d4c596237f`**。无冲突，合并Git树与实测源树完全相同，main干净，checkpoint标签已建立；随后仅补充这份合并记录，产品输入与最终EXE不变。

最终 sourceCommit、mainAfter/mainMerge、Git树一致性与 before/after 信息以 `archives/.../main-merge.json` 为准；源码ZIP仅含受版本控制的应用源码，最终 bundle 单独 verify。封存时要求源树/main干净，所有必要结果passed且app用例EXE SHA与最终EXE一致。

ZIP大小/全SHA/CRC与每项载荷哈希的实际结论见 `test-packages/.../DELIVERY.json`（zipCrcAndAllPayloadHashesPassed）；独立干净解压启动见 `logs/.../cold-verify-results.json`（两个passed）。后者仅用Windows系统PATH，并逐个核对实际加载Qt/FFmpeg模块来自刚解压目录，不依赖研发环境PATH。只有对应实际回执才构成封存验收，不能用本文件替代未执行的最后步骤。

## 未验证边界

真实RX9070XT+Xbox错误触发/恢复、RX9000 NR推理与画质、RTX20/30/40、受影响采集卡及后台长稳、用户616.92驱动、物理显示事件/端到端延迟未新增实机验收。采集日志输入60.009/s而处理54.453/s，不能说原增强组合已达到60处理FPS；Xbox底层 -22 触发器也未在本机复制。软件解码回退可能增加CPU/延迟。

GPU实时档的系统拒绝/高档回退未在拒绝环境实测；原生HDR整套旧失败没有因本轮标为通过，HDR/Dolby PR13/14仍暂缓。暂停后才接入OBS的真人场景仍有缺口。不改用户驱动/配置，不派Agent或制造竞争负载，不执行公开push/Release/资产上传/关机。
