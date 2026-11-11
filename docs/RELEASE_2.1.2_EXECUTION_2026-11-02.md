# 2.1.2 发布执行记录 — 2026-11-02

用户明确授权将 Claude 最新去重修复包公开发布到 Likely7/Veyra-NRVideo，更新群码并整理2.1.1以来全部更新。未授权代发群消息；群公告作为文字交付。

产品来源358be8c、主EXE33d8e615…，只从不可变原ZIP复制，不读取正在运行的解压目录作供体。原两包及源码/配置保留。远端独有27d04bd仅更新star-history.svg，已正常合并，未覆盖远端。

发行前校验：供体ZIP摘要、每文件大小/SHA256、安全路径与精确manifest集合；原程序/QML/shader/runtime/model不变；源码档检查无SDK/专有二进制/用户数据；运行包更新说明、二维码和授权元数据，重新生成清单；发布后核对GitHub资产digest、源代码标签、latest以及两张二维码实际地址/宽度。

原构建测试：8组合同、QtQuick132/0/1、3组GPU功能短测、两包解压冷启动，见BUILD_2.1.2_TEST_2026-11-02.md。发布整理不改产品代码，不重复占用用户采集卡和真实配置。RX7000实卡不由RTX5070结果推定。

构建、归档、上传和远端校验的实际命令与结果写入E:/项目/Veyra/reports/codex-release-2.1.2-20261102/FINAL.json；完成后追加WORKLOG。源码包使用发布准备提交，产品目录必须与358be8c无差异。新包README链接包含完整迁移方法；源码Release保留固定依赖源码链接。


## 已公开并核验

2026-11-02，已发布 [2.1.2运行包](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2) 和 [对应源码](https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.1.2-sources)。Latest 指向 v2.1.2，均为正式发布，非草稿/预发布。发布准备提交 `899590f6864391b89fcb24d1842e88a41addb780`；产品构建提交358be8c，主EXE33d8e615…保持。

- Veyra-2.1.2-NVIDIA-win64-portable.zip: 676597909 bytes，SHA256 `34d91df21a6a6c5928c7e32e2ce4b17fa9deaf484e9e1e4f0aaf441c1ab21142`。
- Veyra-2.1.2-AMD-win64-portable.zip: 716543875 bytes，SHA256 `94dab9547146cc595436158ab64708c7d460a9d20da602d8adae2cc4a2462c92`。

应用源码 SHA256 `950fb21830918ba8e12a4c4ead75f60022c361d8074c830dd38de4ad2c14cb15`；新增网络源码 `0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48`。两个运行包只替换文档/二维码/发行元数据，旧payload新增/变动路径已记录PACKAGING.json。未改程序/QML/shader/运行组件/模型字节。原供体两个ZIP摘要校验并逐文件验证，未触碰原测试目录或用户设置。

执行：`git fetch nrvideo main --no-tags`、正常合并远端star-history；`git archive`生成899590f源码；`python -X utf8 -B scripts/acceptance/release-2.1.2.py`完成ZIP/路径/清单/模型/运行组件身份审计；`git push --atomic nrvideo main refs/tags/v2.1.2 refs/tags/v2.1.2-sources`；`gh release create --draft`、逐资产上传校验服务端摘要，先公开sources（非Latest），再公开运行包（Latest）。外部编排/回执在reports/codex-release-2.1.2-20261102。

远端核验通过：5个资产的GitHub SHA256、字节数和uploaded状态均等于本地；全部下载URL匿名HEAD为200；两张二维码实际匿名下载成功，各width220，新群码SHA256 fe347a447849bd7257ab5f343fa488e54a271ce88c4223c1b0773ccf383bc9ab与用户原图完全相同；README双语及support群码/有效期正确；发布标签解析为上述源码提交；4份固定依赖源码服务端SHA256与声明一致。

发布编排曾遇REST按tag查询草稿返回404，已改用认证release列表取得ID后操作，未误报发布成功或修改二进制。产品源码无变动，沿用Claude同一构建的针对性测试，没有重复占用采集卡。保留最新正式ZIP及对应源码、摘要和必要报告；本轮无解压副本，新增发行文件约1.37GiB，不声称测量了物理回收量。原测试包/用户配置保留。

群公告990字符，保存reports/codex-release-2.1.2-20261102/GROUP_ANNOUNCEMENT.txt，仅交付文字，未向群聊发送。
