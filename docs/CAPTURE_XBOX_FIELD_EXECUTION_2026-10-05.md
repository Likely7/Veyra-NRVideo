# 采集帧率与 Xbox 解码恢复：现场修复记录

本轮按用户最后确认的 `veyra-qml(28).log` 处理 Xbox 补帧后画面停止，
保留 `logs(4).7z` 的采集帧率诊断。26 是被更正的本地文件播放日志，不代替 Xbox。
日志、SDP 和转储只作证据；没有连接用户账号、Xbox 或真实 AMD 主机。

## 证据能够说明什么

- Xbox：RX9070XT，H.264 D3D12VA，1080p 输入，4K 工作尺寸。
  原日志第 2597 行首次报错，时间 `2026-10-05T06:35:07.516Z`；第 9341 行最后报错，
  `06:41:00.274Z`。487 条 ERROR 全部是
  `decoder: send_packet failed code=-22 text=Invalid argument`。
  FSR4 FG provider 4.0.1 的独立 context 在 `06:35:07.658Z` 有初始化记录，
  但解码错误继续；有声音和已建好的 FG context 不代表视频仍推进。
  一次 engine-stall 的 total 为 34528.187ms、tail 为 34124.415ms。
  这些证据确定了解码持续失败、宿主恢复缺口，不能确定触发 EINVAL 的 driver/FG/位流原因。
- 采集：RTX5080，2560×1440 RGB24，标称 60Hz，4K RTX SR + 1440p NR + DLSS3X。
  session2/revision16 的 59 个窗口，callback FPS 中位 60、GPU 完成 FPS 中位 55；
  source PTS 和 sample duration 中位 16.667ms。
  第 5565→6363 行的 55.608s，received +3337（60.009/s）、processed +3028（54.453/s）、
  mailbox dropped +308。源输入继续约 60Hz，消费处理欠速。
  滚动 GPU-ready P95 的窗口中位为 18.269ms，不能把 P95 当平均单帧耗时或把各阶段 P95 相加。
  软件提交/完成 FPS 也不是物理屏幕刷新率或屏幕端到端延迟。
- 7z 中另有 crash dump，保留原始证据，本次不对它作未经定位的根因归属。

输入复本与 manifest 在 `E:/项目/Veyra/logs/capture-xbox-field-20261005/input/`；
`capture-xbox-evidence.py` 校验原 SHA256 并写不含 SDP/凭据的 `field-evidence.json`。
原始日志和转储不进 Git、源码包或用户便携包。

## 修改与对抗检查

1. `XboxSessionSource.cpp` 的错误路径原先只请求关键帧。现在在硬错误以及压缩 AU 队列溢出后
   调用现有 `recoverAtKeyframe()` 清参考链、清待匹配元数据，并沿用 500ms 的关键帧请求限流。
   队列丢帧的断点挂在第一条幸存 AU；反复溢出会传递断点，连续输入不会凭空多次 reset。
2. `VideoDecodeRecovery.h` 给每个连接一份有界预算：单次错误清历史；三个连续硬错误才允许
   一次硬解重开；再次持续失败可回退一次软件。成功出帧只清连续错误，不补充重开预算。
   软件仍连续失败则明确结束，不循环挂着旧画面。NeedInput 不计入硬错误。
3. 首个恢复画面带 Discontinuity，复用共享图的 SR/NR/FG/flow 历史 reset；latest mailbox
   已有的 flags 传播保留此断点。原始 RTP、arrival、音频和序号继续，不重新编造一条 60fps 时间线。
4. 会话的 ready/end 更新原先可能覆盖 decoder 的 Failed 状态并清空原因，现在保留失败状态和信息。
   状态查询改读原子 `hardwareDecodeStatus`，不并发读取正在 close/open 的 worker-owned decoder。
   decoder 的所有操作仍在原解码线程；没有给逐帧增加 CPU fence wait 或像素回读。
5. `EngineController.cpp` 只把采集诊断的 pacing 条件从 `pairAnchoredLive` 改为实际 `pairPacing`。
   无 FG 的物理采集原本 ready-driven，日志却可能写 capture-pair；调度行为和帧率数字未改。
   Claude 已完成的 source-step cadence、丢帧相位和电源节流修复仍保留。

没有修改 NR/颜色/SR/补帧算法、协议、音频链或运行库字节。
新增及修改共 14 个作用域文件：上述三个产品文件，两个现有测试文件，五个
`scripts/acceptance/capture-xbox-*.py`，AGENTS/WORKLOG/本轮 PLAN/本记录。

## 构建与本地验收

工作树 `E:/项目/Veyra/worktrees/capture-xbox-field-20261005`，
分支 `codex/capture-xbox-field-20261005`，起点 `33d6685a3a75d3d4e8ac5d32996b163ef1f488dc`。
测试每进程 timeout≤290s，构建≤890s；仅普通负载，一个 GPU 测试进程一次，没有竞争/压力程序。
所有 TEMP/TMP 只对子进程定向到 E 盘本任务。

| 检查 | 命令与实际证据 |
| --- | --- |
| 生产构建 | `py -3.11 -B scripts/acceptance/capture-xbox-build.py build7`；QML app、Xbox、codec、scene、timing 五目标退出 0。继承 field-fixes/B 的 patched FFmpeg + dav1d、Qt6.8.3、静态 libass，保留已有第三方编译 warning，未把 warning 称为零。`logs/.../build7.log`。 |
| Xbox 本地协议及预算 | `capture-xbox-tests.py xbox1 xbox`：97 checks / 0 failures，包含新恢复策略/队列断点反例及本地 loopback WebRTC。未传 `--live-device-code`，没有远端登录。 |
| 既有场景节奏 | `capture-xbox-tests.py scene1 scene`：18 / 0，真实断档仍 reset，预览跳源帧不引起伪节奏断档。 |
| 既有采集/实时调度合同 | `capture-xbox-tests.py timing1 timing`：78 PASS，包含标称间隔、真实源 PTS、缺 B 输入保留旧完整 pair、mailbox 边界、实际 FPS 衰减等。 |
| H.264 真实解码 | 最终 `codec10`（无 B）/`codec11`（B2）均 ALL PASS 0；使用现有 GTA6 片段在 E 盘生成 1080p30/4s、GOP30 的本地夹具，明确传媒体参数，不接受 SKIP。验证 EAGAIN 重送、D3D11VA 反复 open/close、软/硬解、丢 AU 后关键帧的像素/PTS、坏包后的 flush、硬解重开与软件替换，并持有旧 D3D12 AVFrame lease 跨 close/open。无 B 输出120/120；B2 在未发送 EOF 的实时接口下输出118/118，完整 EOF packet-retry 对照120帧。两种后端恢复 IDR PTS 都是10000000。 |
| 完整便携包 | 最终 `smoke-AMD3` / `smoke-NVIDIA3` 均 PASS / exit0；干净 PATH、独立 `--data-dir`，MKV ASS events=5、内嵌字体 container=1，无 Qt/产品 ERROR。AMD 包在 RTX 上仅验证可启动及基础路径，不冒充 AMD NR/FSR4 推理。 |
| 生产补帧/NR 回归 | 最终 `hot-NVIDIA3` 43.828s / PASS：FSR3.1→XeSS2X→DLSS2X→FSR3.1→关闭，实际 provider 和源位置继续；再测试原版 NR 两层强度5、风格1自动/风格2手动及 PNG，Main.qml 注入在 finally 原字节恢复。实际软件提交读数均60FPS，非物理屏幕或真实Xbox读数。 |

以上脚本从本工作树运行：

```powershell
py -3.11 -B scripts/acceptance/capture-xbox-control.py
py -3.11 -B scripts/acceptance/capture-xbox-evidence.py
py -3.11 -B scripts/acceptance/capture-xbox-tests.py codec10 codec 'E:/项目/Veyra/tests/capture-xbox-field-20261005/recovery-h264-1080p30-b0.mp4'
py -3.11 -B scripts/acceptance/capture-xbox-tests.py codec11 codec 'E:/项目/Veyra/tests/capture-xbox-field-20261005/recovery-h264-1080p30-b2.mp4'
py -3.11 -B scripts/acceptance/capture-xbox-tests.py smoke-AMD3 smoke 'E:/项目/Veyra/test-packages/capture-xbox-field-20261005/Veyra-2.0.3-streamfix1-AMD-win64-portable'
py -3.11 -B scripts/acceptance/capture-xbox-tests.py smoke-NVIDIA3 smoke 'E:/项目/Veyra/test-packages/capture-xbox-field-20261005/Veyra-2.0.3-streamfix1-NVIDIA-win64-portable'
py -3.11 -B scripts/acceptance/capture-xbox-tests.py hot-NVIDIA3 hot 'E:/项目/Veyra/test-packages/capture-xbox-field-20261005/Veyra-2.0.3-streamfix1-NVIDIA-win64-portable'
py -3.11 -B scripts/acceptance/capture-xbox-package.py finalize
```

### 失败和测试判据修正

- `codec1` 因继承测试入口的 ANSI argv 无法打开中文绝对路径而 SKIP，runner 明确判失败；
  改为 E 盘独立 cwd 下的 ASCII 相对文件名，并 hash 验证夹具复制。
- `codec2` 的旧 12s 长 GOP/B 帧夹具不适合这个有界恢复验证；原断言还把当前输入包 PTS 当成
  重排序输出的 PTS。改为 GOP30 的多 IDR 夹具，按下一 IDR 自己的 PTS 检查，不通过改产品 PTS 取巧。
- `codec4/5` 想用连续坏包制造九次硬错误，但 NONKEY discard 会合法地将部分坏包变为 NeedInput。
  保留失败记录；CPU 预算测试验证连续错误决策，真实 codec 测试分别验证真实坏包后 flush 和
  decoder 替换/旧帧寿命，不把等待输入伪造成错误。本地坏包为 InvalidData，未复制 AMD 的 EINVAL 触发器。
- `hot-NVIDIA1` 以固定 5s 等待误判 DLSS 源位置停止；记录显示 context 创建 9293.573ms，
  断言退出前已经有真实/生成帧提交。改为 provider ready 后再观察位置的连续推进，保留 180s 总期限；
  不把首次初始化耗时当成永久卡死，也不因此改补帧算法。`hot-NVIDIA2` 已通过修正判据。
- 首次 staging 复制了旧 NVIDIA 包后来生成的五份本地配置。副本从本轮候选移至本轮私有 logs
  下 quarantine；原包和配置不修改。打包现在只复制原 manifest 列出的 payload，明确排除 user-data。
  refresh 曾因源包新增配置不存在于候选而中止，未以忽略运行库 hash 的方式放过；该错误已修复。

## 交付、存档与剩余缺口

新候选在 `E:/项目/Veyra/test-packages/capture-xbox-field-20261005/`，AMD/NVIDIA 同一个 EXE，
版本 `2.0.3-streamfix1`，最终 SHA256/源码 commit/所有 payload hash 以该目录 `DELIVERY.json` 为准。
生产 EXE SHA256 `0450db056d0611cb416d5594981fa1ac6e28128fc5b0e4ef5e1f14b96e1c0ddb`，
Main.qml SHA256 `d33df73b6f84efd7109e4dcfd428453740d04b9d8538e37800a19c49c98a4632`。
最终打包在上述检查通过后执行，不以本机短测称为已验真实主机。
本地验收存档标签为 `checkpoint/capture-xbox-field-code-verified-20261005`；
源码 ZIP、已验证的 final bundle 和两包 ZIP 的最终身份在外置 DELIVERY/日志中记录。
finalize 只有在 git clean、测试对应同一个 EXE、QML 原字节恢复、所有组件 hash 一致时才执行，
并验证 ZIP CRC 与每个 payload 的 SHA256。SOURCE/代码验收与真实 Xbox 未验边界分开报告。

继承的运行组件原字节：NVIDIA 215 项、AMD 686 项；没有新 proprietary runtime、模型、DLL 修改。
旧 AMD 模板早于 libass，补入同 EXE 的八份已审计 libass/字体依赖版权通知；
保留原依赖源码下载地址和 vcpkg `30ef65cad9` 的 libass 对应构建说明。
新 application source ZIP 只含 tracked 文件；final bundle 单独验证，原始媒体/日志/SDK 不进去。

开工 `source-before.bundle` SHA256 `6169096ddd64ad7a4728f8d348c41b3781fdac6b542060be40e7411c01a81f51`。
不可变 `start.json` SHA256 `c18ad29c8cc6664a408dbcefdf455a23d626d79196d21b40f32b371733926bd5`，
所有 guard 保持另外 18 个 worktree 的 HEAD/status/diff/未跟踪内容身份，包括桌面和 main。
产物分别在本任务的 build/tests/logs/tmp/test-packages/archives 目录，保留构建及必要失败证据；
没有新 merge、push、Release、关机或子 Agent。

本机 RTX5070 的短测不等于 RX9070XT + Xbox 真机验收。软件回退可能增加 CPU 使用和解码延迟，
耗尽恢复后远端会话实际结束及 UI 提示的真人验证仍未执行；只证明本地策略和 codec/resource 合同。
NR 风格保护效果、AMD NR 无效果不是本次第二项，不借本修复宣称已修。
采集没有做新的降低画质/倍率或调度优化，不保证原 NR+4K SR+3X 组合必达60fps。
下一步：受影响用户用 AMD 新候选重连 Xbox，按原先失败的 FSR/XeSS 切换复测，
仍有卡住时保留新的 `xbox-decode-recovery` 动作与恢复出帧日志。
