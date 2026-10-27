# Veyra 2.0.7 发布回执

正式发布：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7，Release ID 410800361，published_at 2026-10-12T16:24:59Z（台北时间 2026-10-13T00:24:59+08:00）。非草稿、非预发布，latest经API核对为v2.0.7。独立源码归档：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.7-sources，ID 410800322，不设latest。

## 包、源码与标签

用户明确“直接这个包”，采用原已验test-r2 ZIP原字节，仅外部文件名去test-r2；包内显示/测试说明/manifest原状态保留，发布正文已明示，不重新构建或伪称另一个EXE。NVIDIA1641/AMD2081 payload逐项SHA/size/CRC及runtime48/534、原EXE和四真实双renderer冷启回执匹配。公开包服务器digest/size均等于原件：

| 文件 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-2.0.7-AMD-win64-portable.zip | 355675904 | 24ea0ed5f8ce6a65d3769e946a870c2eda3383c30864ead612fc3af673b46a2d |
| Veyra-2.0.7-NVIDIA-win64-portable.zip | 714741907 | 3105d62545a76b6e971c1e32f094bb312d5af22332e310c93c19f28550fd9036 |

应用数字版本2.0.7.0，显示2.0.7-test-r2，SHA256 `6fd35109d3c668f51fd7ce1609c8176e164227e2510473ef5a6276a34b1d713d`。发布main/tag提交 `6ede5a7e1c09f919d1145c78af82571c1053db10`，发布文档分支ae7762b；完整产品树仍为已验67ab696，源归档a94a1ce与67ab696整树相同。两个标签均指向该发布文档提交；本回执随后只推进main文档，不移动标签/修改资产。

应用源1970文件逐项核对，patched FFmpeg9.0.1/dav1d/zlib源SHA/CRC通过，PS5 slices256及压缩PGS所需zlib保留。独立源码Release两资产，未改变的598704862字节完整依赖源保留v2.0.6固定公开链接、SHA4eccde63...及重链接材料，无重复上传。新源不含专有SDK/runtime/模型，当前push新增文件扫描通过。

## 文档和公开入口

README中英/完整双语更新/变更索引/构建源入口已推送。索引覆盖v2.0.6至67ab696全部91个非merge提交、80个最终产品路径，含撤回实验与边界。VFG按单张中点前档、其余后档解释；4X三张/8X七张、2X无混合收益，NVIDIA原模型与本项目最新研究的分配策略分开。内容节奏整图±3噪声容差与固定隔帧分开；XeSS只称有限改善、保留成本/负例和非AMD实卡边界。字幕/列表/音频/设置/呈现/UI/采集/诊断均包含。

六群PNG与用户原图逐字节SHA `a237d642efbe3e83294b607cf2ae2937f51f63549eddd356f819481606647844`，tag固定raw URL返回200且字节相等。赞助图/Ko-fi图返回200，原Discord/Ko-fi地址及双QR width220保留，二维码标注2026-10-19前有效。知识库原完整URL在README/主Release/源码归档support中保持；带Cookie的匿名请求200并跳转实际wiki入口，未冒称完整阅读库内资料。最初无Cookie的HEAD404/GET重定向循环和web不可访问保留日志；只是自动访问方式差异，没有改成猜测的新地址。

remote.py按已核实ID创建草稿、上传源/双包、核对各自资产集合/服务器digest/size/正文，再发布源码非latest、主版latest。公开7项文档/图片及两个Release页/四新资产/旧依赖源链接检查均200。主Release上传资产恰两项（NVIDIA/AMD）；GitHub自带的Source code链接由平台生成，不属于额外运行资产。原v2.0.6 ID/正文/时间/资产ID/name/size/digest/创建修改时间逐项保持，下载计数自然变化不作为修改。

## 验收与限制

本轮audit.py只复核原包、源码和发布文档，不重做无根据GPU测试。原刷新版16组/100时间断言/3回放/4解压启动以及首轮28组/52几何状态分别引用；没有声称全部测试通过：扩展NR+DLSS热切换31/34、原2.0.7同场33/34失败仍记录。NV长期显存增长、BMD/Elgato/HDR现场及AMD/Intel/屏幕光学延迟边界保持。混合档−39.6%包含低档模型取舍，不是同画质提速；原2.0.6已发布80→120数据未重复宣传为本版收益。

69旧工作树中只允许main按用户授权推进，另外68树/509脏文件及全部原交付ZIP、其它原分支tip保持；protect.py verify通过。全部新产物在E:/项目/Veyra/{archives,logs,tmp,reports,releases}/release-2.0.7-20261013。最终ZIP和两源以硬链接复用原不可变文件，不另复制/解压整工程；源码before/release bundle均verify。632字符群公告已存releases任务目录/GROUP_ANNOUNCEMENT.txt，仅提供用户、不代发；没有关机或修改用户正在使用的播放器/配置。

发布辅助检查有两次失败，未掩盖：第一次公开资产/正文检查已经通过，但读取旧Release快照时误写Python编码别名utf8-sig，LookupError；只改为utf-8-sig，原脚本存archives，重新只读检查通过，未再次发布。第二次URL检查提前于其依赖remote-final.json完成，FileNotFoundError；等依赖实际完成后顺序重跑原检查通过。两份失败说明在logs，未修改断言、产品或资产。

真实回执：reports任务目录assets.json、remote-final.json、public-links-final.json、knowledge-base-check.json、protection.json；logs目录保留草稿/上传/逐字正文/发布结果。最终main收尾commit与保护再核对记录在final.json，不为写入自身hash反复改发布源码。
