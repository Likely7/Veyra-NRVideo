# 2.1.9《迎接GTA6降临》发布执行记录

用户确认测试正常并明确授权发布。应用：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.9，GitHub发布时间2026-11-15T15:51:51Z，名称`Veyra 2.1.9 ·《迎接GTA6降临》`，正式非预发布并设Latest。对应源码：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.9-sources，不抢Latest。两tag固定发行提交`d442b4e5ac5907fffd4c20e9e876a324708750f2`；实际产品`b430d986c2adace6eae6cd2893fbb7a491473aaa`，EXE `c8a53dc778331c696f5a488400885ef842adb849e1b22a9fbea07520a9ecb8f0`/PE2.1.9.0。发行仅改README/说明/12群原图和包元数据，没有重编译已验程序。

## 交付身份

| 文件 | 字节 | SHA256 |
|---|---:|---|
| Veyra-2.1.9-NVIDIA-win64-portable.zip | 731012476 | `4225ecb11a8e96c2826b4b0b798feb811d7ae237f3edf0b627f3b6895c79f362` |
| Veyra-2.1.9-AMD-win64-portable.zip | 722006907 | `f7113db09eff74065cfd9b7044df8db3c301ac5b7ff80f692da56fee2c2bf377` |
| Veyra-2.1.9-source.zip | 79324853 | `b3dfea5f369375823a29be1326d3420b679100da24f1d33072e02f5933b60cd0` |
| Veyra-2.1.9-rdna3-network-source.zip | 29191 | `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48` |
| SHA256SUMS.txt | 401 | `3d08ef83b2b8d0c2f1d2eae24f0d94cc738f9034c4df010bd278145772916602` |

两个公开包1695/2319个manifest载荷文件、PE导入/许可/模型/88个递归shader、原BGM3005aa60…1847011字节、无ANR资产检查通过；原DLL和已测EXE保持。音乐仅运行包，源码ZIP无MP3/DLL/SDK/模型；应用源码含本次发布文档但生产文件与b430d98相同，RX7000网络源码沿用原件。依赖源码四链接可访问，patched FFmpeg保持。

## 实际发布与验证

脚本E:/项目/Veyra/reports/codex-release-2.1.9-20261115/init.py、docs.py、freeze.py、package.py、cold.py/fix_cold.py、upload.py、body_links.py、verify.py、finish.py；命令均py -3.11 -X utf8 -B -I脚本路径（verify分别docs/publish/published）。读GitHub连接器后发现无Release上传接口，使用本机gh/API。先本地main快进、git push --atomic nrvideo main v2.1.9 v2.1.9-sources，再草稿上传、逐资产大小/服务端SHA核对，最后先公开sources再应用设Latest；未推origin/私人检查点，不代发群消息，不关机。

两个ZIP重新解压、最小PATH播放/公开UI四场景退出0且无错误，NVIDIA/AMD分别208/205真实提交帧，ANR不可用且未假报加载、回放/麦克风默认关、开关/HDR折叠检查通过。冷启动脚本最初提前于封包完成读取PACKAGING失败，未运行产品；随后第一次模板把测试片误改到不存在的2.1.9路径，产生打开错误，原JSON和日志保留，恢复实际已有片源后四组重跑通过。未改变产品或放宽断言。发布正文的两个相对文档链接改为固定tag绝对链接；草稿补正文后tag_name变为untagged，发布前严格检查拦截，repair_draft.py显式带完整元数据恢复v2.1.9再公开，资产ID/摘要未变。该修复和最终正文均已核对。

应用/源码Release正文与本地发布文件一致；原图12群SHA与本机用户附件相同，11月22日前有效，群/微信赞助两图各width220且可访问。双语README与两tag核对一致，作者X/YouTube和英中教程保留，英文从0开始。匿名入口HTTP200、ZIP前缀抽样可读及摘要全量GET匹配；完整ZIP本地逐文件SHA并与GitHub服务端SHA匹配，未冒称两个大包匿名完整重下。2.1.6两Release正文、标签及资产身份不改，下载计数自然变化忽略。

已清理本轮解压验证副本，逻辑文件大小3773679506字节；公开完整app/ZIP/源码/当前build及必要证据保留。实际路径和检查在CLEANUP.json。群公告669字符，文件RELEASE_ANNOUNCEMENT_2.1.9.zh-CN.md；没有独立Reviewer，不把自检说成独立审查。硬件与性能边界见RELEASE_VALIDATION_2.1.9.md。
