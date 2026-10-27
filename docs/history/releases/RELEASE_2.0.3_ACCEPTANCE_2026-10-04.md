# 2.0.3 显卡分包与现场修复验收

> 2026-10-04 纠正：按实际 RTSS.exe 的 FileVersion/ProductVersion 与 SHA256 核验，本文原先的 7.3.7 版本记录不准确；当前用户安装与此前测试副本均为 7.3.5.28314（84E6E439…6736）。本轮另外发现旧测试只退出主 RTSS 而漏收子加载器，导致用户误提示；详见 RTSS_RESTART_LOOP_PLAN_2026-10-04.md。本条纠正版本和清理记录，不宣称重新完成本文全部旧测试。

用户授权分别发布 NVIDIA/AMD 包、保留不可用功能的灰色入口、写入 AGENTS.md、合并当前已完成修复到 main 并发布 GitHub 2.0.3。追加决定保留现有 FSR3.1/4 共用组件；旧 SDK 分离试改已撤下。HDR/Dolby Vision PR #13/#14 不在本版施工范围。本轮不重复此前已经执行的关机。

## 源码与分包

先存档 `checkpoint/pre-release-2.0.3-20261004` 和已核验 Git bundle，再在 `codex/release-2.0.3-20261004` 修改。保留远端 main 的 README 提交 `712daf3`，不覆盖桌面原 checkout 的用户改动。产品构建在 `E:/项目/Veyra/build/release-2.0.3-20261004`；所有测试、日志、临时文件、归档和发布包均在同一 E 盘任务目录。

两个包使用同一 EXE/QML/shaders。NVIDIA 不含 AMD NR/HIP 模型和内核；AMD 不含 NVIDIA NR/DLSS/RTX SR/HDR/VFG/CUDA 专用库。两包保留原 FidelityFX SDK 2.3.0 loader/upscaler/framegeneration 和跨厂商 XeSS；NVIDIA 的 FSR4 ML 入口置灰。不复制显卡驱动 DLL。发布者按固定身份审计组件，软件仍允许用户替换运行库。

NR 保留四个版本入口，SR 保留三个算法入口，FG 保留五个后端入口。统一按实际主适配器与包内文件判断；列表、节点和菜单显示灰色及原因，直接设置也拒绝不可用请求。旧 session/preset 关闭不可用阶段，保留版本、倍率和质量参数；受支持的 FSR3.1 可在 AMD 包内正常使用。未拿模拟 GPU 测试冒充实卡推理。

## 本轮实际证据

以下路径均相对 `E:/项目/Veyra/logs/release-2.0.3-20261004/`。硬件为 RTX5070 / 驱动 616.56，测试子进程均不超过 300 秒；压缩和构建另设上限。

| 检查 | 结果与日志 |
|---|---|
| 产品构建 | fresh 构建及最终增量通过，最终 `build-v8.log`；生产 EXE SHA256 `998d42617e0455523b6f39741ec82206c4685f043794a5235c2853f8c5a98154`。 |
| 硬件规则与 UI 基础 | 56 硬件矩阵检查、331 VFG 设置检查、39 Qt Quick 测试、多语言与 NR preset 持久化通过，见 `unit-v1` 的成功项与 `unit-v2` 修复后结果。 |
| Xbox | 本地 WebRTC/ICE/DTLS/SCTP/RTP 90 checks；48k stereo float WASAPI 输出启动；AMD ABI GPU-copy 62 checks，见 `unit-v1`。音频测试 gain=0，不证明真主机有声。 |
| 原 FSR2.3 组件 | 独立 FSR 39 samples/39 中间帧，meanError=0、GPU debug errors=0；18 次 DLSS/XeSS/FSR 热切换、774 生成帧、0 debug error，见 `shared-native-v1`。这些是保留原组件后的结果，旧 SDK 试验不算本版证据。 |
| VFG | 本轮原生 720p 463 checks/0 failures；真实包内 GUI 所有 2–8X/三质量、DLSS6↔VFG8、seek/pause/resize、列表/节点预设与重启恢复通过；独立 worker 冻结 High 8X，输出 64 帧 HEVC/240fps，见 `shared-native-v1`、`vfg-ui-v1`。上一已存档瘦身阶段还验证过原生 4K，不能将本轮 720p 记作重复 4K 验收。 |
| 灰色入口与兼容 | `policy-nv-v3`、`policy-amd-v2`：真实两份包、同一 RTX5070；四 NR/三 SR/五 FG 可见且状态正确，拒绝 FSR4 请求。AMD 包加载原 VFG 列表8X Medium/节点8X High配置，禁用 FG 并保留参数；两包实际 FSR SR 播放成功，0 provider failure。 |
| 导出码率与 RTSS | `production-rtss-v1`：真实 RTSS7.3.5.28314/RTSSHooks64 注入；2K30 MPEG-4 AVI→NR+最高 RTX SR→4K HEVC；18 Mbps VBR 后改其他选项、24 Mbps CBR 直接开始，两份均 24/24 帧。worker 使用冻结码率；输入值不是保证短片平均码率逐位相同。背景取样 alpha255，旧 NVIDIA marker 不触发误判。只关闭本轮启动的 RTSS，未改其配置。 |
| 极简右边缺口 | `edge-v1`：六个整数/分数 DPI 与软件绘制实际窗口测量，rightGap=0、bottomGap=0；没有用容器比例黑边冒充缺口。 |
| AMD NR | `amd-runtime-audit.json`：528 份实际分发文件逐 SHA，186 权重/62 HIP 模块，真实 ABI1/144 bytes 与模块布局通过；caps.hip=0。没有运行 HIP 推理。 |
| 对应源码可重建 | 源码包保留 byte-identical 2.0.0 依赖包，新增 lmxxf 源码和实际 recipe；排除权重/编译模块/截图/CSV/对话。从独立解压源码和 manifest 重建真实 AMD 宿主 DLL 成功，`amd-source-rebuild.log`；发布仍携带原已核验 DLL，不替换为重建件。 |
| 依赖与身份 | `stage-nv-v2.json` / `stage-amd-v1.json`：完整 payload SHA、厂商边界、每 runtime 的来源身份和 PE imports/delay imports 闭包通过，无 NPP、驱动或开发文件。封包前 NVIDIA1531/AMD2010 payload，最终说明文件更新后数量以各包最终 manifest 为准。 |

独立 staging 副本还以 Windows-only PATH、无 SDK/runtime override，在 GPU D3D12 和软件绘制各打开 home/pro/node/exp/set/min 六页；日志 `clean-smoke-pre-NVIDIA` / `clean-smoke-pre-AMD`，每包12张截图与运行前后全文件 SHA 校验。最终归档的解压/CRC/SHA 和远端资产校验按下节继续记录，不提前声明完成。

## 失败、修复与界限

构建初次有测试目标名、ChainValidation 字符串类型和变量重名问题，修正后通过。旧 SDK 试改有弃用 ABI/版本判定问题；该试改已整体撤下，不依靠它发布。测试 harness 的 argv/offscreen plugin、清理后旧 profile 路径、VFG DLL 文件名假设分别修正后通过。真实节点补帧子菜单漏传 disabled/note，在本轮检查发现并修复，随后两个包真实 UI 均通过。第一次依赖包误带上游测试截图和二进制模型，未发布，剔除后逐文件核验并独立编译通过。所有失败日志保留，不将首次失败记成通过。

没有 RX9000、RTX40、用户616.92驱动、实体 Xbox 音频/长会话、物理屏幕延迟的验收。本版 Xbox 自动重连不能阻止服务端 KickForServerShutdown 或根治 Wi-Fi 丢包。AMD NR 为 1080p 像素预算，不支持 HDR/原生1440p或4K NR导出。VFG8X High 不保证实时240fps；提交 FPS 不是物理屏幕刷新率。旧 `delivery.ps1` 只支持 legacy Win32，本轮用上述 QML/实际生产定向验收，不将旧 gate 记作通过。

## 最终交付的执行与回执

合并后先核对最终 main 与已测构建的产品源树，finalize 写入干净源码 commit/版本/逐文件身份，生成两个7z、应用源码和依赖源码；独立新目录解压后再核对全部 payload/CRC/版本/厂商闭包。归档只做无损压缩，不改 DLL 或模型字节。

发布资产清单与 SHA256 在 `E:/项目/Veyra/releases/release-2.0.3-20261004/SHA256SUMS.txt`；最终本机及远端回执在 `E:/项目/Veyra/logs/release-2.0.3-20261004/`，发布完成后追加 WORKLOG。Release 正文保留赞助与交流群图各 width220；本轮 HEAD 请求两个固定图片均 HTTP200，远端正文上传后再核对。只有完成实际上传和远端 digest/大小检查才报告发布完成。

## 最终归档与发布追加证据

2026-10-04 已实际完成上述步骤：最终 NVIDIA1535/AMD2014 payload 及manifest独立解压CRC/全SHA/PE闭包通过，两包GPU/软件各六页共24截图通过，真实bridge版本2.0.3。远端5资产均大小/digest匹配、下载HEAD200；两图width220/200，正式latest非prerelease。Release https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.3；详细命令/大小/哈希和回执路径在WORKLOG最终发布条目。发布产品commit f82f6499ff0db9c36953bcafb752b9be2d7fca4d 不变，后续追加仅为文档记录。
