# FRUC 与高帧率接入 — 2026-09-09（Cycle87–88）

## 当前范围

本机可选实验后端。专业模式 → 补帧 → 选择「NVIDIA FRUC · 视频补帧」，下方选择关闭 / 2X / 3X / 4X；实时应用。DLSS后端保留，默认仍为DLSS；还原默认关闭补帧。后端随预设保存，预设schema4兼容旧1–3，旧版默认DLSS。只选FRUC但关闭补帧不启动辅助进程，也不要求FRUC DLL可用。

共享顺序为 source → SR（DLSS或RTX Video）→ NR → FG（DLSS或FRUC）→ 呈现/导出。三入口仍使用同一EnhanceGraph，没有复制NR/颜色/指导实现。光流档位配置NR/DLSS共享NVOF，不配置FRUC内部算法；FRUC公开接口没有同等质量滑条。

## 1000fps 的准确含义

呈现原本没有60fps硬锁，Present采用非vsync路径；发现并放宽的是采集间隔估计和CFR导出准入中的120fps政策上限，现在允许到1000fps（1ms源帧间隔）。PTS连续性/VFR拒绝等完整性判断保留。显示器刷新率、解码、GPU预算、驱动呈现和编码器支持仍分别限制实际吞吐。

实际 `app-1000fps.log`：256×144、1000fps、2秒合成H.264，关闭NR/SR/FG，完整处理2000源帧，processedFps=999.34，failed=false，source读取与处理未锁60。不是4K千帧、开启增强千帧或屏幕物理显示1000幅/秒的证明。1080p/4K普通视频仍走当前软件解码，没有借本次改动宣称NVDEC已接入。

## 官方接口与实现

依据NVIDIA [FRUC guide](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html) 与用户本机Optical Flow SDK5.0.7头文件独立实现，没有复制其他项目/SDK sample实现。

- 接口输入当前帧并缓存上一帧，指定任意两帧间目标时间戳；同实例禁止把同一输入重复调用来伪造多倍补帧。
- SDK没有2X/3X/4X枚举。Veyra用1/2/3个独立FRUC实例，分别算1/2，1/3+2/3，1/4+2/4+3/4位置。每个实例每个源帧只调用一次，生成帧不递归当源帧。
- 仍必须等B到达才能在A/B之间补帧。源15fps的一帧lookahead约66.7ms，不能拿9ms阶段耗时冒充端到端延迟。
- Windows接口D3D11。父进程D3D12共享RGBA8纹理/共享fence，子进程同adapter LUID的D3D11打开它们。输入copy → fence signal → FRUC依次wait/signal → 父D3D12 queue wait → output copy。正常路径没有GPU→CPU像素回读。
- `veyra_fruc_worker.exe`为本项目源码构建的隐藏辅助进程；只继承白名单内存映射/event/纹理/fence句柄。共享内存只含控制参数/状态/重复标志，无像素。Job Object限定子进程寿命，退出时只终止本次拥有的worker。
- IPC等待的是CPU API返回/提交确认（10秒故障上限），GPU完成靠共享fence。FRUC SDK本身可能阻塞CPU，此路径不宣称SDK是完全异步的。
- 每个FRUC实例创建后记录SDK留下的CUDA上下文，调用前切换，之后恢复；直接省略上下文切换会出现result16。
- SDK重复回退标志映射GenerationValidity::Disabled，不计为有效生成帧。导出沿用原有明确hold计数；不把hold写成真实补帧。

本地DLL身份：`NvOFFRUC.dll` SHA256 `5A0B6701D30709E25E7E5B92CA46B18AAB1459160CECD4F629872369D85C8B0A`，783416bytes，Valid NVIDIA签名。仅从忽略的本机SDK绝对路径加载，不复制/上传/分发；CUDA使用System32绝对路径。依赖协议和分发权仍需审计。

## 重建失败证据与处理

尝试同进程方式时，首次2/3/4倍生成能成功，但销毁后重新注册返回4（InvalidHandle）；先创建替代实例再销毁旧实例会在下一次warp出现0xC0000005。正常上下文恢复、逆序销毁、保持模块驻留未解决重复注册失败。没有继续用同一失败指纹无限尝试，没有修改或替换SDK DLL。

最终改为每次完整重置启动干净worker进程，保留GPU共享纹理。测试覆盖连续3次reset且每次reset后再次process，不止检查seed帧。`bSkipWarp`只承诺更新状态并跳过warp，没有证据证明其清除全部光流历史，本轮不将它冒充完整reset。

代价：seek、scene-cut、capture-drop、参数历史reset会有worker重建成本。尤其当高倍率超过源帧预算、采集持续掉帧时，可能反复重建并恶化卡顿。这是本轮已知实卡风险，未宣称可用于任意4K/60卡的稳定4X。后续优化应先对比重置后下一对像素与fresh worker一致性，再决定可否轻量reseeding，不能直接去掉reset门槛。

## 实际测试（Maker）

命令均单次≤300秒；本轮常见8–60秒上限，无实卡占用。

- 构建：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root <project> -Preset x64-release`；最终源码候选 `logs/video-sdk-trial-20260909/build-fruc-lazy.log`。
- CPU：`veyra_live_timing_tests.exe`16项、`veyra_repair_contract_tests.exe`27项、`veyra_repair_preset_tests.exe <独立临时文件>`全部通过。首次preset漏传路径exit2为调用错误，补传独立路径后通过。
- GPU像素/PTS：`veyra_fruc_tests.exe 2|3|4 logs/video-sdk-trial-20260909/pan.nv12`，已知8pixel/帧平移6源帧，分别5/10/15有效生成帧；最大RGB8 MAE约0.00008 / 0.29943 / 0.14638，均优于保持上一帧；各子帧PTS误差≤1个100ns tick；连续3次reset后无跨epoch生成，下一次process成功。日志 `fruc-worker2/3/4.stdout.log`。这里只证明构造平移样本，不能推断遮挡/字幕/自然场景总体画质。
- 真播放器known-pan15：2X为60源+59生成；3X为59源+116生成；4X为57源+168生成，各failed=false。日志 `app-knownpan2/3/4.log`。FRUC阶段区间中位数约9.22 / 18.36 / 27.13ms，P95约11.69 / 22.86 / 33.92ms；这是GPU时间戳包住共享队列等待/IPC提交的阶段区间，非纯CUDA核耗时、非端到端延迟。
- 正常testsrc2并非每帧满足SDK质量门槛：`app-fruc4-controls.log` 和 `app-fruc2-srnr.log` 成功执行但generated=0（repeat回退），如实保留。这说明打开倍率不保证每对源帧都有有效生成，不能只看API成功码。
- SR→NR→FRUC：`app-fruc2-srnr.log` 实际执行VSR、NR和FRUC，87个NR帧，无失败；该样本重复回退不作为生成画质通过证据。
- UI实时切换：`python scripts/acceptance/ui-fruc-switch.py`；2→3→4→DLSS→FRUC→关闭→2→还原默认通过，未更改用户启动偏好。Maker日志 `logs/fruc-switch-ae7549842be3415b937af9e0ac6a307d.log`（最终lazy创建变更后需独立重跑）。
- NVENC D3D12导出：`veyra.exe --export-out <新文件> --max-frames 12 --no-nr --no-sr --fruc --fg-multiplier 2|3|4 <known-pan15.mp4>`。分别12源+11生成+1尾部hold=24帧/30fps，12+22+2=36帧/45fps，12+33+3=48帧/60fps；ffprobe均1920×1080、0.8s。日志 `export-fruc2/3/4.log`、`.ffprobe.json`。该素材无音轨，本轮FRUC导出不单独声称测过有声源；已有统一gate继续验证既有完整性，不加重检查。

## 尚待/限制

独立只读复核与preflight/phase7结果见下方追加记录。整体Phase7仍in_progress；自然视频主观质量、实卡FRUC延迟/掉帧、4K高倍率持续性能和长时间稳定仍未验收。NR表盘依旧仅NR阶段，旧统计窗口混合配置问题不在本次修复中。不得公开打包专有runtime。


## 独立检查与用户反馈补充

独立review_fruc已报告CPU16+27项/preset、实际2/3/4pixel/PTS/连续reset、UI全后端切换和自有worker进程故障测试通过，无新增已确认P0/P1/P2消息。preflight69 PASS；其实际phase7日志 `logs/video-sdk-trial-20260909/review-fruc-gate-phase7.log` 为73 PASS，delivery `logs/delivery/1c195ef6d3684879b220fc8c13439e83/result.json`。Reviewer随后额度耗尽，未返回最终整体结论；因此记录为independent_checks_passed / final_verdict_unavailable，不伪造最终Reviewer PASS。用户随后明确“我已经测试没问题”，记录用户本次体验通过，不外推其未说明的配置与长期稳定性。程序SHA256 BD76BA1F3D66DF30286E450AD1742E773F48989B5815C175BDC0A297ABF4E94D；workerSHA256 B6F267C3B1C73DE0CB99C4BAD4B5DA9355C42DF7E7F37F21C9891B02B80412A3。
