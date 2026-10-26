# 采集链路审查、抗锯齿与显示状态：Claude工作接手完成

当前分支 `codex/capture-review-handoff-20261026` 从Claude干净提交 `b2eb77d` 接续；原 `claude/capture-review-20261026` 保留。主线仍是 `68def10`，没有合main、push、Release或封包。软件UI默认分支 `claude/software-ui-default-20261026` 的 `ea148bf` 已完成但保持独立；Lecram仍暂停。准确源码、构建、文件身份与检查结果在 `E:/项目/Veyra/reports/codex-capture-review-handoff-20261026/FINAL.json`；原任务目录的FINAL.json指向这份接手结果。

## 接手内容和完成情况

| 内容 | 状态与证据 |
| --- | --- |
| AA列表顺序 | Claude的d807407已把有NR时的AA移至NR组后；无NR仍在源尺寸/SR前。列表普通/NR先行、节点、分块图片合同保留。此次补完同帧真实NR/AA、SDR合成边缘与HDR GPU验证。 |
| VRR事件查询 | Claude的5a80948/309bd58经39e4de4合入当前任务分支；不是合入main。按显示器缓存、后台单在途、事件作废、有限重试，无固定2秒驱动轮询。 |
| HDR与信号卡片 | b2eb77d把HDR/峰值并入同一服务；专业页左下信号区共用刷新按钮与HDR/VRR/Hz信息。此次补修缓存过期等待与HDR独立失败恢复。 |
| 截图格式提示 | 新增完成结果savedFramePath，经既有快照/Facade发布；UI在编码成功后才公布真实文件及通知，HDR的.jxr不再误报.png。UI不额外轮询磁盘、不解码截图来猜测保存完成，原33ms/250ms节奏保持。 |
| 最大化/全屏恢复 | 复现了Windows短暂报告Windowed导致极简尺寸动画重启。screenSized覆盖Fullscreen/Maximized及恢复中的短暂状态，Qt.callLater在本次切换结束后清理；没有新增轮询定时器。实际Qt/Windows窗口恢复与截图测试通过。 |
| 声音偏大 | 只读端点探针再次确认G50为+30.00dB、scalar=1、范围-96..+30dB、hwSupport=0x2。没有修改端点、用户音量或添加AGC。 |
| 报告与验收 | 原先未完成的离线AA对照、完整工作记录与FINAL索引已补齐；失败和无效比较保留，不伪装通过。 |

## 抗锯齿的实际结论

原实卡截图不能用于严格开关对比：相同配置连拍仍有约37%像素变化；所谓“无NR”只关掉所选层，另一层仍运行；所谓“AA在NR前”的节点步骤切入的是空链。原报告aa-shots.json已标出这些问题，此次不重跑采集。

改用那张空链HDR截图作为固定、未做NR/AA的捕获画面。先将原scRGB浮点数据按80nit单位/203nit参考白/Reinhard映射成SDR sRGB8，再将**同一个**2560x1440输入分别送入产品真实GPU图：原画、仅SMAA高、仅NR两次、AA→NR、NR→AA。NR使用原测试夹具的单层Realtime/关闭时域/零运动诊断配置，不冒充用户的原生HDR、多层NR和实时运动条件。

- 两次独立NR输出逐像素一致，排除了这次离线对照的NR重复噪声。
- NR→AA输出中，任意RGB量化变化22485像素；超过一个8bit量化等级的变化19402像素（约全帧0.53%）。边缘平均变化0.001299，非边缘0.0000215，约60倍，证明后置AA确实执行且主要作用在边缘。
- AA→NR改变了更多区域；这不是质量优劣分数。NR会重新生成内容，前置AA的影响可能被改写，但不能把“完全抵消”当成所有素材都已证明的根因。
- 640x360合成SMAA高测试有2215个变化像素，208229个平坦像素逐字节不变；已知超采样轮廓参考的误差32.191→6.762。仅对这张合成图成立，不能宣传成真实游戏画质提高79%。
- 独立HDR SMAA测试通过：有限浮点、平坦高光、1000nit亮度及超709负分量保留。

所以“AA开关完全没效果”不成立；但这种边缘处理本就可能很细微，不能承诺像DLAA/TAA一样全面重建画面。没有做偏好盲测、时域运动画质或所有模型/素材的质量排名。

证据：`aa-gpu.json`、`fixed-frame-analysis.json`、`fixed-frame-comparison.png`；源帧/SDR输入SHA在prepared.json。图像为固定ROI对照，不是不同时间的直播截图拼接。

## 显示查询与性能审查

发现HDR接入后，图重建遇到过期缓存会等共享查询最多一秒，其中包含可能很慢的VRR驱动调用。已改为同屏旧缓存立即使用并异步更新；只有首次完全没有缓存时最多等待100ms，之后沿用原有目标屏保守HDR合同。没有改FG期限、预算或60→30采样策略。

另修HDR单独失败却被当作有效缓存永久保留的问题：即使VRR成功，HDR输出描述失败也触发同一代最多两次自动重试，耗尽后等待新事件/手动刷新。真实HDR关闭(false)与查询未知(nullopt)保持区分。新增CPU用例覆盖未知、不捏造SDR、延后重试、上限及手动恢复。

原135.6秒实卡轮次（本轮没有再次占用采集卡）的60秒稳态中：NR + SMAA + 调色 + VFG2X、HDR、2560x1440p60输入、60→30处理后提交约60fps；显示查询0、HDR轮询0、engine-stall计数0，无ERROR，slotWait=0。报告的GPU相关P95包括NR5.23ms、光流1.67ms、FG3.11ms；回调到Present返回P9530.28ms，其中调度等待17.58ms、GPU ready10.58ms。

**覆盖纠偏：这不是全部效果同时开启的实卡验收。** 原脚本只强制开启AA和调色，继承了NR和VFG；SR GPU计时为0，且未强制开启美颜。不能把此轮扩展为SR/美颜同时开启、所有效果/所有后端均已实卡通过。用户要求只做一轮采集，故复用现有轮次并如实保留覆盖缺口。

约37ms是原日志中的驱动时间戳年龄，17.58ms是本轮时序的等待，均不能直接宣称硬件物理下限；插帧确需后帧，但具体等待取决于源帧率和策略。各窗口P95也不能直接相加当HDMI到屏幕延迟。320Hz是显示模式读数，60fps是软件提交，不是屏幕扫描/光学测量。

“每帧SetWindowRgn”也不符合原稳态证据：整轮112条，60秒稳态0条；源码已有区域缓存，主要在界面动画/切换时变化。本轮没有为此移除必要的区域更新或掩盖日志。格式两次发现分别服务UI枚举和打开时协商/验证，不是稳态逐帧查询；没有为省启动日志删除设备契约核验。

## 音频：已确认什么，尚未确认什么

旧探针与此次只读探针均为+30dB（理论振幅倍率约31.6），明显高于0dB。0x2表示硬件静音，未声明硬件音量；[微软QueryHardwareSupport文档](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-queryhardwaresupport)及[EndpointVolume说明](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudioendpointvolume)说明，缺少硬件音量时Windows会在共享模式提供软件音量处理。这不等于Veyra自己又加了30dB；没有在Veyra源码找到对应的端点放大操作。音频输入超1.0也不直接等于已剪裁。

建议用户在Windows声音→录制→HDMI(KUHAIMI G50)→属性→级别，切换dB显示后先设0dB，再听/看日志；本次没有代改系统设置，也没有用自动限幅/静音掩盖问题。缺少修改端点后的A/B与原始无增益参考，因此不声称已经证明声音偏大的唯一原因。

## 验证、失败与交付边界

构建复用`build/claude-screen-recording-20261022`，2.1.0-cr2，-j2；完整定向配置构建45.2秒，保存结果接点最终构建41.4秒。六个目标均通过：EffectChain、AsyncDisplayQuery（含HDR新增三项）、UI合同、i18n、Repair272、QtQuick115。extract.py --check为2674条，四语缺失/占位问题0。Snapshot最终改动重建并重跑UI/Repair；其余无关已过项目不反复重测。

Qt/Windows窗口程序化测试（非物理键鼠、非实卡）验证：极简最大化→全屏→恢复原尺寸，恢复期间不启用cinema尺寸动画，无setGeometry错误；SDR截图PNG、真正PQ输入/HDR输出的截图JXR，实际文件、lastScreenshot和通知一致。最终ui-sdr-r4/ui-hdr-r4各约8秒。没有修改Windows HDR开关或用户真实配置。

失败保留：第一次误写HDR用例名hdr-smaa-high退出2，改为实际存在的hdr-smaa后通过，只补跑该用例；绘图时本机matplotlib缺packaging、内置Python缺matplotlib，通过工具返回的内置site-packages补依赖路径后生成，不安装全局包。最初全屏修复只覆盖稳定visibility仍复现Windowed中间态，补恢复状态保护后通过。首个10bit HEVC夹具未写入PQ VUI，实际是SDR，不能算HDR通过；保留该文件，改显式x265 VUI并用ffprobe校验后才验收真正HDR路径。最初截图完成使用文件轮询，最终改为引擎完成快照，避免UI磁盘轮询/解码和状态文案竞态。

运行组件177项SHA核对不变；旧Claude提交/VRR/软件UI分支/main及旧源码工作树不覆盖。单测试<=300秒，所有新产物在E盘本任务用途目录，没有推送、合main、封包或修改驱动/模型。原实卡结果属于Claude的b2eb77d，接手后新修复由定向离线/窗口测试覆盖，不冒充最终代码又做了一轮实卡或整机长期验收。
