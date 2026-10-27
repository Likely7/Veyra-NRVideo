# 2026-09-12 PS5 主机发现修复

用户要求先修自动发现，不要求登录PSN，不改代理、防火墙或账号设置。

旧实现只用255.255.255.255、等待3秒且关闭Chiaki日志。当前通过GetAdaptersAddresses枚举活动IPv4接口，按前缀计算并去重定向广播地址，过滤loopback/tunnel/link-local与无广播的前缀；向Chiaki提供broadcast_addrs。全局广播仍保留，搜索6秒并可取消。新增DiscoveryReport，初始化/枚举/发送错误与零回应区分；日志有上限且不记录账号、配对密钥。发现不依赖Account ID；配对要求不变。

修改：Discovery.h/.cpp、VeyraRemotePlay.cmake（系统iphlpapi库）、RemotePlayPanel.cpp、ProductBoundaryTests.cpp。测试通过--discover显式调用产品发现实现；默认边界回归不自动探测网络。

验证：
- cmd /c out/remoteplay/audit-20260911/build-product.cmd：首次产品链接因运行中veyra.exe被占用报LNK1104（logs/remoteplay-discovery-build.log）；正常关闭旧窗口后重建成功（logs/remoteplay-discovery-build-retry.log）。
- scripts/run-short-test.ps1，30秒上限，boundary executable --discover：6秒搜索，实际枚举198.18.0.3及192.168.6.255；收到真实PS5回应192.168.6.232，standby=0，hosts=1 error=0。logs/remoteplay-discovery-live.stdout.log。仅发现，不连接/配对/控制主机。
- 同一boundary executable默认回归exit0，覆盖取消后不搜索、mailbox、invalid reopen、SDL；logs/remoteplay-discovery-boundary.stdout.log。
- scripts/remoteplay/test-ui.ps1 -PlayerExe ./out/remoteplay/product-repair/veyra.exe -OutputDirectory ./logs/remoteplay-discovery-ui：PASS，面板开关/无效配对本地拒绝，未实际PS5配对。

当前结果证明修复版在这台多网卡电脑上可发现PS5，不证明任意防火墙/路由环境，也不把发现成功当作串流已通过。没有修改音频/GPU链路或运行时，未重复完整GPU回归。下一步用户获取Account ID后配对。仅本地Git，无push或发布。
