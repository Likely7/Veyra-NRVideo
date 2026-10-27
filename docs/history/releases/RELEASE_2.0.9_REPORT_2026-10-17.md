# Veyra2.0.9 发布回执（2026-10-17）

正式版：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.9，ID415481126，published_at 2026-10-17T00:18:11Z，非草稿/非预发布；latest经API确认v2.0.9。源码：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.9-sources，ID415481099，非latest，应用源码与新增去隔行源码两个资产。

用户批准发布已交付2.0.9测试包。两ZIP只改外部文件名，内部2.0.9-test、清单/原文件夹及所有payload保持，PE2.0.9.0，EXE SHA7add56b9651d6b629433419fb0d2d9d077057d9a77595ebeabbf4d5fb3b80f80。不重编译/重新封包，正式远端资产digest/size/state逐项等于本机原件。

| 资产 | 字节 | SHA256 |
| --- | --- | --- |
| Veyra-2.0.9-NVIDIA-win64-portable.zip | 677304267 | `6618461d722428aff65e8eee1847cd673aaa02d6c00beab87b7e661b97e0f843` |
| Veyra-2.0.9-AMD-win64-portable.zip | 356014225 | `e9224fb7f546253e351d2262168a8cedd3c80319f68379392699ecb439c6cc83` |
| Veyra-2.0.9-source.zip | 71765370 | `efc2463bf09bb2936e36e3537bbb23a5de11e252d17aeabee41d099ad68ab408` |
| FFmpeg-9.0.1-deinterlace-module-source.zip | 22807881 | `f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152` |

主Release恰两个运行包，源归档两个新材料；原字幕FFmpeg源和依赖/重链接源只保留既有固定链接，服务器digest已核对，不重复上传约600MB依赖。对应应用源c9e779b、产品0ac9d39，发布标签08832f6，后续仅追加本回执/WORKLOG并普通推送main，不移动标签或修改资产。

## 完整说明与实测边界

覆盖v2.0.8..bfce70b的57非merge提交/70产品构建测试路径：新版SF310.9.16/defaultB与旧A开关、迁移/缓存/导出，PR44五卡HDR/记忆/低频20字节统计，20系等待样本相关NVOF注册同步与失败状态，粉丝字幕缓存/游戏画面及自定义热键，issues45/43/42/41的VRR/英文底栏/地址播放/去隔行，以及复用CMake缓存头文件静默stub修正。保留贡献来源；不把诊断中间提交均当新功能。

RTX5070/GTA4K输入/1080NR同条件旧SF-v2 5.87296→新版默认3.20118ms，NR GPU阶段−45.49%；模型画面可能不同，不是全显卡同画质FPS或光学延迟保证。20/30/40及AMD实卡、真实VRR/跨屏、长期NV显存问题保持明确未验/未根治说明。

README当前版本/新增内容/下载与现行NR/快捷键/构建更新，其他页面说明和六群/赞助/Discord/Ko-fi/知识库区块保留。两个Release正文均有微信赞助与六群二维码width220、Discord/Ko-fi和四群知识库。公开raw文档/二维码/Ko-fi图及Release/四资产/两固定源下载入口实际可访问；知识库匿名请求HTTP200，仅验证链接，不声称阅读全部文档。

## 验收与保护

原NVIDIA1666/AMD2106payload、48/534运行组件、ZIP CRC/逐文件SHA/EXE/PE和应用2044源码文件验证通过。旧实际构建树745输入SHA一致，发布新checkout2个LF/CRLF差异逐字归一化与Git对象等价；没有混称原始SHA相同。四次最终ZIP播放回执395/404/402/403原帧、无QML/GPU错误，复用原证据，不重复GPU基准。

新发文件为原ZIP硬链接，所有产物E:/项目/Veyra/{用途}/release-2.0.9-20261017。83旧工作树中仅main获准推进，其它82树/95旧分支除main/509脏文件/六原ZIP/旧v2.0.8主与源Release原样保护。源码前后bundle verify、最终远端/保护/资产核验结果见reports/release-2.0.9-20261017/final.json。

群公告已拟稿为799字符，含下载与知识库链接，仅交付用户自行发布。没有代发群消息/评论、关闭issue、子Agent、驱动/真实配置变更或关机。
