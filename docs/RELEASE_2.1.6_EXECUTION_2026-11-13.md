# Veyra 2.1.6 发布执行记录

用户明确要求整理2.1.5以来全部更新与数据并发布GitHub。应用Release：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.6，publishedAt 2026-11-14T05:50:21Z；对应源码：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.6-sources。应用设为Latest、sources不抢Latest。发行tag v2.1.6/v2.1.6-sources均固定 `7244307b7930a8e742831ee59d5512d512b825e2`；精确应用源码 `f46df7f7cec4e8d2df3b8fc7bcbc9b2e9d7ad54e`，EXE `bf27ebed5d81a79814441671e933c7a8ef6be584c784f75b8ec80493642c10f3`/PE2.1.6.0。后续只追加本验收文档，不改tag/二进制。

## 内容与来源

全部更新见[双语说明](RELEASE_NOTES_2.1.6.md)及[测量条件/限制](RELEASE_VALIDATION_2.1.6.md)：回放/F9、麦克风混音/独立轨、全效果剔除/NR节点、自动/半速去重与实时送帧/日志、精修光流、GPU/VFG默认值/Boost移除、HDR收纳/美颜删除/PSNTools及社区支持更新。阶段A/B、CPU成本、重档真实帧延后与过载探测代价同时列出，不冒充所有数据来自两正式包直接比较或光学延迟。

ANR协作者2026-11-13于Issue9明确禁止当前Beta第三方再分发，公开包不带任何ANR运行库/模型/配置/程序包或生成桥接loader；适配源码保留，未做自动下载/镜像。本机完整ANR实验包和用户配置不改。其余已批准组件原字节/许可保留，源码和运行二进制分离。

## 资产身份

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Veyra-2.1.6-NVIDIA-win64-portable.zip | 725702391 | `c55fbb2eb7395f29c997d0f744928f645f991729ff722f95f1d90394f80d0968` |
| Veyra-2.1.6-AMD-win64-portable.zip | 716696981 | `5dd97d7413a1bb1a630dbc6a7d0669677b779b2bfb36272a94940f06a099f094` |
| Veyra-2.1.6-source.zip | 75319410 | `f1d365fe9560d566574dbf8c5e337b6427631f0858a857524dcb113f8c2851c9` |
| Veyra-2.1.6-rdna3-network-source.zip | 29191 | `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48` |
| SHA256SUMS.txt | 401 | `b7ddf936b9ed8680a1db174d11b2758a4c71dcedb6eb980965935a3b5ed585f1` |

两包ZIP逐成员SHA/CRC、PE导入、模型/许可、全部88个shader、无ANR原件哈希和无用户数据检查通过；公开包独立解压、最小PATH播放213/221真实帧、0错误，实际UI验证ANR不可用且不假报加载、回放/麦克风默认关、顶部开关和HDR默认折叠。当前EXE沿用已验证bf27ebed，不用发行文档改动重新制造性能成绩。精确应用/网络源ZIP从已验证包复制并校验，固定依赖源码四链接可访问。

## 发布操作与验证

工具连接器用于读取最新Release、远端main及ANR作者声明；因没有Release写入/二进制上传接口，使用本机gh。脚本位于E:/项目/Veyra/reports/codex-release-2.1.6-20261113：init.py、docs.py、package.py、metadata-adjustment.py、cold.py、upload.py、verify.py、finish.py。源码常规快速前进合main，git push --atomic nrvideo main v2.1.6 v2.1.6-sources；未推origin或私人检查点。先创建草稿/上传，再核对每个资产服务端大小/SHA和逐字正文，随后PATCH公开并设置应用Latest。

所有公开下载入口HTTP200，ZIP前缀匿名抽样可下载，摘要匿名全量GET匹配；完整ZIP已在本地逐文件哈希并与GitHub服务端SHA一致，未声称匿名重新完整下载两个大包。两Release正文与预备文件逐字一致，赞助/10群两二维码各width220、图片字节/标签与README检查通过。2.1.5原正文/标签/资产ID、大小、SHA及应用/网络源码保持，下载次数自然变化不作为修改。

为修正包内根目录更新说明到验收文档的相对链接，追加同内容MD别名并更新manifest。已有ZIP本地记录原字节复制，仅zipfile重写中心目录及两个文本成员；全部成员重新解压SHA核验通过，不改任何运行二进制。没有把未验证的ZIP操作当通过。GitHub上传成功不冒充功能或其他GPU验收。

29原工作树HEAD/状态/脏文件SHA核验，main只允许本次授权推进。仅本轮两解压验证副本在路径/父链reparse/进程检查后退役；记录CLEANUP.json，公开完整app/ZIP/对应源码/当前build及必要证据保留。本机原ANR包、用户配置、依赖/Junction、正式2.1.5及其他版本保持。单Agent自审，不声明独立Reviewer，不群发，不关机。

最终事实与服务端响应在reports同任务FINAL.json；实际上传日志在logs同任务。
