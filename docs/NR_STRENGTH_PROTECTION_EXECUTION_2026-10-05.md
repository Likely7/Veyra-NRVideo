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

## 风格 1/2 色偏与调控参数续修（2026-10-05）

用户明确反馈 NVIDIA 原版、总变化强度 5，风格 1/2 有明显色偏，风格 0 较自然，手动只有局部压缩明显。起点为首轮已交付 `9eb3b1cf4431362738081442e3f0e071a19962e5`；tag `checkpoint/pre-nr-style-controls-20261005`，续修前 bundle 已验证，SHA256 `19703EFD383E2B012AF6FFDE0663395CB2276D0A9FCA3D524DD6AE16D2EBF42C`。原包与原 `DELIVERY.json` 不覆盖。

审查和原版 runtime=3 的真实调用确认：三个风格原来都接入同一 shader，并非漏掉风格 1/2。旧自动规则没有分风格的颜色保留约束，灰轴处色相投影减弱，且仅约束绝对 Lab 色度仍会因亮度改变而改变饱和度。原五项多是超过阈值才生效，暂停画面不体现时域稳定。这些是保护策略和可调性缺口，不能称模型风格未接入。

续修保留原模型风格和总强度 5。自动分别设置中性色／原图色彩／亮度／暗部保护，风格 1 为 `1/.9/.25/.55`，风格 2 为 `1/.95/.2/.4`，风格 0 为 `1/0/0/0`（原来五项自动值仍为 1）。保持随当前亮度缩放的源色度而非把图像调成灰色，源色彩射线接近色域上限时约束亮度，避免最终色域映射再次脱色；灰区染色判断使用平滑风险门限，保留 FP16／proxy 的微小量化变化。无新的 NR Evaluate、全局逐帧白平衡／曝光、未来帧或 CPU 像素回读。

手动增加“中性色保护、原图色彩保留、亮度保持、暗部保护”，合计九项，各有适用说明；可显式采用当前风格自动值作为起点。自动力度 0–1，0 保留原始输出且不分配调控时域历史。自动、手动值分别保存。新参数仅非默认时写 PresetStore v30／PresetLibrary v9；旧 v29／v8 手动配置读取时新四项为 0、自动力度为 1，保留其原含义。残差／全栈保护／oracle 根常量统一 40，时域 44；公共 shared-memory settings 与同一个生产 EXE 内导出 worker 一起构建。

### 续修实际验证

下列路径均相对于 `E:/项目/Veyra/`，测试每进程≤300s、构建≤900s，串行正常负载，无压力程序。

| 检查 | 命令、结果与证据 |
|---|---|
| 生产构建 | `nr-protection-build.py style-build1 --styles veyra_qml_ui veyra_preset_library_tests veyra_repair_preset_tests veyra_effect_chain_tests veyra_nr_antiflicker_tests veyra_nr_correction_gpu_tests veyra_repair_shader_tests veyra_nr_temporal_gpu_tests veyra_qml_quick_tests`；修复量化风险后 `style-build2 --styles veyra_qml_ui`，均退出 0；`logs/nr-strength-protection-20261005/style-build*.log`。新增声明对应 `veyra_nr_video_quality_probe` 另构建通过（`style-probe-build.log`），未将其未执行的实卡场景记为通过。仍继承 field-fixes/B 的 libass 与 patched FFmpeg 路径。 |
| 预设、会话与链 | `nr-protection-tests.py style-cpu preset-library preset-legacy effect-chain nr-tiers` 全过；原 v29/v8 和新增 v30/v9，隐藏手动数值、两模式／多层／节点 editor、自动力度 0 的拓扑与自动值转手动等均检查。`logs/nr-strength-protection-20261005/style-cpu/results.json`。 |
| GPU 与时域 | `nr-protection-tests.py style-gpu2 legacy-shader temporal correction correction-fp16` 全过。FP32/FP16 各 45 checks、2048 像素，关调控 0/1/2/5 与冻结旧 shader 逐位一致；自动力度 0、四项独立响应、源色彩比率、亮度／暗部、signed HDR、零残差、全栈保护与既有运动／切镜／reset 检查；两种精度 D3D12 errors/warnings 均 0。`logs/nr-strength-protection-20261005/style-gpu2/`。 |
| 真鼠标 QML | `nr-protection-quick.py style-mouse1`，40 passed/0 failed；新增力度和全部九项滑块真实鼠标响应，模式往返保存值；`logs/nr-strength-protection-20261005/qml-style-mouse1/result.json`。夹具画布 1500 高，覆盖展开后的九行说明；不改生产窗口尺寸。 |
| 原版实际视频／保存／导出 | `nr-protection-ui.py first style-ui1 --styles`，原版风格 2 强度 5，自动值转手动、九项编辑、复制／重置／预设、节点与列表贯通，实际 4K HEVC NVENC 导出 60 帧。随后 `restore style-restore1 --styles` 在第二进程恢复新增参数；`logs/nr-strength-protection-20261005/production-style-*/` 与 export-probe.json。 |
| 不同风格多层 | `nr-protection-ui.py multi style-multi1 --styles` 原版两层强度 5，第一层风格 1 自动、第二层风格 2 手动，真实创建／播放／全栈保护及 PNG 成功；`production-style-multi1/`。 |
| 真实视频三个风格 | `nr-protection-styles.py original-fixed --app <新候选> --media E:/项目/Veyra/tests/hotfix-2.0.0-20261002/media/gta6-1080p30-12s-audio.mp4` 以及 `original-fixed2 --manual` 均通过；原版 runtime=3，三风格 raw/auto、独立四项与自动 0 实际 PNG 输出。第一次新旧视频 run 分别停在 0.8/0.8333s，因此不拿它们计算严格跨版本同帧收益。 |
| 严格同源对照 | `nr-protection-styles.py original-baseline3 --app <首轮包> --media <固定 source.png>` 与 `original-fixed3 --manual --app <新候选> --media <同一 source.png>` 均过；源 PNG SHA256 `1a620d8bdfaaaccb03d139038861156b93399ed2a429e38d586d79e308ccf025` 相同，三个风格的 raw 输出也分别逐位相同。`nr-protection-image-metrics.py` 两次测量后，`nr-protection-style-audit.py` 通过；`logs/nr-strength-protection-20261005/style-picture-comparison.json`。 |
| 字幕与翻译 | `nr-protection-smoke.py style-inherited1 <新候选>`，真实 MKV ASS 和内嵌字体启用通过；`smoke-style-inherited1/result.json`。`scripts/i18n/extract.py --check`：1995 entries，繁中／英文／日文 missing=0，placeholder problems=0。 |

固定输入的源相对平均色彩距离：风格 1 从旧自动 `0.0100369` 降到 `0.00113989`，风格 2 从 `0.0125284` 降到 `0.00101397`。这只是该 SDR 帧的颜色变化数值，不是所有素材的画质评分。四项新手动控制的独立结果不同：原图色彩保留 1 时颜色距离 `0.0009387` 而亮度变化仍 `0.0590955`；亮度保持 1 时亮度变化 `0.0007461` 而颜色变化仍 `0.0178619`；暗部保护 1 时暗部进一步压黑从 `0.0140620` 降至 `0.0024489`；中性色保护 1 时灰区颜色距离从 `0.0230654` 降至 `0.0052643`。同一固定输入下，所有手动项为 0／自动力度为 0 的 PNG 与风格 1 原始强度 5 完全同 hash。

### 续修失败与边界

- `style-gpu` 首次 FP16 的“安全比例细节保留 5”失败，中性色规则把微小量化色差也当染色修复。加入平滑 tint-risk 下界后 `style-gpu2` 两精度全部通过，未放宽原检查阈值。失败日志保留。
- 视频 `original-baseline2` 夹具同 tick 暂停／关闭 NR／seek 的严格位置检查仍读到 0.8s，未取得要求的 0.8333s；没有据此修改播放／seek 引擎。另一次 `original-fixed2` 视频 seek 对照成功。严格跨版本对照改用同一固定 PNG，并审计源图与 raw 输出字节相同；失败不计为通过，不以相邻帧差异冒充纠偏收益。
- 首轮本地 native HDR 的 8 项基线失败、RTX 20/30/40／AMD／真实 HDR／长时运动拖影与主观质量未验边界仍存在。续修重点核验本机 RTX 5070 的 NVIDIA 原版，未替换或修改任何运行库。不宣称模型重造纹理／几何错误可被色彩调控完全修复。

### 续修本地交付

新目录 `E:/项目/Veyra/test-packages/nr-strength-protection-20261005/Veyra-2.0.3-nr-controls2-NVIDIA-win64-portable/`，同级新便携 ZIP 和源码 ZIP，`DELIVERY-styles2.json` 记录最终源码提交、逐文件清单和包 hash；续修源码 bundle 在 `archives/nr-strength-protection-20261005/style-controls/`。最终以 `nr-protection-package.py finalize --styles` 核验、生成，不覆盖首轮产物。仍无 merge／push／Release。
