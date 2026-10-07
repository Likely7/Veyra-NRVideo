# Veyra 2.0.6 正式发布回执

已发布：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.6；Release ID 405696687，published_at 2026-10-07T11:47:13Z，非预发布，latest核对通过。标签v2.0.6指向main合并e3aa842a00f693a4a17d1fc4c454d0ebc59428f6，发布源码e1f6952a6ab11483fc6ff67177d8d390a8faea2f，二者树完全相同。收尾文档另行提交，不改标签、产品或资产。

## 构建与回归

按[发布验收](RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md)执行全新2.0.6产品/测试构建、NR→FSR图20项、ABI62项、效果链264项、采集颜色213项/格式协商、GPU颜色124项、VFG GPU913项/settings331项以及四项NVENC导出（720p4X、4K NR4X、取消、缺失运行库）和GTA中等4X/高2X普通优先级预览。构建源输入逐hash绑定最终源码；PE数字版本2.0.6.0。真实AMD HIP/FSR4.1/Xbox及Blackmagic HDMI未在本机实卡验收，NVIDIA持续显存增长未修复。

package.py对干净发布源码生成完整包和对应源码；audit.py对两厂商所有manifest文件、运行组件身份、驱动排除及PE依赖闭包通过；ZIP CRC和全部载荷SHA通过。最终ZIP冷启：AMD 10.562秒/exit0/26个包内Qt/FFmpeg模块; NVIDIA 10.109秒/exit0/26个包内Qt/FFmpeg模块。均为RTX5070本机基础播放，不能代替AMD硬件推理/编码。

remote.py push/draft创建与外部resume-remote.py upload/verify-draft/publish/verify-public：普通atomic推送main+标签，草稿上传5资产，服务器大小/digest逐项等于本机；正文逐字一致、标签/main一致，正式latest核对通过。重新下载SHA256SUMS字节一致，5资产/页面/双QR/Ko-fi按钮HTTP200。新群5二维码远端SHA256等于用户原图5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd，双QR各width220，原赞助URL、Discord和Ko-fi保留。旧v2.0.5 ID/正文/发布时间/资产完整保留。

## 发布阶段异常与恢复

源码/标签推送成功，gh release create成功创建草稿ID405696687，但随后的GET releases/tags/v2.0.6返回HTTP404；列表和GET releases/405696687确认同一个草稿存在，且无资产。原remote.py draft因此在上传前退出，未重复创建、未提前发布。使用E:/项目/Veyra/tmp/release-2.0.6-20261007/resume-remote.py按已核实ID继续同草稿上传/核验/发布；原Git源码、标签、资产均不改。外部helper和原publisher SHA、失败命令/原因记在logs/remote-recovery.json；upload.json另记上传结果。草稿按ID查询，发布后仍核对tag commit、latest、资产digest与正文。旧Release资产比较使用ID/name/size/digest/创建更新时间/URL，不把用户下载造成的download_count增加当成文件篡改；最终另与开工previous-release.json核对。生成本回执时用finish-with-recovery.py追加此实际异常说明，未修改已发布的finish.py源码。

## 资产

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Veyra-2.0.6-AMD-win64-portable.zip | 355334521 | 5e6f55a17214d5d03bb577b9a4fce14e90754854fef4891f4af9ef0416763275 |
| Veyra-2.0.6-NVIDIA-win64-portable.zip | 714362289 | ddc4c3a74508ceac19a9f829eff918d8b23fa43db5db8e4936effe7f9212cf6b |
| Veyra-2.0.6-source.zip | 70410696 | 6dbc254b3fec3e945fb418225bc8ef8d7b0f8af0ad7a5c50ede5955205823f89 |
| Veyra-2.0.6-dependency-source.zip | 598704862 | 4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9 |
| SHA256SUMS.txt | 398 | 412e298b1bb9ed8f91d46c2bd28b2db1508a0fb57481dedd66b02a7e9fd73dd1 |

依赖源码与2.0.5逐字节相同4eccde63…，仅改名2.0.6；patched FFmpeg、静态依赖源码及重链接材料保留。应用源码ZIP来自干净发布提交，仅排除一份历史tracked pyc；Git内原文件保留。源码无SDK/runtime/模型，用户包无测试provider/日志/媒体/个人profile/PDB/LIB。

## 保护与路径

执行回执：E:/项目/Veyra/logs/release-2.0.6-20261007/；最终资产：E:/项目/Veyra/releases/release-2.0.6-20261007/；独立解压：E:/项目/Veyra/verify/release-2.0.6-20261007/；构建/测试/tmp分别在同任务子目录。archives/release-2.0.6-20261007/start.json固定开工字节，source-before/final.bundle可恢复源码。guard复核33个其他工作树、495个已有修改及3678项旧产物/证据；main只允许本次合并与三文件收尾提交，不修改旧baseline。产品回归和编译记录见发布验收，发布接口异常及修正见下段。群公告写入GROUP_ANNOUNCEMENT.txt，只交用户，不代发、不关机。

## 数据边界

VFG +50.0%/+69.0%来自此前相邻匹配的RTX5070/GTA4K30/单层1080p NR/普通优先级对照；新2.0.6单轮复测单独记录，不拼成新因果百分比。计量是软件呈现提交FPS，不等于物理刷新/屏幕延迟/显存降幅。AMD图回归使用真实FSR3.1.5和确定性NR测试provider，不能冒充HIP推理。
