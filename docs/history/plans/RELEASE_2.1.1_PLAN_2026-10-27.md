# Veyra 2.1.1 GitHub发布计划（2026-10-27）

用户明确授权发布到GitHub，整理2.1.0以来Claude/Codex已完成且入包的修复，更新报告只介绍实际交付内容，发布后提供1000字以内群公告，不代发消息。允许本轮发布必要的main普通整合、push、v2.1.1及v2.1.1-sources标签/Release/资产上传；覆盖此前仅本地交付限制。Lecram及独立软件UI默认分支不混入既有已验包，不重编/重压缩用户包，不改运行组件、驱动或用户设置，单Agent/全E盘。

起点00d7d2d，已验包应用源码快照89116ba。原refs/工作树HEAD与远端元数据存archives/reports/release-2.1.1-20261027，工作分支codex/release-2.1.1-20261027。远端main新增c525379只改Star图，普通merge保留。新正文/双语README/来源索引，不改产品逻辑；通过Git diff核对最终产品与应用源码快照一致。

1. 按v2.1.0..00d7d2d全部提交及对应验收整理功能，不把独立分支/仅诊断事项写成已修。
2. 使用已验NVIDIA/AMD两个ZIP原件；核对SHA/CRC/清单，源码与依赖来源哈希匹配。正式归档只复制这两个原ZIP和应用源码，不重新构建/打包。
3. 更新双语README及RELEASE_NOTES_2.1.1，保留支持区块的交流群/赞助图及各width=220。核对远端图像可访问，保留图片所示有效期。
4. main普通快进，普通atomic push主线及两个新tag；保留旧分支/tag和其它工作树。草稿上传并按服务器size/digest/state逐项核对；源码页非latest，主Release只两个运行ZIP并设latest。
5. 发布后复查latest、正文、两个二维码及资产；输出最终URL/哈希与1000字以内群公告。新产物E:/项目/Veyra/{releases,reports,logs,tmp,archives}/release-2.1.1-20261027。

## 2026-10-27 用户追加：整合软件绘制并重建

前阶段发布因用户要求查看分支而暂停，未push/上传/创建Release。用户现要求合入claude/software-ui-default-20261026并将默认SR模型改RTX视频超分，然后重新构建2.1.1发布。覆盖此前原样发布不重建约束。先保存发行文档脏改动，再正常merge保留Claude历史，保留当前菜单/HDR/事件查询修复。新任务codex-release-2.1.1-final-20261027，新staging/构建回执/源码及交付ZIP；此前2.1.1包不作最终发布资产。

默认SR只改变未明确选择模型时的选择，不自动打开超分；AMD/Intel继续原FSR选择。验证新配置/既有DLSS及FSR选择/预设与节点默认、软件/GPU实际重启切换、事件查询与菜单/HDR回归。复用同源build，-j2<=900秒、单测试<=300秒；最终按新ZIP/SHA/来源/依赖闭包/解压冷启动审核，更新正文/双语README，main普通合入后发布。
