# Veyra 2.0.8 发布回执（2026-10-15）

正式版：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.8，Release ID 413665238，published_at 2026-10-15T10:17:26Z，非草稿、非预发布；latest经API核对为v2.0.8。独立源码归档：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.8-sources，ID 413665184，非latest，只有应用源码一个新资产，未变的FFmpeg/依赖源码保留既有固定下载。

## 用户批准原包直接晋升

仅改外部文件名，ZIP/EXE/包内QML/着色器/运行组件/原文档逐字节保持。内部2.0.8-test、原文件夹名与清单标志保留；PE2.0.8.0，EXE SHA256 a4676e9a5bf08738dcc85c6c6909045f0593ebadcf857e6108904e90d4df2432。没有重新构建、重封包或用旧版本截图冒充本版验证。

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Veyra-2.0.8-NVIDIA-win64-portable.zip | 718608815 | `e89972d234fc79167c08b2183b42347c9ace606663b9ac363e2d09a547afe0ba` |
| Veyra-2.0.8-AMD-win64-portable.zip | 355816850 | `1740d7dbc6c8307d0dd8deefb9aaafac4398ed0e40022b1e40e66a86092ecda9` |
| Veyra-2.0.8-source.zip | 71605760 | `4ca39a70797c6080ef02646e1d2aec0a392346ea3594206c8809f188a64d3d99` |

服务器资产digest/size/state与本机原件逐项一致。主Release只上传两个用户包；GitHub自动源码链接为平台自带，不算额外运行包。应用源码原样归档80a3d7c、产品构建99435ae；发布文档标签/当时main33b22b9，679构建输入不变。后续仅本回执/WORKLOG推进main，不移动两个发布标签。

## 说明与公开入口

汇总双方v2.0.7至已验main3107e00全部51个非merge提交、72个产品路径。重点延迟/游戏模式/VRR/采集卡手柄及第三方远程电脑游戏；DLSS性能模式、双卡、采集格式、GC550/BMD/直播伴侣、GPU恢复、滑块/开关/缓存/缩放/旋转/节点/更新/日志/设置统计全部列明。输出预算最终只提示并保留用户倍率，已替换中间自动改倍率方案；VFG没有新模型。

软件阶段耗时与光学端到端区分；没有采用理论PBP或旧统计误归属数据宣称屏幕收益。RTX50性能模式的GPU收益保留画质取舍，VRR当前NVAPI检测、真实VRR/GC550/BMD/AMD/直播伴侣并行场景边界明确。用户已测试当前包并允许发布，不把该反馈扩展成所有硬件测试。VFG首次合成校验间歇异常、独立复测通过及基线HDR RGB阈值失败仍保留。

中英README仅版本/新增内容/用法/构建入口等必要更新，Discord/Ko-fi/微信赞助/六群footer逐字保持；Release/SOURCE两个二维码各width220。知识库URL及四群汇总说明保持。公开raw文档与图片、Release页/三个新资产/两个旧源码链接均已检查成功；知识库匿名Cookie请求返回200但未宣称阅读内容。

## 验收与保护

原包1654/2093payload、49/534组件、EXE/PE/CRC、应用源码2011文件逐项SHA复核；四场最终ZIP播放回执原样引用，无产品修改，不重做无根据的GPU测试。旧v2.0.7 Release及资产保持；未变FFmpeg/依赖源服务器digest对应。77工作树中仅当前发布树/main按授权变化，其余75树/509脏文件、五原归档和既有分支保护复核；实际最终结果见E:/项目/Veyra/reports/release-2.0.8-20261015。

本轮产物全在E盘，发布文件为原ZIP硬链接，没有整工程重复副本或新解压目录；不重试此前被拒绝的递归清理。无子Agent、群消息代发、关闭issue、关机或用户配置修改。完整命令/API草稿-上传-发布日志在logs/release-2.0.8-20261015；源码前后bundle在archives同任务目录。
