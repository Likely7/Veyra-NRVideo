# 补齐 Claude 计划中的 FSR 单独光流修复（2026-10-12）

用户追问 FSR bug 和 Claude 其他工作是否完成。重新核对最新聊天及其获准的 docs/JELLY_FIX_PLAN_2026-10-12.md，FSR 的初始化漏项明确在原计划内。初次接手自行排除这一项是遗漏；此前 f468dfb 只完成 XeSS 部分，不能称整个计划完成。

保留 checkpoint/xess-handoff-complete-20261012、其 source-after.bundle 和所有旧回执。继续 codex/xess-handoff-20261012，本轮独立产物 E:/项目/Veyra/{archives,build,tmp,tests,logs,reports}/fsr-flow-followup-20261012；start.json 记录 f468dfb、main、初次不可变基线及 bundle/最终回执哈希。61 旧树/509 脏文件保护继续。无 Agent、main合并、push、Release或关机。

## 最小修复与验证

1. 给原 FG 边界诊断增加 product-only、auto-only、zero-only 三个真实消费用例，NR/SR/standalone 全关，不用注入运动纹理。断言真实 NVOF 执行次数，保留原 J0 product 强制 standalone 对照。
2. 用 f468dfb 产品对象构建上述测试，旧路径已复现：FSR 生成 9 帧，但 NVOF 执行 0，合同检查失败；D3D12 错误 0。logs/fsr-flow-followup-20261012/tests-20261012-115656/receipt.json 中 before 的 exit 1 是预期失败，不能写成旧产品通过。
3. initNvof 的 in-graph FG 消费者条件补上 Fsr/Fsr4，保留显式零运动不创建、VFG不创建、stillImage/noFeatures边界。仅让原本需要光流的 FSR 路径实际初始化现有后端，不把 XeSS 精修应用到 FSR，也不改默认选择。
4. 构建联合 XeSS/FSR 本地 GUI；检验 FSR 光流/自动/零运动与旧 standalone 对照、XeSS合同、设置重建及实际 GUI 的 FSR 单独启用/开关/seek。AMD/Intel显卡和 FSR4 ML 的实卡结果不能由本机 RTX5070 推断。
5. 记录命令/结果，核对原 main、旧树、原测试版与运行组件未变，本地提交与 bundle，交付联合测试入口。真实游戏观感验收仍待用户；延迟研究与 XeSS 模型栏杆问题仍暂缓。

## 实施结果

根因是 initNvof 对图内补帧消费者只识别 DLSS，漏掉 Fsr/Fsr4。NR/SR 关闭且没有独立光流请求时，FSR 虽然生成帧，实际拿到零运动。原 J0 的 product 夹具强制 enableNvofStandalone，所以它绕过了消费条件缺陷。修复仅把原 FSR 枚举加入已有创建条件；显式 Zero、VFG、stillImage/noFeatures 边界保留，FSR 不使用 XeSS 专用的 FlowRefine/FlowFill。

现有运动来源规则保持：引擎里的 FSR Automatic 原本需要光流；本次使这条原有规则实际生效。QML 属性 fgMotionSource 只接受 0/1，并把 Automatic 映射成有效的零运动或光流状态；没有添加 UI“自动”选项，也没有改 XeSS Automatic 的零运动规则。

以下相对产物路径均基于 E:/项目/Veyra。实际命令前缀为 C:/Users/123/AppData/Local/Programs/Python/Python311/python.exe -X utf8 -B -I；脚本在 tmp/fsr-flow-followup-20261012，所有单次测试低于 300 秒。

| 命令/回执 | 结果 |
| --- | --- |
| build-before.py；logs/.../before-build/receipt.json | 4.11 秒，用 f468dfb 产品图对象链接新增消费测试；不覆盖旧构建 |
| test.py before；logs/.../tests-20261012-115656/receipt.json | 旧产品预期失败：generated=9、evaluated=8、NVOF=0、D3D12错误0 |
| build.py；logs/.../build-20261012-115740/receipt.json | 40.7 秒，4 个依赖闭包对象重编、16 个 EXE 链接；复用已核验对象，非 clean 全工程构建 |
| configure.py；logs/.../configure-20261012-115841/receipt.json | 完整 CMake 配置成功，35.7 秒；未引入 Qt Quick 私有接口 |
| test.py flow auto zero standalone xess contracts hdr video；logs/.../tests-20261012-115841/receipt.json | 8 组全部通过；最长 GTA6 设置重建 43.62 秒 |
| gui.py；logs/.../gui-20261012-120643/receipt.json | 真实 GTA6 4K30、NR/SR全关、FSR3.1 2X，30.8 秒；NVOF/seek/零运动/AMD光流/补帧开关/恢复NVOF通过，约60提交fps |
| launchers.py；reports/.../launchers.json | 两个实际 CMD 入口读取配置、9.77/9.47 秒启动退出；探针删除、Main.qml 恢复 |

本机 RTX5070 的 FSR3.1 消费测试：

| 路径（NR/SR 关闭） | NVOF实际执行 | 生成/评分帧 | 轮廓带 MAE8 |
| --- | ---: | ---: | ---: |
| 旧产品显式光流，独立光流关闭 | 0 | 9/8 | 10.5214 |
| 修复后显式光流，独立光流关闭 | 9 | 9/8 | 2.0412 |
| 修复后引擎 Automatic，独立光流关闭 | 9 | 9/8 | 2.0411 |
| 修复后显式 Zero，独立光流关闭 | 0 | 9/8 | 10.5214 |
| 原 J0 强制独立光流对照 | 9 | 9/8 | 2.0411 |

所有修复后 FSR 组调试错误 0、未增强实帧 MAE=0。XeSS 合同仍为 160 检查、6 改变帧对、失败/调试错误0；颜色/HDR/设置重建回归通过。这些合成误差是“确实使用了运动输入”的证据，不能表述为真实游戏果冻减少某个百分比。现在 FSR 单独开启光流会真正消耗光流 GPU 时间，这是从错误零运动恢复所选功能的成本；没有测量或宣称本轮提速。

## 密集竖条回归

补齐原 Claude 计划中的一次密集竖条横移对照：在诊断测试新增 bars 场景，上方四分之三是间距22px/宽4px的竖条，横移27px/源帧，前景人物静止。下方保留唯一纹理角落用于确定抓屏属于哪个帧对；质量评分只覆盖上方竖条区，防止周期纹理把帧身份混淆。没有改变产品算法或原 run/orbit 场景。

rebuild-boundary.py 最终回执 logs/.../boundary-build-20261012-121135/receipt.json（4.12秒），只重编/链接诊断 EXE，产品和其余15 EXE哈希保持。bars.py 回执 logs/.../bars-20261012-121146/receipt.json：同一 EXE 的旧/新光流分别完整评分11/11中间帧，matchedReal=0，D3D12/采集错误0。竖条区人物轮廓带 MAE8 为10.1397→9.7037、远背景12.4370→10.2953，没有本组负收益；仍存在模型误差，不声称栏杆问题已修好。

## 失败夹具与验证边界

- gui-20261012-120042 把后端字符串写成 fsr，实际 API 要求 fsr3；gui-20261012-120259 又把只接受0/1的 QML 运动来源写成2。这两次夹具失败保留。按真实 API 修正后完整 GUI 通过；Automatic 由上述引擎测试验证，没有假称 GUI 具有该选项。
- bars-20261012-121021 的纯周期画面不能可靠通过原全图最近邻匹配，只识别1/11目标帧，exit1。保留失败；新增独立的非周期匹配区域后，仍要求完整11/11、原12 MAE身份阈值和零采集错误，未放宽为部分帧通过。评分区域排除了匹配辅助区。最终旧/新对照均重新执行。
- 本机实际验证的是 NVIDIA RTX5070、FSR3.1 和在此卡上可运行的 AMD 光流 provider。代码覆盖 Fsr4 初始化枚举，但没有 AMD/Intel显卡或 FSR4 ML 实卡验收；不外推兼容性。原 XeSS 开销/画质数字仍对应其独立测试，没有重跑无改动依据的性能 A/B。
- 默认GPU优先级、33ms统计/约250ms状态更新、运行组件和真实用户配置保持；正常播放没有新增读回/逐帧等待。真实游戏观感、长期显存和屏幕端到端延迟未测。栏杆模型问题、延迟研究仍按用户要求暂缓。

## 本地交付

联合 GUI 为 E:/项目/Veyra/tests/fsr-flow-followup-20261012/app，版本 2.0.6-xess-fsr-fix-test-20261012，SHA256 02d10c603da2a49c3bb2a352b3f741c241626d959baba97d482c594fddb342c6。

- Start-XeSS-FSR-Test.cmd：独立配置，XeSS2X/显式光流/NVOF，原版NR一层内部1080；继续做真实游戏观感测试。
- Start-FSR-Only-Test.cmd：另一独立配置，NR/SR全关、FSR3.1 2X/显式光流/NVOF；核验此次缺陷。

没有新建运行库压缩包；旧 XeSS 测试目录、启动器、所有原回执/bundle与61旧树/509脏文件保持。seal.py 逐项校验构建输入、当前EXE/QML/运行组件、回执和范围；reports/fsr-flow-followup-20261012/final.json 记录最终本地提交、增量源码bundle、检查点与保护结果。未 main 合并、push、发布或关机。
