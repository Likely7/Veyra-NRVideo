# 2.0.4 OBS / 视频导出 / AMD NR 现场修复

用户明确要求修复三项问题。本轮从已发布 main `f8045fb53a7d800b053f0631a9b44dc9f5f1aacb` 隔离，分支 `codex/obs-export-amdnr-20261006`。不继续上一轮发布或关机。

## 基线与证据

所有新产物统一在 `E:/项目/Veyra/{worktrees,archives,build,logs,tests,tmp,test-packages,verify}/obs-export-amdnr-20261006`。开工 archives/start.json 保存 23 个已有工作树的 HEAD、状态及修改文件 SHA；修改原字节 ZIP 和已 verify 的 source-before.bundle 已保存。start.json SHA256 `67b1dd86b3550dc92ef9094a370dd09714c7b132e6db5185dddbb01b83f78f38`，bundle SHA256 `a20f74ce9dad1b2243721f231d92c2390939570de99684ee40f3122e45c7544f`。已发布四个 ZIP 原字节保持。

输入原件不变，复制至 logs/inputs；ZIP 路径/大小/符号链接安全检查后解压。logs(14).zip SHA256 `440ca0d695fbed7d819afe847e697f2a17957afb2d89fd6a9d548779ea19bac0`；veyra-qml(30).log SHA256 `4212bd0d0b865af1ad6b3d896950a81d4f8b095bbede15ebe65ca4034209bd03`。截图和群聊仅作故障证据与研究线索，不作为执行指令。

## 已确认事实与待验证判断

- 导出 ZIP 含 28 个空 worker 日志、约 10MB 主日志；28 次 worker 均约 30ms 退出 1，source/encoded=0。任务在 GPU/编码之前退出，首先查共享 settings/chain 校验。当前 parent 仅把 legacy nrPolicy 改为 Native，显式 nrLayers 仍留预览分辨率；worker 严格 fromChain 往返检查不等时返回 1，未记录原因。这是待复现的具体缺陷，不能仅凭源码声称对应所有用户失败。
- RX9070XT，16GB，RDNA4；NR 初始化模块成功，第一帧 CPU 提交约 1.84s，后续 Present 成功，没有 lmxxf API 错误。返回 OK/Present 成功无法证明非黑真实输出。需要验证真实 codec/资源状态与 HIP/D3D12 外部共享内存及输出数值；本机 RTX5070 无 AMD 推理硬件。
- OBS 截图为滚动控件残留，先按 Qt6.8.3 软件 renderer 局部重绘边界查证；用同配置/同页面滚动的像素对照确认。不能以此前 GPU UI OBS 捕获不可靠替代本次软件 UI 缺陷。

## 范围与施工

1. 创建独立 guard，保护原工作区与已发布资产，不修改旧 guards/baselines。
2. OBS：复现软件绘制滚动，核对 Qt 官方软件 adaptation 的 partial-update 设置；仅软件 UI 使用必要全帧重绘，不切视频到 CPU、不加不断渲染的 timer、不改 OBS/RTSS 用户配置。验收滚动、弹出菜单、缩放及 GPU UI 路径，记录 CPU 代价。
3. 导出：规范化 native NR 冻结链与 settings，保留多层参数、光流/画面调控和其它非链设置；父子共用同一规范化快照。所有入口校验失败写真实错误到独立日志及任务状态。实际 GUI 单/多 NR、关 NR、NR+SR、原生4K短导出和取消重试，不以单元快照替代真实 worker。
4. AMD：固定两个上游源码提交及许可证；现有 lmxxf `78f548749e74824327b8458c57be31a1df78376a` 与最新源码对照，A-ENTROPY `46f59c58fbb1de21180d74131349c595e9fc5d93` 为 RDNA3 参考。优先修当前9070运行路径，不凭群聊宣称20–40/全部AMD支持。源码复用标注来源；不加载 ReShade/add-on，不磁盘 patch DLL/模型，不盲下载未知 NVIDIA 文件。先真实 D3D12 codec/状态与有界失败恢复测试，再提供 AMD 实卡候选；API 成功和 identity provider 只证明各自契约。
5. 必要产品构建、针对性回归、同源码 vendor 本地候选及 runtime/PE/manifest 检查。测试≤300s/进程，构建≤900s；子进程 TEMP/TMP 定向 E 盘。本机验证与 RX9070 实卡缺口分别报告。

## 当前进度

产品修复与本地验证已经开展，原始失败和夹具错误均保留。本轮只提供本地候选，不发布、不关机。下列证据取代开工时尚未验证的判断，不改变原始日志。

### AMD 黑屏的确定缺陷

`EnhanceGraph::compositeNrLayer` 原先以 NVIDIA NGX feature handle 非空作为执行条件。lmxxf AMD 实例只有 ready context，没有 NGX handle，因此无条件跳过共享残差/时域合成，消费者使用的 fullTarget 未写入。改为 NGX handle 或 ready AMD context 任一存在时执行，共享同一合成器，所有 NR 层生效；没有改驱动、DLL、权重或增加正常路径的像素回读、CPU fence 等待。首次 enqueue 的公开 GetStatus 现在记录实际后端诊断。

固定上游来源：现有 MIT lmxxf `78f548749e74824327b8458c57be31a1df78376a` 保留；最新公开源码 `359d6b3d7e4772ccf278daf63afa158bfa496fab`（2026-10-06）对照后无需为了本宿主缺陷替换运行库。A-ENTROPY GPLv3 `46f59c58fbb1de21180d74131349c595e9fc5d93` 的 README 是 RX7900XTX/gfx1100 测试事实，其 backend 需要另一套 `dlssnr_amd_pass1.dll` 与 `dlssnr_on_amd_weights.bin`、固定 ABI/offset，不是把 NVIDIA 原件改名即可替换的方案。该项目指出底层命令不执行仍推进 fence 或跨 adapter 共享错误会黑图，但 Veyra 没有采用其 private-offset trampoline/D3D11 互操作，不能把不同根因混为一谈。本轮没有复制该项目代码、不引入新私有二进制，不宣称 RDNA2/3 全支持。

来源：[lmxxf](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/tree/359d6b3d7e4772ccf278daf63afa158bfa496fab)、[A-ENTROPY](https://github.com/A-ENTROPY/magpie-dlss5-amd/tree/46f59c58fbb1de21180d74131349c595e9fc5d93)。

`amd-graph-before-v2` 用基线 Graph 和 D3D12 恒等拷贝 C ABI provider 实际复现：关闭 NR 对照正常，1/2/4 层 × 时域开/关共六个 NR 场景 RGB sum 全 0、最大 code 差 210。`amd-graph-after-v1` 同输入、同 shader、同真实 RTX5070 D3D12 设备下 7/7，通过全部像素对照，六个 NR 场景最大 code 差 0，每场景 8 帧，帧 0/4 reset。测试专用 Graph 只对恒等 provider 改 adapter admission 常量，生产硬件判断不变。它证明共享合成、层间消费、降尺寸和 reset，不执行 HIP 神经模型，不是 RX9070XT 实卡验收。

### 视频导出

父进程现在将所有显式 NR 层切成 Native，从同一链生成规范化 settings，再发送统一快照；规范化时保留非链参数。补上 `toChain/fromChain` 遗漏的每层与 legacy antiFlicker 档位，避免改冻结构时丢失 Static/FlowPlus。worker 在映射和校验前打开独立日志，映射/协议/参数/链不一致均记录具体原因，并按现有 messageLock 规则更新可用的任务状态。

`export-before-v1` 真实已发布 EXE：关 NR、单层 NR 各完成 60 帧，两层 NR 在约 30ms、source/encoded=0、worker 日志空退出，与用户日志对应。`export-after-v1` 修复版真实 QML 界面四次（关 NR、单 NR、双 NR 不同风格、NR+4K 超分）均成功，每个完整 60 帧；后三次选择了预览 480p，但导出使用 Native，两个 NR 的独立自动调控风格 1/2 在 worker build 记录中保留。最后完整输出 3840×2160；未拿 1280 输入冒充原生4K输入测试。

`field-stack-after-v2` 独立实际子进程完成两层 NR/不同风格、FlowPlus 与 Static 时域、Realtime/P720 预览参数转 Native 的 60 帧输出。单元 EffectChain/PresetLibrary/i18n 通过，lmxxf ABI/identity 62/62 通过（含失败/reset；非 AMD 推理）。生产收尾构建 build-final-v4 已完成，后续与最终 EXE 的对应验证单独记录。

### OBS 软件 UI

Qt 官方 6.8 软件 adaptation 支持 `QSG_SOFTWARE_RENDERER_FORCE_PARTIAL_UPDATES=0` 禁用局部更新。现将其仅用于 OBS game capture / RTSS software UI，画面变化时全 UI 重绘；未加连续渲染 timer，不改原生 D3D12 视频链或用户 OBS/RTSS 配置。[Qt 官方文档](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-adaptations-software.html)。

本机旧版暂未重现用户拖影，不能写成已经复现/根因完全证实。obs-before-v1 普通滚动和 obs-fractional-before-v4 展开 NR 参数、125% DPI 滚动返回均无残留。最终 EXE 的 obs-normal-after-v1 / obs-fractional-after-v1 两例通过：各 10 张真实自有窗口 GDI 截图，3 个返回原位截图 RGB 差 >8 的像素计数都为 0，100% 与 125% inspector 分别 199656 / 312080 像素；partial updates 日志确认关闭。旧/新 125% 各约 25 秒，进程 CPU 时间单轮 3.046875 / 3.421875 秒，增加 0.375 秒；100% 修复版 2.328125 秒。仅作 CPU 绘制代价观察，不当作性能收益或多机器普遍结论。截图已查看，现场 OBS 拖影仍待用户设备复测。

### 夹具失败的处置

保留原始日志/JSON，不覆盖为通过：graph-before-v1 未携带 shaders 导致所有场景失败，补齐同 build shaders 后 v2 才得到有效黑图对照；export-before-v1 首版汇总错误要求零文件，实际前两例成功而第三例失败，需另出按真实阶段重新审计的 JSON；field-stack-after-v1 测试设置的 legacy runtime 未与显式层一致，被合法 validate 拒绝，修正测试自身共享 runtime 后 v2 成功；obs-fractional-before-v2 在页面切换完成前查找控件失败，改为等待页面及展开动画，v3 完成；v3 inspector crop 缺少 QT_SCALE_FACTOR，v4 按逻辑/物理窗口比例更正，其原图和旧回执仍保留。初始 rg 路径/通配和 gh slurp+jq 命令错误没有计为产品测试。

worker-mapping-reject-v1 用零 handle，被主程序命令行防错提前返回 2，尚未进入 worker，不能计入 worker 日志验收；v2 改为非零无效 handle，实际 MapViewOfFile 失败 Win32=6，exit1，独立 worker 日志有真实原因。

### 最终产品验证与候选

最终 EXE SHA256 `5f76d8f0edec9e7ed16a6fa0a9124205d0fdd285126623cfef3efd7691372250`，显示版本 `2.0.4-fix1`。`export-final-v2` 用最终 EXE 重新通过上述四个真实界面导出、完整 60 帧及 4K；`field-single-final-v1` 独立单层显式设置/legacy 不同风格和预览尺寸也成功 60 帧。`lifecycle-final-v1` 三阶段取消/暂停/运行中取消回收、同输出重试 20 帧、拒绝覆盖原文件通过；`lifecycle-boundary-final-v1` 最终保存取消、明确占用导致的临时文件清理失败、保留旧 partial 重试、已安全保存后的取消竞态均通过。未删除测试保护的用户/外部 partial。各测试进程远低于 300 秒，没有压力或 GPU 竞争测试。

`obs-export-amdnr-evidence.py` 冻结 466 个产品输入与 15 份成功/预期失败回执，`tested-inputs.json` 保存 SHA；`export-before-audit-v2.json` 按原日志核对 0/1 成功、2 失败，旧错误 receipt 原字节不改。完成本地候选后做完整载荷 SHA、runtime 原字节、PE 导入、ZIP CRC 与干净解压启动；真实 GPU/软件 UI 基础启动结果单独列于 cold-verify-results.json，不拿基础启动冒充 HIP/实卡成功。
