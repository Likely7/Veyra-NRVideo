# 2.1.0 B 站主页纠正（2026-10-19）

用户提供的正确地址：`https://space.bilibili.com/19500532?spm_id_from=333.1007.0.0`。
原地址的账号 ID 少了最后一位；HTTP 200 只能说明页面可访问，不能证明账号正确。
本次修正 `QmlPlayerBridge::openSponsorLink`、既有赞助页验收预期和赞助页说明。
基于 Claude 已整合的 main `e3453c2`，不改变增强链路、UI 刷新频率、版本号、运行库或模型。

构建沿用 `docs/BUILD_2.1.0_TEST.md`，工具为
`E:/项目/Veyra/tmp/bilibili-2.1.0-20261019/tools/build.py --check-clean --jobs 2 veyra_qml_ui`。
应用构建通过（234.8 秒），回执 `E:/项目/Veyra/logs/bilibili-2.1.0-20261019/build-20261019-122231/receipt.json`；
编译产物包含完整正确 URL，不含以空字符结束的旧错误 URL；见 `reports/bilibili-2.1.0-20261019/compiled-link.json`。

按 Claude 原 `package210.py stage|seal|cold` 配方重打 NVIDIA / AMD 两包，
显示版本仍为 `2.1.0-test`。工具副本只调整任务目录、源码工作树和本条记录路径。
输出：`E:/项目/Veyra/test-packages/bilibili-2.1.0-20261019`。原包保留。
打包审计、ZIP 哈希、冷启动及播放结果以输出目录的 `DELIVERY.json`、`cold-start.json` 为准；
这些运行结果在封包后产生，不预先宣称通过。没有 GitHub 推送或发布。

## 封包后结果

`package210.py stage`、`seal`、`cold` 全部通过；两个包各以 D3D12 与软件界面冷启动，
GTA 4K 视频各播放 14 秒，实际提交 399–404 帧，无检查到的 QML/GPU 错误。
AMD 包是在本机 NVIDIA 显卡上检查启动和播放，没有重新验证 AMD NR 推理。
`tmp/bilibili-2.1.0-20261019/verify.py` 对照 Claude 原两包：仅 EXE 和文档有差异，
QML、着色器、运行库、模型逐文件 SHA256 相同；报告 `reports/bilibili-2.1.0-20261019/final.json`。
ZIP 与源码绑定到封包提交 `66b3210`；本条验收结果为封包后补记，不改变包内代码。
