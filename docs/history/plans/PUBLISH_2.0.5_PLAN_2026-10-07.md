# Veyra 2.0.5 正式发布计划

用户在本地测试交付后明确授权：“发布吧，2.0.5 老样子发完给我弄个群公告”。允许本次main合并、普通推送、v2.0.5标签和GitHub正式Release上传；群公告只交用户，不代发，不关机。不继续显存假说施工，不修改产品代码、DLL、模型、驱动、用户配置或其他工作树。

源起点main54740646cbc6b416beeda0cf92ffa8c39f66ae32；隔离工作树E:/项目/Veyra/worktrees/publish-2.0.5-20261007，同名codex分支。发布前fetch确认远端nrvideo/main为578d63c，v2.0.5标签/Release不存在。独立开工封存28个既有工作树、495个未提交文件、1724个源输入与53份历史/本地交付证据；start SHA256 a94e3e83c8b80dc16adb1e65c68b508fb9e56f3af5b944b45c264b516c6c9c0d。每轮先运行scripts/acceptance/publish-2.0.5-control.py，旧baseline/guard不改。

1. 仅替换README当前更新区和必要版本/已失效限制；保留其他内容、Discord、Ko-fi与双二维码width220。写双语详细Release说明，分别标注当前验收、fix2匹配性能证据、AMD用户反馈及未验证项。NV显存持续增长明确未修复、下版排查。
2. 冻结当前已测2.0.5生产EXE acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936。两正式包从本地2.0.5逐文件manifest复制，更新发布文档与manifest；运行组件/许可证、EXE、QML、shader和依赖字节保持。不因文档发布重建产品或重复GPU性能测试。
3. 提交文档/发布脚本，--no-ff合入main并验证树完全一致；记录允许推进的main收据。源码ZIP来自精确发布提交，排除唯一历史tracked pyc但保留原Git文件；检查全清单/CRC/每文件SHA，保留Git bundle。依赖源码与2.0.4资产字节相同，改名2.0.5，SHA4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9。
4. 两正式包逐文件SHA、跨厂商边界/PE依赖审计、全部ZIP CRC/载荷核验。各从最终ZIP新解压，私有profile和仅系统PATH启动，检查Qt/FFmpeg模块来自各自包。每测试进程≤300秒，无压力/竞争负载，不派Agent。
5. 正常推送nrvideo/main及v2.0.5标签，先建草稿，上传NVIDIA/AMD、应用源码、依赖源码与SHA256SUMS共5资产。核对远端digest/大小、标签指向、正文和支持区；通过后改为正式最新版本，再核对公开下载/QR可访问性。无force push或覆盖旧资产。
6. WORKLOG/正式回执记录实际结果，群公告≤1000字，量化数据带本机条件，不把UI提交减少冒充延迟/显存降低，不把RX9000预览反馈扩展为AMD离线编码验收。

产物统一E:/项目/Veyra/{archives,logs,releases,tests,tmp,verify}/publish-2.0.5-20261007。实际GitHub ID、提交/资产SHA、冷启和线上核验以任务JSON回执为准。保留当前构建/最终包/必要证据，不重试此前自动审批已拒绝的清理。
