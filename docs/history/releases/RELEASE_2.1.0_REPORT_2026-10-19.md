# Veyra 2.1.0 发布回执（2026-10-19）

正式版：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0，ID 417022047，published_at 2026-10-19T07:43:22Z；API确认非草稿、非预发布且latest=v2.1.0。源码页：https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.0-sources，ID 417022025，非latest。两个新标签固定3b295e89b5151c69a121ffc9e198c505bb9ac7ee，后续不移动；main已包含双方此前修复与发行资料。

用户指定bilibili-2.1.0-20261019两包原样晋升，仅去除外部文件名-test，不重新构建/压缩，内部2.1.0-test/原目录/清单保留，PE2.1.0.0。应用精确源码66b3210，EXE SHA256 5b8a8920c4c67a4e1760e26b6c4f197ed532d429cca8effd59526642d7a03a65。

| 资产 | 字节 | SHA256 |
| --- | --- | --- |
| Veyra-2.1.0-NVIDIA-win64-portable.zip | 679283725 | `259a800768e24ee6b8b03e6ee537b5e2a1a7b9f60e7b39c6573055786da33391` |
| Veyra-2.1.0-AMD-win64-portable.zip | 465926556 | `e9ad547c0711f03d8277a47cae5e100b8817ee0da496916d56770d1c4927689e` |
| Veyra-2.1.0-source.zip | 72524010 | `d3ed0860cebef310eeceb5fafd790b553f78cecdf2d393e9f4c4d6ca87b32559` |
| lmxxf-044-corresponding-source.zip | 1285780 | `f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b` |

主Release恰两个GPU包，源码Release恰应用源/AMD对应源两个资产，既有FFmpeg/去隔行/依赖源使用核验后的固定下载链接。远端所有资产uploaded、size/digest逐项等于用户原件；未覆盖任何旧Release/标签/资产。

更新说明人工覆盖AMD0.44双模型、AA、468点美颜、PR57采集格式/窗口、#48网络、PR63/64音频、全屏操作、效果链/导出及赞助页；另提供86提交/223路径完整索引。没有把5070或模拟ABI当成AMD/圆刚/BMD实卡通过，也没有增加未经测量的性能百分比。Lecram实验保留暂停，未合入本版。

两README和两Release已使用用户新6群原图；发布后重新GET核对图片200及SHA，赞助码GET200；双图各220宽、Discord/Ko-fi/知识库保留。原包内文档因用户要求不封包而保持；软件交流群入口跳仓库最新README。群公告793字，仅拟稿未代发。

命令：git普通fast-forward main、atomic push main与两个新标签；gh release create --draft --verify-tag/gh release upload；python -X utf8 -B scripts/acceptance/release-2.1.0.py remote；核对草稿后gh release edit先发布源码latest=false、再主版latest=true。没有重新跑GPU验收，也没有将既有短测冒充本轮新测；本轮新增只做发行校验，git diff --check及文档链接/图片/编码检查通过。

全部回执在E:/项目/Veyra/reports/release-2.1.0-20261019：local.json、dependency-digests.json、support-images.json、remote.json、final.json；原DELIVERY/refs/旧Release元数据在archives同任务目录；releases同任务目录的四文件为原文件硬链接，没有再复制约1.2GB。用户原ZIP/目录保持，无SDK/DLL/模型新增Git，无用户配置/驱动修改、子Agent、代发消息或关机。
