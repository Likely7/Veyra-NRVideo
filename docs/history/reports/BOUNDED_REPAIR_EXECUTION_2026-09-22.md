# 全链路有界修复执行记录

用户已授权实施、验收、有效修复存档、无效实验回退，结束后关机；没有推送或发布授权。
开工存档 `7724ea8` / `checkpoint/pre-bounded-repair-20260922`。
隔离分支 `codex/bounded-full-chain-20260922`，工作区沿用 `playback-nr-20260920`。
存档包含尚未验收的 HDR 查询候选，不等于这些改动已通过。

## 执行边界

- 全部问题以 [总台账](WHOLE_PRODUCT_ISSUE_LEDGER_2026-09-22.md)为入口，
  [全链路方案](../plans/FULL_CHAIN_REGRESSION_REPAIR_PLAN_2026-09-22.md)为技术边界。
- 每条新性能假设最多一次基线/候选比较和一次必要复核；没有收益就撤回。
  已排除路线不重复，不降低画质、不自动降倍率、不增加固定等待或队列深度。
- 优先修复可证明的合同错误：HDR 查询必须区分失败和 SDR、缓存按显示器隔离；
  采集格式必须按稳定身份恢复、协商结果包含帧率验证。
- DLSS 2X/6X、XeSS 2X/4X 检查整个资源/历史/呈现链；上游资料仅作为合同依据，
  不当作本机性能证据。只有新证据指向具体错误才改，不再无目标调整节奏参数。
- 清晰度、采集断档、音频、字幕、UI、导出等逐项记录处理结果和实机边界。
  无日志或缺少受影响硬件的项目不写“已根治”。
- 当前轮产物集中在 `E:/项目/Veyra/tests/bounded-repair-20260922/`，
  临时文件在 `E:/项目/Veyra/tmp/bounded-repair-20260922/`；沿用隔离构建目录。

## 进度

1. 已完成开工存档和隔离分支；干净工作区确认。
2. 已完成 HDR 目标隔离、采集格式身份与协商检查，产品及相关测试构建通过。
3. 针对性构建/回归完成；性能主问题仍开放，按有界停止条件收尾，不宣称全部根治。

## 保留的产品修改

- HDR 查询区分明确 SDR 和查询失败。同一显示器失败时保留最近成功结果，
  换显示器、无显示目标、新会话不沿用其他目标的 HDR 状态。查询结果与 HMONITOR
  来自同一次查询，避免跨屏时再次 MonitorFromWindow 取到另一个目标。
  此项消除的是已证实的状态错误，不是用户所有欠速闪烁的根因结论。
- 采集 URI 新增格式键，最近打开和重连按分辨率、帧率、像素类型等身份恢复。
  键不存在时明确报错，不退回已变含义的序号；旧 URI 仍保留原序号语义。
  连接结果核对尺寸、帧率、设备子类型，重连复核原颜色和帧率合同。
  不能证明用户那次重开变糊就是格式串位；也不改变色彩或增加锐化。

修改入口：`CapturePanel.cpp`、`CaptureCardSource.{h,cpp}`、
`CaptureFormatSelection.h`、`HdrDisplayState.h`、`PresentSink.{h,cpp}`、
`EngineController.cpp`，配套单元/集成测试及 CMake 注册。
没有修改 DLSS/XeSS 调度策略、倍率、队列深度、画质、NR/SR 参数或等待时间。

## 插值位置诊断：失败保留，未改产品

`veyra_fg_presentation_tests` 原资源检查 PASS：4X→6X→4X、resize、
原帧早呈现/生成读取生命周期，呈现像素一致、D3D12 errors=0。
新增单独 `temporal` / `temporal-exact` 诊断模式，640×360 灰底运动矩形，
每源帧平移 12px，20 源帧并包含一次显式跳源 reset；静态灰块同时监测亮度。
位置容差固定 2.5px，没有为通过而放宽。

| 倍率 | NVOF 最大位置误差 | 解析运动向量最大位置误差 | NVOF/解析向量违规帧 |
| --- | ---: | ---: | ---: |
| 2X | 0.880074px | 0.883735px | 0 / 0 |
| 4X | 2.25125px | 2.26911px | 0 / 0 |
| 6X | 3.55059px | 3.54614px | 18 / 18 |

两种模式分别生成 18/54/90 帧，灰块误差均为 0，D3D12 errors=0；
两个诊断进程均 exit3，代表画质门槛失败，不是通过。
精确的是该矩形的解析位移，并不表示我们具备游戏原生深度/所有引擎信息。
此现象对应 09-17 已记录的内容相关方块/正弦位置误差：当时直接 NGX、
精确/NVOF/反向向量、矩阵和串行等待也未消除，非周期纹理又能通过。
因此不是新发现的统一调度根因，不重试这些参数路线，不归咎于运行库内部算法。
本次只保留回归诊断，没有产品实验需要回退。
日志：`fg-normal.log`、`fg-temporal.log`、`fg-temporal-exact.log`。

## 上游合同复核

- Intel XeSS 指南固定提交 `de0fb9c1c510661c571164e1418ceca8101dab69`：
  https://github.com/intel/xess/blob/de0fb9c1c510661c571164e1418ceca8101dab69/doc/xess_fg_developer_guide_english.md
  `UNTIL_NEXT_PRESENT` 需保持到配对 Present；frameRenderTime=0 允许表示未知，
  COMMON 合法、NON_PIXEL_SHADER_RESOURCE 是优化建议，不能据这些值直接判错误。
- 本地 Magpie `3841698348bfb246623d4acf791984c8b68a577b`：
  `XeSSFGPresenter.cpp` 的复制/提交/tag/Present 顺序与资源生存期，
  `DLSSFrameGenerator.cpp` 的生成张数、子帧编号、motion/相机参数分别核对。
  参数约定不同不等于 Veyra 错误；既有位置实验不支持盲搬 motionScale。
  没有抄入未经验证的新调度器，没有以 Magpie 项目名代替本机证据。

## 验收日志与可重复命令

下列相对日志路径均位于 `E:/项目/Veyra/tests/bounded-repair-20260922/`。
可运行目录为其中 `app/`；它使用本机受控 runtime/依赖链接，是研发 staging，
不是独立便携包。NR 原件此次核验 SHA256 为
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`，
165840496 字节，NVIDIA 有效签名；不宣称本次重新审计所有运行库身份。

- 构建：`scripts/build-isolated.ps1 -Root E:/项目/Veyra/worktrees/playback-nr-20260920
  -BuildDirectory E:/项目/Veyra/build/playback-nr-20260920
  -DependencyCache E:/项目/Veyra/build/playback-nr-20260920/CMakeCache.txt
  -TempDirectory E:/项目/Veyra/tmp/bounded-repair-20260922 -Targets ...`。
  产品/相关目标 `build-final.log` exit0；后续仅诊断目标 `build-temporal.log` exit0。
- `veyra_hdr_display_state_tests`、`veyra_capture_format_selection_tests`、
  `veyra_ui_contract_tests`、`veyra_color_grade_tests`、`veyra_subtitle_panel_tests`、
  `veyra_control_paint_tests` 均 exit0，各自 `*-final.log`。
- `veyra_repair_preset_tests` 首次漏必需路径 exit2；补上本轮 `preset-fixture.txt`
  后 exit0，证据 `repair_preset-final2.log`。首次失败未抹除。
- `veyra_capture_tests --rate-test capture:0:2:-1 0` exit0，
  `capture-key-reconnect.log`。MCS4K--T800 NV12 3840×2160 nominal30，
  先验证不存在的键被拒绝，再用故意无效的序号 999999 + 正确格式键打开、重连，
  两段各6秒，后段实测回调29.972fps，PTS单调。PS5关闭，此项不是游戏信号画质测试。
- `veyra_fg_presentation_tests <out> <app/runtime/experimental> [temporal|temporal-exact]`，
  三种运行结果如上，诊断 GPU→CPU 读回不进入产品实时路径。

### 追加功能回归

- `veyra_fg_settings_tests <p001.mp4> <output> backend-switch` exit0，
  XeSS4→DLSS4→XeSS4→XeSS2→XeSS4→DLSS6→XeSS4 均实际恢复生成并退出。
  不带末参数再跑 exit0：DLSS 2/6/关/6、暂停 seek、恢复历史、退出通过。
  日志 `backend-switch.*.log`、`lifecycle.*.log`，各上限180秒，实际各约数秒。
- 同一实卡 `--rate-test capture:0:<格式>:-1 0`：
  格式4 NV12 2560×1440 nominal60，回调58.691/58.664fps；
  格式22 YUY2 2560×1440 nominal50，回调50.004/50.004fps；
  格式38 P010 1920×1080 nominal60，回调57.159/57.286fps。
  三项打开、缺键拒绝、旧序号被键覆盖、重连和 PTS 单调检查均通过。
  原测试有5%回调容差，所以 exit0 **不等于两项60模式实测稳定满60**。
  不拿本机设备替代 GC573，更不把未开 PS5 的输入当实际游戏验收。
  证据 `capture-mode-{4,22,38}-corrected.*.log`。
  首次 mode4 通过 PowerShell CLI 传数组时参数合并，测试走错入口 exit3；
  改用 PowerShell 脚本直接数组调用后通过，保留首次日志，不改产品掩盖。
- `veyra_yuy2_color_tests --4k` exit0，四种601/709与范围组合，
  源颜色最大误差1个8bit码值、1:1呈现误差0；交替单像素亮度线条保留。
  `yuy2-4k.*.log`。这排除该合成路径的额外亮度模糊，不是 PotPlayer 同输入实测。

### 六组实际视频回归

命令为 `scripts/acceptance/fg-utilization-matrix.py --exe <app/veyra.exe>
--native <用户p001.mp4> --derived <nr-fg-followup-20260921/p001-derived1080.mp4>
--output <matrix-final> --temp <本轮tmp> --seconds 30 --cases` 加下表六个 case。
每进程30秒、45秒额外 watchdog，顺序执行，六项均 exit0 / failed=false。
RTX5070，NR实时内部1920×1080，真超分使用 RTX Video SR quality3。
EXE SHA256 `29C0E9A6693EC46A44F6F362999CE8583535E2BE96323608F2957AB3854B8139`。

| Case | 有界追踪末段秒数 | 提交/源提交每秒 | 间隔P99 ms | NVML平均GPU |
| --- | ---: | ---: | ---: | ---: |
| native-nr-dlss2 | 9.742 | 120.001 | 8.883 | 60.97% |
| native-nr-dlss6 | 5.367 | 287.329 | 16.790 | 90.37% |
| native-nr-xess2 | 13.633 | 60.001 源提交 | 17.439 源间隔 | 61.75% |
| native-nr-xess4 | 13.634 | 59.998 源提交 | 17.566 源间隔 | 88.34% |
| sr-nr-dlss6 | 7.750 | 148.393 | 17.103 | 91.07% |
| sr-nr-xess4 | 19.031 | 39.988 源提交 | 40.501 源间隔 | 70.60% |

必须区分统计范围：NVML取10至29秒；追踪环被覆盖后的末段时长如表，
不是整段30秒均值。XeSS行只测外层源提交，不能把60当补后60，也不能
把source×multiplier直接声称物理屏幕FPS；没有新抓取逐输出/scanout。
原生 XeSS2/4 全运行计数分别1643/4929生成，真超分XeSS4为2475生成，
但累计计数不能证明这些帧均匀显示。

DLSS6原生/真超分末段分别78/328次>10ms空档，对应相同数量的real-only组；
这些组同时带拒绝和播种状态，不能将两类标签相加算两份丢帧。
末段没有已生成子帧丢弃记录，但全段周期计数分别有3/13张过期生成帧。
因此“平均数上升”“末段未丢生成”均不构成均匀6X验收通过。
真超分XeSS4仍约40源/s、周期累计544源预览跳过，重负载问题明确未解除。
本轮没有同期旧二进制交错对照，不能用287对历史268、148对历史138宣传收益。
未改变这两后端调度，不重跑已失败的全局放行/固定等待/缩队列等方案。

## 全软件收尾与停止条件

总台账所有编号均已给出处置；没有把“未动”写成“本轮修复”。
本轮接受 C2/H1 合同修复和回归覆盖；先前已验收的 P010 上传优化继续保留。
D1/D2/X1–X4/F1 等历史性能/闪烁问题**没有攻克**。现有证据既不能证明
全是硬件极限，也没有确认一个未尝试、可删除且无副作用的具体阻塞点。
依用户的有限尝试要求，本轮停止性能实验，不继续靠改参数烧测试时间。

GC551/GC573/Elgato、30/40/5090、真实HDR/跨屏快速移动未进行本轮实机验收。
没有重测所有压缩codec、完整大MKV/音轨、导出组合、PS5/窗口采集或24/30fps全倍率矩阵。
共享增强图/导出/音频本轮未改，没有新增失败证据的既有修复继续保留。
本机采集无PS5有效游戏信号，不能比较其真实清晰度或端到端显示延迟。
独立降噪、XeSS/FSR输出限帧、原生Reflex+FG仍是未交付边界；
杜比视界/NR叠层保持暂缓，NVIDIA FSR4保持撤回。

没有新增发布包，没有合并main/推送/发布。最终仅本地提交及checkpoint；
git状态、提交号和关机交接在工作记录与CURRENT_STATUS登记。

## 验收原则

测试覆盖本次修改的失败分支，构建通过后运行现有相关回归；
性能同时看源覆盖、输出间隔和帧龄，不只看平均 FPS。
新代码未通过则回退到本轮存档对应状态，保留失败证据。
既有功能未改且无新反例时，不为宣称“全修完”进行无根据重写。
