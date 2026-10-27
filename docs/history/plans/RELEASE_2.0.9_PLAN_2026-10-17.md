# Veyra 2.0.9 发布计划

用户在2026-10-17明确“发布吧，老样子发好给我群公告”。授权将已交付的2.0.9-test NVIDIA/AMD包正式发布至Likely7/Veyra-NRVideo，必要源码main快进与普通推送、新版本标签及Release文档。继续采用已验ZIP仅改外部文件名的方式，不重新构建或封包；群公告只拟稿，不代发，无关机/子Agent授权。

产品提交0ac9d39，745构建输入/52EXE；发布起点bfce70b，main b571a44。封存应用源c9e779b。原NVIDIA ZIP677304267bytes/SHA6618461d722428aff65e8eee1847cd673aaa02d6c00beab87b7e661b97e0f843，AMD356014225bytes/SHAe9224fb7f546253e351d2262168a8cedd3c80319f68379392699ecb439c6cc83；EXE7add56b9651d6b629433419fb0d2d9d077057d9a77595ebeabbf4d5fb3b80f80，内部2.0.9-test、PE2.0.9.0与原清单保留并明确说明。原DELIVERY/cold-start与final回执在nr-sf-310916-20261017对应E盘目录。

1. 先存档83旧工作树/95分支/509脏文件、六原归档、旧2.0.8主/源码Release、固定FFmpeg/依赖源资产，bundle verify；从bfce70b建立独立codex/release-2.0.9-20261017，旧分支和工作树原样保护。
2. 覆盖v2.0.8以来全部非merge提交与最终产品diff。重点SF310.9.16双模型/defaultB与真实GPU阶段收益；完整列出PR44 HDR五卡/记忆、2060 NVOF同步/失败状态、粉丝字幕和鼠标穿透快捷键、issues45/43/42/41的VRR/英文布局/地址播放/去隔行及最终构建头文件问题。中英README仅改当前版本/新增内容/下载/现行NR和快捷键说明；保留现有六群/微信赞助/Discord/Ko-fi/知识库区块。
3. 原样ZIP与应用源码硬链接为正式外名，核验ZIP逐文件SHA/CRC、清单、PE/EXE/运行组件、745构建输入和四次解压冷启证据。新独立去隔行源码单独上传到v2.0.9-sources；不变的字幕FFmpeg源与依赖源引用既有固定资产并核对服务器digest。
4. 主v2.0.9仅NVIDIA/AMD两资产；对应源码v2.0.9-sources不设latest。干净main正常快进，atomic普通push main与两个新标签，不移动旧标签。草稿上传后核对服务器digest/size/正文，再发布源码、主版latest。
5. 核验远端main/标签/latest、两主资产/两源码资产、README/更新报告/构建文档、两张width220二维码实际可访问性。发布完成后只追加回执，不改产品或包；最终保护复核与source-final.bundle完成，提供1000字以内含下载/知识库链接的群公告。

全部新产物在E:/项目/Veyra/{archives,worktrees,tmp,logs,reports,releases}/release-2.0.9-20261017。RTX5070测得NR GPU耗时下降45.49%不等于所有显卡同画质/FPS/光学延迟收益；20/30/40与AMD实卡、长期显存/真实VRR继续按既有边界说明。实验运行库原身份/许可与源码分离保持，不改驱动/真实用户配置，不代发评论或关闭issue。
