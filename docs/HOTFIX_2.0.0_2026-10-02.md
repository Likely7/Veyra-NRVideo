# 2.0.0 修复更新（2026-10-02 晚）

用户要求修复以下反馈并替换 GitHub 上 2.0.0 的包（不发新版本号），同时把 5 秒宣传动画放到 README 顶部。

## 修复

| 反馈 | 原因 | 改动 | 验证 |
|---|---|---|---|
| 输出稳定器·抗闪烁开了再关，画面定住只有声音 | 关闭后管线重建，但 `EnhanceGraph::createComputePasses` 只在强度 > 0 时才重建稳定器，`shutdown` 也不释放它；旧的 `NrHoldPass` 留在新图里，每帧把它最后一张结果拷回 `residualRgba_`（还持有旧图已释放纹理的裸指针） | 总是调用 `createNrHoldPass()`（强度 0 时只做释放）；`shutdown` 释放 | `scripts/ui-check/stabiliser-toggle.qml` 关闭后每秒截图：修前连续 0 差异（画面静止），修后每张都有变化；SR+NR、SR+NR+FG 两组 |
| 首页"继续上次"采集卡没有声音，要去采集面板重选 | 继续上次（查询 kind 2）只恢复设备/格式/色彩，没有恢复音频输入和帧率，连接时 audio=-1（现场日志 1 次 audio=0 后每次 audio=-1） | 抽出 `restoreRememberedAudio`，面板和继续上次共用 | 本机无采集卡，**未实机验证** |
| 性能球（负载）多层 NR 只算一层，已满载仍显示绿色 | 每层 NR 都写同一个 `GpuStage::Nr` 计时槽，只剩最后一层；总耗时 `enhancementProcessingMs` 只看这个槽。稳定器开启时还用第二对 `Residual` 时间戳覆盖了残差合成 | 总耗时并入 `NrLayer0..3` 各层区间（重叠只算一次）；稳定器在残差区间内收尾 | `veyra_repair_contract_tests` 243/243（单层、重叠区间、缺测阶段等原有用例不变） |
| 调色"效果"组的纹理/清晰度/去朦胧没效果 | 三个滑块只存值，着色器从未实现（注释写着留到 P2） | 新增 `ColorDetailPass`（`ColorDetailReduce/Blur/Apply.hlsl`）：约 1/8 块统计 log 亮度与暗通道最小值 → 可分离高斯 → 全尺寸应用。纹理=细节环，清晰度=中间调局部对比，去朦胧=暗通道去雾（按像素自身暗通道封顶防光晕）/负值加雾。列表模式接在输入转换（融合调色）之后，节点调色接在 `ColorGradeInstance` 之后；全为 0 时不分配不调度，改值不重建 | `scripts/ui-check/colour-effects.qml` 暂停同一帧截图对比：清晰度 ± 使亮度标准差 26.7→29.7/24.9，纹理 + 27.3，去朦胧 + 层次加深无黑边，− 均匀雾 |
| 选择采集后卡死（4070 SUPER，改 scRGB 后好了） | 开始出画面约 60 ms 后整个进程的 GPU 设备被移除（0x887A002B），Qt 界面设备一起丢失；进程内注入了 RTSS 与 NVIDIA 叠加层。日志里输出是 SDR，HDR 输出格式只在 HDR 输出时生效，代码上找不到 scRGB 起作用的依据 | 不能替用户关驱动/叠加层功能：失败提示点名注入组件并给出关闭位置；同时写 `veyra-last-failure.txt`，下次启动再提示一次 | 编译；提示路径**未实机触发** |
| 点击开始采集后卡死（5080，logs.7z） | 两次 GPU 挂起（nvlddmkm 153）时进程里有 NVIDIA 呈现层（Smooth Motion）；两次崩溃在 `nvppex.dll`（NVIDIA App 的 RTX HDR / Smooth Motion / 动态鲜艳度插件） | 同上；崩溃处理器记录崩溃模块和已注入组件，下次启动提示 | 同上 |
| 点击任务栏图标不能最小化 | Qt 无边框窗口是裸 `WS_POPUP`，没有最小化框，任务栏点击不处理 | 主窗口加 `WS_MINIMIZEBOX | WS_SYSMENU`（不加边框/标题），每次可见性变化后补回 | 日志确认样式 `0x960A0000`；任务栏实际点击**需用户确认** |
| Xbox 串流 SDP 交换失败 `type must be string, but is null` | `getExchange` 用 `json::value()` 读 `exchangeResponse` / `errorDetails.code`，服务器给 null 时直接抛异常，真实原因被吞 | 只读字符串字段；空应答时记录完整应答（不含凭据）并报"主机拒绝（原因）" | `veyra_xbox_tests` 72/72；真实失败原因要等用户下一份日志 |
| 抗闪烁两个滑块 QML 报错 | `onMoved` 用了已弃用的参数注入 | 改为形式参数（顺带音量、音频延迟两处） | QML quick 29/29 |

## 未处理 / 未验证

- **闪烁（3070 Ti 笔记本，logs.rar）**：日志中没有对应"闪烁"的错误；两次崩溃在 NVIDIA 驱动 `nvwgf2umx.dll`（一次窗口缩放、一次关闭 XeSS 补帧交换链）。需要用户说明闪烁的样子（整屏黑闪/局部/只在补帧时）或录屏，暂不盲改。
- 采集卡音频恢复、注入提示、任务栏最小化、Xbox 新日志都需要实机。
- 清晰度/去朦胧的强度系数是本机按 1080p 画面调的，HDR 工作域仅按单位换算，未在 HDR 显示器上看过。
- 多层 NR 的负载数值未在实机多层场景下截图核对（单测与编译通过）。

## 构建与测试

- 构建：`E:/项目/Veyra/tmp/main-merge-20261002/build.py`，日志 `E:/项目/Veyra/logs/main-merge-20261002/hotfix-build*.log`。
- 测试产物：`E:/项目/Veyra/tests/hotfix-2.0.0-20261002/`（stab*/hold*/colour*/final-hold）。
- 现场日志副本：`E:/项目/Veyra/tmp/hotfix-2.0.0-20261002/field/`。
- `scripts/ui-check/ui-check.py`：日志尾部未写完的半行不再算已读（之前会丢截图指令）。
