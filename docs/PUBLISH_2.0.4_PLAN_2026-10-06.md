# 2.0.4 正式发布执行（2026-10-06）

用户授权正式发布、更新社区/赞助入口、交付群公告及最后关机。当前隔离分支codex/publish-2.0.4-20261006，路径E:/项目/Veyra/worktrees/publish-2.0.4-20261006。

- 从本地main 0a4e761接入远端main f652c93；合并7630268保留远端新页头、Star History和工作流。
- 原22个工作区/未提交字节及main、远端引用封存到E:/项目/Veyra/archives/publish-2.0.4-20261006。before.bundle已验证。新checkout的CRLF差异按已测工作区恢复原字节；473个生产输入SHA完全相同，git diff无生产变更。
- 程序沿用列表预设纠偏最终构建，EXE SHA256 d01329aa6d9fbd6ac2d1eee294782f4cb9d233490595ed2e3e1a9ea12c16b2d6，PE版本2.0.4。原AMD/NVIDIA测试ZIP不改。
- README只替换当前版本更新/下载及必要过时版本状态、支持入口；原教程、图片、远端页头与Star History保留。两个二维码保留原地址及width=220；新增Discord官方样式徽章和Ko-fi官方咖啡按钮。
- 从已核验候选manifest白名单生成正式NVIDIA/AMD ZIP，更新发布说明与源码地址。原DLL/模型/许可证/运行时manifest逐字节保持。应用源码对应正式提交；依赖源码保留原2.0.3依赖包并补齐实际libass/字体依赖源码、vcpkg配方与构建信息。源码不含专有SDK、模型或二进制。
- 正式包逐文件/CRC/PE依赖/显卡分包检查与干净解压启动；473生产输入冻结，因此复用已有功能验收，不重跑无关GPU测试。发布前检查群入口与两个二维码；Ko-fi页面自动访问403，但官方按钮200，按用户给定账号保留链接。
- 只向nrvideo推送main及v2.0.4，先建draft、上传5项资产、核对远端大小/SHA256/正文后正式发布为latest。保留源分支与旧Release。记录实际发布状态，失败不能报成功。
- 产物统一E:/项目/Veyra/{releases,logs,tmp,verify,archives,tests}/publish-2.0.4-20261006。群公告≤1000字；全部发布核查完成后安排正常关机。

性能报告取自已完成的三轮匹配A/B数据。重建/暂停编辑/进程GPU利用率和正常处理区间分别标注，不能宣传显示延迟或所有显卡FPS提升。真实Xbox/AMD NR、受影响实卡及物理显示边界沿用原验收限制；HDR/Dolby PR13/14暂缓。

## 已执行的正式发布回执

发布source/tag/main起点67b6dd63be6b797ca445ba810dad0227c890a98c，main由0a4e761快进；Git只向nrvideo推送main/v2.0.4。Release ID404229374，2026-10-06T01:58:03Z公开为latest，非prerelease；URL https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.4 。本节后续只补文档，不改tag或原资产。

| 资产 | bytes | SHA256 |
|---|---:|---|
| Veyra-2.0.4-NVIDIA-win64-portable.zip | 714170752 | abc556d94f53d8ae873de2f7a14a78dcc7504b361c8815bebff7123659d3203a |
| Veyra-2.0.4-AMD-win64-portable.zip | 355142873 | 1dd9cd5fc5f16e9c30935499190b0f9fcf5ddefee87e7dec68a88664ebf21978 |
| Veyra-2.0.4-source.zip | 69928876 | 6febee1fd1a58aff182dafc62d67fb50d71fd831c361053c1bdf16305c134cd6 |
| Veyra-2.0.4-dependency-source.zip | 598704862 | 4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9 |
| SHA256SUMS.txt | 398 | 2cbeccd4d6a96cc82e4ccac30216689dfd78d41cdae59e3a90ff5e648c4205b6 |

package-v1退出0，全部ZIP CRC/载荷SHA、source ZIP逐文件与实际Git工作树、PE imports/delay imports及分包边界通过；AMD2045/NVIDIA1596总文件，原组件/EXE不变。cold-v1退出0，两例新解压目录/隔离profile/Windows-only PATH各约10秒，退出0、26个Qt/FFmpeg包内模块、ASS五事件/字体附件正常。source-final.bundle verify通过。未新增模型推理/硬件测试，复用已验收同一程序与473原输入。

gh draft验证5资产state uploaded、大小/服务器SHA与本地完全一致、正文和两个README/标签/main相同。gh release edit --draft=false --latest后，public验证同样通过；五个浏览器下载URL均HTTP206且ZIP头正确，完整SHA256SUMS相同。正文GitHub渲染检查保留两QR的原地址/width=220和Discord/Ko-fi官方咖啡按钮；远端全文与RELEASE_BODY.md一致。日志remote-draft.json、remote-public.json、verify-public.log及release-body.html。

最终群公告596字符，releases/publish-2.0.4-20261006/GROUP_ANNOUNCEMENT.txt；只交付用户。正式ZIP、完整可运行目录、解压启动证据和失败源码包留在本轮E盘目录，不触碰旧包或用户文件。关机必须在发布核实/回执推送完成后安排，实际进程/时间记入logs/.../shutdown.json。
