# 更新检查（2026-10-13）

用户要求真正检查更新，每次启动自动检查；有新版本顶部短暂提示，无更新静默。起点 main7571919，隔离 codex/update-check-20261013；70旧工作树/脏文件及增量源码已存档 E:/项目/Veyra/archives/update-check-20261013。

只改 UI 更新服务、bridge、QML、翻译、必要 CMake/验证。无需修改引擎、运行库、模型、用户设置或33ms/250ms刷新节奏。新网络模块由现有 windeployqt 根据 imports 部署。产物全部 E:/项目/Veyra/{build,tests,logs,tmp,reports}/update-check-20261013。不派Agent、不合 main/push/发布或关机。

实现：界面就绪2.5秒后单次异步 GET GitHub /repos/Likely7/Veyra-NRVideo/releases/latest，无凭据；12秒总超时、128KiB响应上限、不重试。只接受非草稿/非预发布的 v数字.数字.数字，数值比较（当前 -test-r2 标识归一同版本），验证链接属于当前仓库正式标签。有更新显示8秒顶部提示，可点击发布页/关闭，设置页保留下载入口。自动无更新/失败均无提示，手动显示最新/失败/可用结果；并发点击合并为一个请求。

验收：独立本地 HTTP 服务覆盖新/同/旧版本、10与9数值比较、预发布/源码归档/无效JSON/外部URL、404/403、总超时、超大响应、自动静默/手动、并发/销毁；真实匿名 HTTPS GitHub 查询验证。Qt Quick 测顶部弹窗显示、8秒关闭、点击/窄屏；完整产品构建和私有配置启动。不得用本地夹具冒充真实远端查询，不生成完整测试包。

参考：GitHub REST releases latest https://docs.github.com/en/rest/releases/releases#get-the-latest-release；Qt6.8 QNetworkAccessManager https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html。

## 已完成验收

- 产品代码已提交到本地隔离分支，实测产品提交 `9d5a42e`。新服务使用 Qt Network，已有 windeployqt 部署出 Qt6Network 与 Windows Schannel TLS 插件，正常验证证书，不携带凭据或下载二进制。
- 首次空目录完整构建5目标、504步骤、125.0秒成功；后续测试夹具修正与请求日志追加做增量构建，最后17.7秒。辅助脚本沿用旧“fresh source build”打印/字段，只有首次为空目录，不能把后续增量称为全量。最终 EXE SHA256 `3e98899bb5ebb5bdd8843d9043e561727717f7dc46d356141a4dfc4cc99f97ea`，显示2.0.7-update-test。命令 `python -X utf8 -B -I E:/项目/Veyra/tmp/update-check-20261013/build.py --task update-check-20261013 --label 2.0.7-update-test`。
- `veyra_update_checker_tests.exe` 29项（含23数据行，各自动/手动两次）通过：新/同/旧及数字10/9、当前test标识、预发布/源码标签/草稿、非法JSON/字段/URL/整数溢出、404/403/128KiB上限、超时与请求合并、启动一次、手动提前检查、请求中销毁。
- 同一个产品服务 `veyra_update_checker_tests.exe --live` 无凭据访问真实GitHub成功，结果current（本地2.0.7-test-r2，与正式v2.0.7相同）。新版本提示使用本地真实HTTP应答验证，没有为测试修改GitHub版本。
- Qt Quick `UpdateBanner::test_defaultSilent/test_click/test_close/test_timeoutAndNarrow` 软件后端及显式D3D12各6项通过（含init/cleanup）；8秒计时、窄230px换行、点击和关闭成立。最初GPU夹具名叫d3d12但未强制后端，未据它宣称D3D12；最后 `QSG_RHI_BACKEND=d3d12 QSG_INFO=1` 真实日志确认D3D12，另存banner-d3d12-explicit.log。
- 产品EXE用独立数据目录、系统PATH、`--page set --size 1100x800 --exit-after 17000`，D3D12与`--obs-game-capture`真实软件UI各启动一次；各1条automatic请求/1条current结果，无QML错误，正常退出。没有播放性能/NR实机或全部页面验收声明。UiLanguage/UiContract（含384布局合同）及extract --check：四语缺失0、占位符问题0。
- 首次HTTP夹具因大小写敏感的User-Agent断言23行失败，改为按HTTP语义不区分大小写后原组通过；取消时read关闭reply警告已加isOpen检查。首次整软件计数脚本重复拼接console与engine日志，且未区分手动/自动的旧日志中有两条完成记录，不能用它证明启动一次；随后追加请求来源日志、仅查engine日志，最终两模式均一次自动请求。未追溯初轮额外记录来源，不冒称已修复自动重复请求。原失败日志在verification/与verification-094223保留。
- 收尾保护脚本首次错误要求software也打印`interface renderer=software`（产品软件分支实际打印OBS software UI），修正断言后直接复核既有实际日志通过，未改产品。70旧工作树/脏文件/main原样，原包39个根DLL SHA仍匹配manifest；引擎/采集/补帧/NR/SR/着色器无diff。没有运行库/SDK/模型入Git，没有改33ms/250ms刷新或加周期网络轮询。

最终回执：`E:/项目/Veyra/reports/update-check-20261013/{build,verification,protection}.json`；日志 `E:/项目/Veyra/logs/update-check-20261013/verification-094514`。本轮无main合并、push、完整测试包或正式发布；已发布2.0.7资产没有替换。GitHub不可达/限流时自动静默，用户手动检查才能看到失败提示。
