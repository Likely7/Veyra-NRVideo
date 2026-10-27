# XeSS 开启光流时的运动边界修复（2026-10-12）

本轮接手 Claude 最新本地会话 d6689aca-8401-42b5-8005-40a5adb7fb7f。其最后的用户要求是修开启光流后的果冻，自动模式保持，延迟研究暂缓。来自 claude/jelly-20261012 的两个诊断提交 66e2407、7fe3ea6 与五个未完成文件已继承到 codex/xess-handoff-20261012；原树、桌面树和 main 保留。独立 start.json 与 source-before.bundle 在 E:/项目/Veyra/archives/xess-handoff-20261012。

## 已确认的问题与修复

NVOF 的网格矢量可能跨越人物和背景边界，原有校验还会把低置信度区域清零。Claude 的 XeSS 诊断显示更准确的运动输入有改善空间；这证明运动边界是可改善因素，尚不能断言用户所有果冻均由这一项造成。

保留 Claude 的两个 shader 算法，完成其遗漏的产品接线：

- FlowRefine 在每个光流像素选择邻近网格中与前帧亮度块更匹配的 current→previous 矢量；FlowFill 对匹配差的区域借用附近相似颜色、低代价的矢量。这是有界启发式处理，没有修改 XeSS 模型。
- 只在 XeSS + 显式光流 + NVIDIA NVOF 时建立这条分支。Automatic/Zero、AMD FidelityFX/GPU DIS 光流和其他补帧沿用原路径；NR/SR 仍用原有校验后的光流。f468dfb 初次交付曾还原 Claude 半成品中的 FSR 光流初始化修复；这是接手范围误判，后续已单独补齐，见本报告末尾说明。
- 修正创建顺序、3 个 compute pass 的 shader/descriptor 对应关系、SRV/UAV 转换与屏障、尺寸缩放、资源预算和释放。首帧、reset 和没有有效帧对时仍送零运动并保持无历史标识。
- 将新增工作纳入现有 GPU 光流计时，避免性能面板漏算。没有增加 UI Timer，没有提高 33ms 统计或约 250ms 状态更新频率，没有改变 GPU 优先级默认值。测试显式选择普通 class 2；当前产品原有默认是实时 class 5，不能把两者混为一谈。

正常播放未增加 GPU→CPU 像素回读、逐帧 CPU fence 等待或无界队列。读回只存在于诊断测试。

## 构建与可复验命令

命令前缀为本机 Python 3.11 -X utf8 -B -I。以下相对产物路径均基于 E:/项目/Veyra，各 subprocess 测试最多 290 秒，构建设总上限 890 秒。

1. tmp/xess-handoff-20261012/build.py：最终产品回执 logs/xess-handoff-20261012/build-20261012-111040/receipt.json，22.2 秒，17 个依赖闭包对象核对/重编、16 个 EXE 链接成功。使用已验 main 构建对象和固定依赖，不是 clean 全工程重编。EnhanceGraph.h 的所有使用者按传递 include 重编。
2. tmp/xess-handoff-20261012/configure.py：configure-20261012-111144/receipt.json，完整产品功能配置和新 shader/test target 登记成功；没有引入 Qt Quick 私有重绘接口。
3. tests/xess-handoff-20261012/harness/regress.py xess-motion chain contracts hdr video rollback：logs/xess-handoff-20261012/xess/tests-20261012-111135/receipt.json，六组 exit 0。160 项真实 NVOF/GPU 检查、6 个实际改变的运动帧对、D3D12 错误 0；首帧/reset/Automatic 为零，原有校验光流与未增强的实帧逐字节一致，重建/释放正常。真实 GTA6 设置事务、失败回退与 HDR 颜色回归同时通过。
4. tests/xess-handoff-20261012/harness/ui.py perf-old perf-new lifecycle：logs/xess-handoff-20261012/xess-ui-20261012-111651/receipt.json，40.3/40.0/50.1 秒，三组 exit 0。实际 GUI seek、暂停恢复、2X→4X、零运动、AMD FidelityFX 光流→NVOF、补帧关闭/开启通过。日志确认 4X 的 SDK framesPresented=4；AMD 光流也在本机 RTX 上实际 Dispatch result=0，不代表 AMD 显卡验收。临时 QML 探针已删除，Main.qml 恢复源文件字节。
5. tmp/xess-handoff-20261012/rebuild-boundary.py 仅重建抓屏诊断测试：logs/xess-handoff-20261012/boundary-build-20261012-112011/receipt.json，4.0 秒；产品及其他 EXE 哈希保持。
6. tests/xess-handoff-20261012/harness/quality.py：logs/xess-handoff-20261012/quality-20261012-112023/results.json，六组 exit 0，每组 11/11 个暖机后的中间帧、调试层和采集错误均 0。
7. tmp/xess-handoff-20261012/check-launcher.py：reports/xess-handoff-20261012/launcher.json，9.6 秒，实际启动测试入口并核对 XeSS2X/光流/NVOF/原版NR1080p配置，再按时退出；检查探针也已删除。

## 画质对照

同一 EXE、同一场景用 VEYRA_TEST_FLOW_REFINE=0/1 切换旧/新路径，2560×1440、XeSS 2X、光流 1280×720。SDK 测试开关只显示插值帧，再用桌面复制抓取实际显示像素；HDR 桌面的 SDR 白电平按 4.75 还原。27/80 是每个 30fps 源帧的位移像素，不是播放帧率。

| 场景 | 旧轮廓带 MAE8 | 新轮廓带 MAE8 | 误差降低 |
| --- | ---: | ---: | ---: |
| 人物中速侧跑 27px | 6.8755 | 6.3781 | 7.23% |
| 人物高速侧跑 80px | 8.2148 | 7.4180 | 9.70% |
| 背景横移 80px | 3.7651 | 3.0332 | 19.44% |

远处背景 MAE8 分别 1.3340→1.2589、0.4784→0.3884、0.5131→0.3873，没有本组对照中的背景负收益。腿部截图人工核对可见网格边缘改善，也仍有细条弯曲等残留。误差下降不是“果冻减少多少”的用户体验百分比。

SDK 在“只显示插值”诊断模式中的 generated counter 为 0；不拿该计数当生成成功证明。每个目标中间帧均通过像素匹配找到，matchedReal=0；正常 GUI 测试另有实际 SDK 2X/4X 提交记录。

## 性能对照与成本

本机 RTX 5070、616.56，用户指定的 GTA VI 原 4K30 视频、原版 NR 一层/内部 1080p、NVOF 1080p、SR/HDR/调色关闭、XeSS 2X。两组均在打开视频前设定普通 GPU class 2，不改真实用户配置。预热后取相同源帧 300–840，共 541 帧；20 个独立 GUI 秒样本。

| 指标 | 旧路径 | 新路径 |
| --- | ---: | ---: |
| GPU 处理图中位数 | 8.4835ms | 8.8310ms |
| GPU 处理图 P95 | 8.6534ms | 9.0396ms |
| 提交 FPS 中位数 | 60.0 | 60.0 |
| 暖机后显存读数中位数 | 1980MiB | 2037MiB |

新增 GPU 成本约 0.348ms（本组 +4.10%），固定纹理预算约 56MiB，实测显存读数 +57MiB。GPU 图按同一帧 Color.begin→Residual.end 计算，不能把 XeSS presenter 的异步 FgBatch 时间戳混进去，也不是全部 XeSS 生成耗时或屏幕端到端延迟。现有“光流”统计新增约 0.57ms，还包含过去未计入该区间的 densify/adapt；真实新增成本应看上述同边界处理图，不能用两版 UI 阶段之差替代。

分析脚本 tmp/xess-handoff-20261012/analyze.py，汇总 reports/xess-handoff-20261012/validation.json。先前 110652 对照约 +0.340ms/+57MiB，与最终复测一致。

## 失败记录与限制

- 第一轮性能夹具误把 sizePolicy=1 当实时 1080p，实际为 Native 4K，且没有显式固定普通优先级。xess-ui-20261012-110508 保留，已作废；110652 和最终 111651 确认资源尺寸与 class 2 后重测。
- quality-20261012-111634 的第二组只抓到 4/11 个有效目标中间帧：桌面重复更新耗尽 64 张容量，并非产品报错。诊断测试改为仅保留像素不同的相邻截图、容量有界 128，完成条件加强为 11/11 且无采集错误。六组完整重测与此前 110221 的 11/11 结果一致，没有降低验收门槛。
- 单独重链测试最初漏设缓存库工作目录，LNK1181；修正后 exit 0。保护核对器起初没处理两棵 detached HEAD，误报失败；修正为空引用与开工值 None 比较后 61 树/509 脏文件一致。失败输出保留，不计入成功。
- 首次启动器检查在等待继承管道关闭后才查询进程，错过已正常退出的 GUI；日志显示启动与按时退出均正常。也发现种子配置需要一起复制 chain-session.v1.field-render 迁移旁文件；补齐后直接在真实 GUI 读取配置断言通过，未修改产品持久化逻辑。
- 本轮没有用户所述真实游戏片源的逐帧真值、AMD/Intel 显卡、长期显存、屏幕延迟和所有 HDR+XeSS 组合实机验收。不能宣称果冻根治或所有设备变快；本轮是用小幅 GPU/显存成本换取已测运动边界改善。

## 本地交付与保护

本地运行目录 E:/项目/Veyra/tests/xess-handoff-20261012/app，版本 2.0.6-xess-fix-test-20261012。入口 Start-XeSS-Test.cmd 使用独立配置目录；选择 XeSS 的“光流”运动来源才能测试新增分支。产品 EXE SHA256 43a5fea655a3b2840e5f5f25befac70c8ad6cd2de2f78c79385d8f83ea658edc。

运行组件沿用原件：libxess_fg.dll 1.3.1.78 / SHA256 EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27；原版 NR E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E。1626 母包文件 SHA 核对不变；DLL、SDK、模型、测试媒体均未入 Git。保护、最终 commit、source-after.bundle 与回执哈希见 reports/xess-handoff-20261012/final.json。本轮不 main 合并、不 push、不发布、不关机。

## 后续范围纠正与联合交付

用户追问后重新读取 Claude 的原会话与 JELLY_FIX_PLAN：其中明确包括 FSR 单独启用时创建光流；此前自行排除它是 Agent 遗漏。初次报告的 XeSS 数字仍对应 f468dfb，不将它们改称 FSR 修复后的重测。现已在同一隔离分支补齐该初始化条件、旧代码失败/修复后通过的真实消费测试、FSR GUI 生命周期，以及原计划的密集竖条横移对照。新产品 EXE 和独立入口为 tests/fsr-flow-followup-20261012；原版测试目录和全部旧回执保持。完整结果见 docs/FSR_FLOW_FOLLOWUP_2026-10-12.md。栏杆模型问题、延迟研究和真实用户游戏观感验收仍未完成，不宣称全项实卡验收或根治。
