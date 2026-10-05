# NR 强度与画面调控执行记录

## 2026-10-05 开工

起点 `de18fc4`；隔离工作树/授权/合同见 `NR_STRENGTH_PROTECTION_PLAN_2026-10-05.md`。
`git status --short` 干净，tag 与 `git bundle verify` 通过。归档 SHA256 已记录在方案。
产物统一使用 `E:/项目/Veyra/{build,tests,logs,tmp,test-packages,archives}/nr-strength-protection-20261005/`。

## 实现及审查

- 总残差强度 0–5；每层调控默认关。自动和手动都保留强度 5，手动五项 0–1，自动启用源图约束。模型强度仍为原来的 0–1，不额外调用模型。关闭调控与起点 shader 在 0/1/2/5 下逐位一致。
- 来源为 Magpie `27c5df91177a29b33be612e98274169f3d2fca49`，Oklab 色相投影、灰轴连续保护、软膝残差压缩。Veyra 改为直接线性 FP16，追加色度变化预算、高光肩部和固定色相色域搜索；HDR 使用 signed scRGB / BT.2020 物理边界，保留负 BT.709 分量与源高光。没有逐帧曝光、CPU 像素回读或未来帧。来源及修改见 `THIRD_PARTY_NOTICES.md`。
- 时域使用现有 80 ms 光流重投影历史，按原图补丁一致性拒绝不可信历史；切镜、无光流、间隔断点、参数变更、零当前残差不保留旧修正。开启调控接管旧防闪，关闭后恢复旧设置；手动稳定性 0 不启用历史。
- 列表／节点共用 `NrLayerEditor`、桥接与处理图；单层／多层、全栈保护、暂停、图片保存和视频导出使用同一组参数。旧预设默认关；新参数或强度 >2 使用 preset schema 29 / library schema 8，会话保留其嵌套配置。复制、重置、保存与重启恢复均已检查。
- 对抗式源码审查发现多层最终保护 pass 仍使用旧 24 常量布局，已统一为 32（补充块清零，避免重复调控）。实际两层分别自动／手动、强度均为 5、带全栈保护的播放器短测通过。
- 构建配置最初继承旧发布缓存，遗漏 Claude 的 libass。候选未交付；已改为最新 `build/field-fixes-20261005/B/CMakeCache.txt` 的全部依赖配置，静态 libass 路径与已验收版一致。最终实际播放器日志有 `fonts configured container=1`、`track ready events=5 styles=3`，保留 ASS 与 MKV 内嵌字体能力。

## 已执行验证

| 验证 | 结果及证据（均在 E:/项目/Veyra 下） |
| --- | --- |
| 生产 C++ / HLSL 构建 | `py -3.11 -B scripts/acceptance/nr-protection-build.py stack-root-build veyra_qml_ui veyra_nr_correction_gpu_tests veyra_nr_temporal_gpu_tests`，退出 0；`logs/nr-strength-protection-20261005/stack-root-build.log`。后续仅改测试／文档，生产 EXE SHA256 `599fd89057a645540cf51e1afa3f93534297ee2de5a95ec1073f346b50149174`，19702272 bytes。 |
| 参数、旧预设、会话、处理链、旧防闪、残差回归 | `py -3.11 -B scripts/acceptance/nr-protection-tests.py final-cpu preset-library preset-legacy effect-chain nr-tiers legacy-shader` 全部退出 0；`logs/nr-strength-protection-20261005/final-cpu/results.json`。包括强度 5 关闭／自动／手动、隐藏手动值、flat／多层与列表／节点保存、严格输入验证、旧文件保护。 |
| 实际 GPU 色彩 shader | `py -3.11 -B scripts/acceptance/nr-protection-tests.py final-gpu temporal correction correction-fp16` 全部退出 0。FP32 和生产 FP16 各 2048 像素、34 检查；关闭时对冻结起点 DXIL 的 0/1/2/5 逐位一致，maxError=0；自动色域／肤色／灰轴／高光／阴影、有限值、手动各项、零保护／零残差、signed HDR。两组 D3D12 debug errors=0、warnings=0；`tests/nr-strength-protection-20261005/final-gpu/shader[-fp16]/shader-result.json`。 |
| 实际 GPU 时域 | 同一 `final-gpu/temporal.log`：48 帧交替亮度残差输出范围 <0.02（输入 0.06），交替色度范围 0.12→0.03125；运动越界、源图变更、断点、无光流、reset、零当前残差与手动稳定性 0 均正确旁路／清历史；debug error=0。 |
| 鼠标／键盘组件 | `py -3.11 -B scripts/acceptance/nr-protection-quick.py mouse4`：40 passed、0 failed；真实 Qt 鼠标点击、滑块、最高值键盘钳位、开关、自动／手动、五个参数、隐藏值保留、多层独立；使用软件 Qt 界面和显式测试快照，不冒充真实引擎桥接。`logs/nr-strength-protection-20261005/qml-mouse4/quick.log`。 |
| 真实单层播放／PNG／预设／导出 | `nr-protection-ui.py first final1` 通过真实播放器桥接控制，四张暂停同帧 PNG、预设保存／应用、复制／重置、两种编辑模式切换。NVENC D3D12 HEVC 3840×2160 导出 source=60、encoded=60、generated=0、hold=0；ffprobe 验证 60 帧，`logs/nr-strength-protection-20261005/production-final1/export-probe.json`。随后 `restore finalrestore1` 第二进程成功恢复两模式强度 5 与手动值。 |
| 真实多层及保护 | `nr-protection-ui.py multi multilayer1` 两层强度 5，第一层自动、第二层手动，真实 NR 创建／播放／全栈保护及 PNG 成功；`logs/nr-strength-protection-20261005/production-multilayer1/`。 |
| 真实游戏素材静帧对照 | `nr-protection-ui.py visual realvideo1 E:/项目/Veyra/tests/hotfix-2.0.0-20261002/media/gta6-1080p30-12s-audio.mp4` 通过；`tests/nr-strength-protection-20261005/production-realvideo1/screenshots/`。人工查看原始 5／自动／手动，观察到自动抑制原始 5 的明显色偏；这只是该帧检查，不是所有素材主观画质或拖影验收。 |
| 继承字幕功能 | `nr-protection-smoke.py inherited2` 实际原 MKV ASS／字体夹具通过，退出 0，无产品错误；`logs/nr-strength-protection-20261005/smoke-inherited2/result.json`。 |
| 翻译与范围 | `py -3.11 -B scripts/i18n/extract.py --check`：1979 entries，繁中／英文／日文 missing=0，placeholder problems=0。每阶段 `nr-protection-control.py guard` 确认独立分支、固定开工归档、其他 18 个工作树 HEAD/status 不变，禁止 SDK/DLL/模型入本轮源码。 |

本机为 RTX 5070，沿用已核验的 Lecram（F95FEB…）、SFv2（6EB209…）与 NVIDIA 原件（E16BCF…），未采用 Swapper 新 DLL。只做正常有界短测，没有竞争负载、压力测试或停止用户进程。构建 ≤900s、每项测试进程 ≤300s，TEMP/TMP 只对本任务子进程设在 E 盘。

## 失败、修复和边界

- 开发期间两次编译失败分别为 `ChainNrParams` 没有 `usesTemporal()` 和测试类型缺少命名空间，均已修复并重新构建。最初 GPU HDR 测试错误地要求两个 BT.709 分量都保持负值，改为验证实际的 signed 色域与源色相／物理峰值合同，未通过放宽 SDR 高光或非有限值检查制造通过。
- 旧预设测试必须传有父目录的目标文件；首次未传参数／无父目录均属夹具错误，已修正。实际 UI 夹具先遇截图目录未建，再遇“预设 UI 行号”与“预设 id”偏移，改用真实 `id` 后保存／应用通过。QuickTest 首次缺 Qt QML 模块，后补完整打包模块；随后修复测试布局等待、总强度新范围及布尔快照转换。libass 首次 smoke 错误要求日志输出版本号；库本身已正常渲染，已改为检查真实字体和 track ready 证据。失败日志保留，不记成产品通过。
- MPEG4 人工短片首次硬解 `send_packet -22` 后使用原有软件回退，NR 播放及导出正常。另用真实 H.264 游戏素材核对原路径；不能把此 MPEG4 回退隐去或称为本轮已修复解码问题。
- `veyra_hdr_color_tests` 的 8 项 native HDR_COLOR 失败在起点 main `de18fc4` 独立构建／执行后，失败名称、数值完全一致：HLG=0 error=12.4882，HLG=1 error=2.53929，各四种组合。证据 `logs/nr-strength-protection-20261005/hdr-baseline-comparison.json`；对应 NR 关闭，属于已存在的 HDR 边界，未修改／合并暂缓的 HDR PR。全局 HDR suite 不是通过。
- 保护会牺牲危险区域的局部变化，不能保证“完整局部增益 5 + 所有素材正常”；有些 NR 新造纹理／几何错误无法从色彩后处理中恢复。仅确认本机软件执行与上述数值／素材，RTX 20/30/40、AMD、真实 HDR 显示、真实采集、长时运动拖影及用户主观质量仍需对应设备／素材。
- 自动 NR 尺寸（按负载）原有合同不接受时域 NR；开启调控稳定时继续显示该限制，不借本功能扩大 Auto 尺寸准入。本次不修原后台掉帧／GPU UI 采集问题。

## 本地交付

目录：`E:/项目/Veyra/test-packages/nr-strength-protection-20261005/Veyra-2.0.3-nr-controls-NVIDIA-win64-portable/`。
同级便携 ZIP、对应源码 ZIP、最终逐文件 manifest 及 `DELIVERY.json` 由 `nr-protection-package.py finalize` 在本地源码提交后生成和核验；最终包摘要以 `DELIVERY.json` 为准（不自引用包 hash）。开工与最终源码 Git bundle 保留在 `archives/nr-strength-protection-20261005/`。
运行库／许可证继承已验收的 field-20261005b NVIDIA 包；本轮 export-worker 诊断移到任务 logs，不随包提供。主线、桌面旧工作树、其他工作区和用户配置保留；本功能没有合并、推送或发布。
