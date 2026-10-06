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
