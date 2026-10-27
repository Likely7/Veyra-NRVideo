# 2026-10-09 本地 main 修复整合与 HDR 兼容开关

用户先授权“先把目前修复合并到 main”，随后要求 HDR 补帧闪黑兼容做成设置页开关、默认关闭。本轮完成这个选择后整合已完成分支；未推送、发布、更改版本或关机。旧 UI/engine 冻结由本轮具体授权覆盖，没有继续其它历史实验。

## 合并范围

合并前 main `dbfceabee804cd81bac4a4781f0b7b4cf272902e`，工作树 `E:/项目/Veyra/worktrees/main-merge-20261002`。四次 `--no-ff` 合并保留原作者/提交与分支：

| 分支 | 原分支 tip | 合并提交 |
| --- | --- | --- |
| codex/fg-source-flash-20261008 | 3fd3cde2 | 52d83eaf |
| codex/nv-vram-field-20261008 | a210ec18 | 49c9e95c |
| codex/blackmagic-followup-20261009 | 886b461b | 083d6785 |
| codex/fg-flash-fix-20261009 | f6a8fa27 | 2caa8dca |

前两个只有隔离诊断脚本/头文件和记录，不把像素取样或显存追踪加入正常 GUI。BMD 只改原生接收协商及其定向测试；HDR 候选 ce50d18 的自动启用已由 f6a8fa27 改为用户选择。产品合并节点为 `2caa8dca928d50634dd301622aa2da69ab336048`；之后仅修组合验收脚本和归档文档。

冲突只涉及 AGENTS.md 和 WORKLOG 的追加/前置记录。解析 Git 三阶段，要求完整旧正文仍逐字存在，再保留两边新增段落；没有整份取 ours/theirs。首次保护器只接受前置记录而拒绝 WORKLOG 末尾追加，停在冲突态，检查后扩展为同时支持前置和追加，再继续。未覆盖其它工作树。

## 最终行为

设置 → 兼容 → **HDR 补帧闪黑兼容**，保存为全局 `hdrFgCompatibility`。首次启动及旧配置缺字段均关闭，选择可跨重启保留，不进入增强列表预设或导出设置。播放中切换通过原 presentation revision 在 GPU 所属线程执行；仅切换时排空呈现队列。关闭释放额外合成纹理。

默认继续直接呈现，没有额外兼容纹理或复制。开启只适用于 NVIDIA HDR10 应用侧补帧；SDR、非 NVIDIA、无补帧与 XeSS 保留原路径。适用时单张 GPU 纹理合成后复制到交换链，4K 实际分配 33,423,360 字节（31.875 MiB）；可能增加 GPU 带宽/耗时，切换可能短暂停顿。不增加逐帧 CPU 等待、像素回读、睡眠或新帧队列。分配/排空失败有日志和实际 effective 值；没有做 OOM 压力测试。

## 合并后构建与定向回归

执行 `py -3.11 -B scripts/acceptance/hdr-fg-toggle-build.py --combined`。按原 Ninja 头依赖重编 13 个生产消费者（其中 8 个 engine）、新翻译资源、2 个 BMD source 和各测试 fixture；GUI/原生/GPU 测试使用对应的新 engine/source 对象。只查询原缓存，不配置/运行 Ninja 构建；原 objects/lib/EXE/manifest SHA 保持。最终构建 69.141 秒、exit0，回执：

`E:/项目/Veyra/logs/hdr-fg-toggle-20261009/build-20261009-113817/receipt.json`

| 产物 | SHA256 |
| --- | --- |
| veyra_qml_ui.exe | 4e0f8193afbbbe7889ce019b25999d6af60f60e375d24aa774aed5fa85a0c0e9 |
| fg-presentation-tests.exe | 5b3391771177ce80b09de4b711a0dd6a618f07b3b6b940ae219ec19ea941ced9 |
| capture-contract.exe | 358782bab947a02a64d7669cc3b8987630b6d0023e4e6386f2898c360f9be429 |
| capture-gpu.exe | 18439d7ba53b89311113b5b0dc64a9d997b546e4098002587f645859a887b358 |

RTX5070 / 616.56，同一私有 app，测试串行，无竞争/压力负载。实际命令为 `py -3.11 -B scripts/acceptance/hdr-fg-toggle-test.py` 加下列参数：

| 参数 | 实际结果 |
| --- | --- |
| capture merged-contracts-final contracts | 261 项、0 失败，0.032 秒 |
| capture merged-pixels-v2 pixels | 124 项 GPU 检查，HDYC 两方向与 UYVY BT709 相同，3.422 秒 |
| native merged-hdr-toggle hdr-4k | 512 原帧/620 生成帧、94 次窗口无损对照、最大 10-bit RGB 误差 0、D3D12 错误 0，18.859 秒 |
| gui merged-settings restore-off | 真实 QML/bridge 恢复关闭、保存/其它显示设置保持、无错误，7.922 秒；稳定设置页截图已查看 |

HDR 包括默认关闭、启用→关闭→再启用、窗口/全屏、resize/reset/销毁；4K 是交换链缓冲，源 fixture 为 640×360 P010，不冒充 4K NR 或真实 HDR 屏幕验收。合并前相同 HDR/bridge/QML 产品源码已通过新/旧配置默认关闭、开关保存/跨重启、英文内置资源、HDR VFG 与 SDR 回归，详见 HDR_FG_COMPAT_TOGGLE_REPORT_2026-10-09.md。两个现场启动器实际各运行 15 秒，Compatible/Direct 分别写私有偏好 true/false，exit0、未强停、原配置 SHA 保持、ZIP CRC 通过。

首次组合检查的失败保留：契约 fixture 主动制造 6 条拒绝错误，通用日志扫描误判失败；现同时核对准确日志次数与相应 PASS 拒绝断言，只豁免这些预期负例。GPU fixture 首次漏链接新版 VideoPresenter，混用旧类实现而访问违规；补齐全部受影响 engine 对象，构建脚本一次“4 个 engine”计数假设也在链接前被保护断言拦下，改为实际依赖集合。重新构建后 GPU 124 项通过，GUI 产品逻辑没有为这些测试失败修改。第一次契约回执对应重链接前 EXE，最终又对实际最终 EXE 重跑，不用旧 SHA 的通过冒充当前验收。

`scripts/gates/delivery.ps1` 明确只接受 legacy UI，本次没有用它替代 QML 定向验收。没有宣称新一次全功能、AMD、采集卡现场或长稳验收。

## 保全与边界

新产物统一在 `E:/项目/Veyra/{build,tests,logs,tmp,reports,archives}/hdr-fg-toggle-20261009`。只保留一个共享私有 app（依赖硬链接、EXE/Main.qml/SettingsPage.qml 独立副本）、当前构建和必要成功/失败证据，没有制作新的整套便携包。1614 项 donor 文件逐项 SHA 前后不变；正常 GUI 未引入诊断探针。源码合并没有 DLL/SDK/模型/媒体或其它二进制变更。

开工不可变基线 `archives/hdr-fg-toggle-20261009/start.json` 覆盖 39 工作树/495 脏文件，除获授权 FG 和 main 外，其余 37 工作树受保护。前后 checkpoint 为 `checkpoint/pre-merge-field-fixes-20261009` 与 `checkpoint/merged-field-fixes-20261009`，开关验证节点 `checkpoint/hdr-fg-toggle-validated-20261009`。最终提交/源码匹配、构建和每项测试 SHA、逻辑目录占用及保全结果分别记录于 reports 同任务目录的 `closure.json`、`preservation-final.json`；逻辑字节含硬链接，不冒充实际新增磁盘占用或卷空间变化。

HDR 现场根因未确认，RTX5090 尚未回测当前兼容开关；BMD 实卡黑屏、AMD/BMD 联用及屏幕端到端延迟尚未验；NVIDIA 显存持续增长仍未定位。不能将这些本机定向通过称为现场问题根治。版本仍沿用 2.0.6/VFG 本地基底，未推送、发布或更新 Release。
