# Veyra 2.0.5 正式发布回执

已发布：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.5，GitHub Release ID 405358232，published_at 2026-10-07T03:59:46Z。正式版、非预发布，GitHub latest核对通过。发布标签v2.0.5指向main合并939f1a5afa7b1d122adf656d157d4df6b6fad9af，发布源码提交e44e828119710b8f031cc968e8ddb1982aecc873；标签/源码树相同。收尾文档另行提交，不改标签或资产。

产品沿用已测2.0.5：EXE SHA256 acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936，PE File/ProductVersion2.0.5.0。EXE/QML/shaders和NVIDIA215/AMD694项原运行组件字节保持；本轮只有发布文档、脚本和元数据。1724个开工源输入、20份当前验收收据、28旧工作树/495修改文件/53历史证据受独立guard保护。

## 实际命令和结果

Python3.11 -B运行publish-2.0.5-control.py：通过；package.py：两完整包、应用/依赖源码、5资产清单完成，逐载荷SHA和ZIP CRC通过；audit.py：逐文件manifest、厂商隔离、PE导入闭包通过；cold.py：两最终ZIP独立解压、仅系统PATH/私有profile基础播放及内嵌字幕启动通过。冷启动：AMD 9.781秒/exit0/26个包内Qt/FFmpeg模块; NVIDIA 9.594秒/exit0/26个包内Qt/FFmpeg模块。均RTX5070本机基础运行，不能代替AMD HIP推理/编码验收。

remote.py push完成普通atomic推送main+标签；draft创建成功，但错误使用发布tag查询草稿返回404，上传尚未执行。随后tmp/publish-2.0.5-20261007/remote-recovery.py按已存在ID405358232继续resume-upload/verify-draft/publish/verify-public，没有重复创建、覆盖或重打安装包。远端全部size及sha256 digest与本机一致；逐字核对正文、标签/main和正式latest通过。比较旧资产身份时排除自然变化的下载计数；旧资产ID/名称/大小/digest/创建与更新时间均保持。恢复脚本/原脚本SHA及原因见remote-recovery.json；远端README/支持区四文件与精确Git blob一致，见remote-readmes.json。实际重新下载SHA256SUMS与本机一致，5公开资产/Release页面和双QR及Ko-fi按钮HTTP可用。双QR width220、Discord/Ko-fi保留；旧v2.0.4 ID/正文/时间/资产未改。没有代发群消息或关机。

所有回执：E:/项目/Veyra/logs/publish-2.0.5-20261007/，包括source-audit.json、stage-AMD/NVIDIA.json、cold-verify-results.json、main-merge.json、push.json、verify-draft.json、verify-public.json及remote-public-release.json。产物E:/项目/Veyra/releases/publish-2.0.5-20261007/；源码bundle在archives/publish-2.0.5-20261007/source-final.bundle并已verify。

## 失败和复核

发布脚本首次草稿查询接口使用错误，退出1；实际草稿ID405358232、tag为v2.0.5、0资产已确认，改用release ID继续。安装包、源码提交及产品字节未变；不是产品测试失败，也不将失败调用记为通过。恢复脚本单独封存，发布源码ZIP仍是精确冻结的产品/发布源树。

开工严格guard发现新checkout13个继承文件被CRLF转换；所有内容先证实仅换行差异，再在新自有工作树恢复受保护main原字节，并确认clean-filter blob等于HEAD。旧baseline/其他工作树未改，随后guard通过。原记录checkout-line-endings.json保留。唯一历史tracked pyc保留在Git，源码ZIP明确排除；不修改/删除既有源文件。

## 正式资产

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Veyra-2.0.5-AMD-win64-portable.zip | 355214873 | 7a01a9092d948b8f627040bf4247bf70a42dd0efd8e864a1483ba5fc7a0c27d8 |
| Veyra-2.0.5-NVIDIA-win64-portable.zip | 714242595 | e731d3661cf326f160242ebc6b8cade547e40fe514e1503661e1d986e9064c74 |
| Veyra-2.0.5-source.zip | 70164855 | d15c74598ffc341173e42c912fa8999576e812ed7881aa7b08c0cc550c1ecf86 |
| Veyra-2.0.5-dependency-source.zip | 598704862 | 4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9 |
| SHA256SUMS.txt | 398 | 1ff06292966739d1ab393b75bb73986f24fa65ce409e1a2cba1878283afac815 |

依赖源码与2.0.4原包字节相同（4eccde63…），本次改名并随Release提供；patched FFmpeg、静态字幕和串流对应源完整保留。源码无proprietary SDK/runtime/模型，用户包无开发LIB/PDB/测试媒体/配置/日志。

## 数据与边界

fix2同EXE统计动画反向对照NR区间下降5.80%、UI提交下降89.06%，只限RTX5070/616.56/320Hz、GTA4K30、原版单NR内部1080、普通优先级。与2.0.3匹配差+0.009%/−0.638%。新2.0.5单轮6.295ms不拼算新提速；UI提交不等于延迟/显存。NVIDIA显存持续增长仍未定位/修复，下版排查；AMD真实高分辨率HIP离线导出/编码、物理HDR/撕裂/VRR及长期多设备稳定性未全部验收。XeSS补帧/节点离线导出仍不支持。公告同样保留这些边界。
