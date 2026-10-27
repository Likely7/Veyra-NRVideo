# 2.0.7 发布输入验收

此次按用户决定直接发布既有2.0.7-test-r2，不重新构建或更改包内容。只有外部ZIP名字改为2.0.7，内部显示/测试说明/manifest原状态保持；本页不声称包内显示已经变为另一版。软件的支持按钮打开在线README，因此六群与知识库可通过在线入口获取。

## 实际本轮检查

`python -X utf8 -B -I E:/项目/Veyra/tmp/release-2.0.7-20261013/audit.py`：exit0。

- NVIDIA1641/AMD2081项payload全部逐字节SHA/size匹配package manifest；ZIP全部项读取完成（CRC验证），无重复名/危险路径。
- 所有48/534条runtime manifest逐项匹配；原EXE SHA一致，PE2.0.7.0，显示2.0.7-test-r2。旧运行库签名/社区身份不改，以原批准manifest记录为准，不伪称重新签名。
- 原应用源码1970文件逐项SHA，FFmpeg/dav1d/zlib与未变依赖源码哈希/CRC核对；新上传应用/FFmpeg源无DLL/EXE/LIB/PDB/OBJ/模型。未变依赖源继续指向既有v2.0.6资产及相同SHA，保留原重链接材料。
- 发布改动没有触及src/include/apps/qml/shaders/CMake，产品与67ab696一致。待普通推送范围新增文件无专有runtime/SDK/模型；原tracked历史缓存不新增。
- 原最终ZIP四次真实冷启回执各自绑定同一个ZIP SHA及EXE：NVIDIA D3D12/software提交406/401真实帧，AMD D3D12/software407/401，exit0。software以实际`--obs-game-capture`入口及日志确认，不再使用此前无效的环境变量假定。
- 中英README/Release support/新报告相对文档链接存在；六群PNG原字节a237d642...，赞助图/Discord/Ko-fi保留，两二维码各width220。
- `git diff --check`通过；源码before bundle verify通过。完整保护最终核对另记reports/release-2.0.7-20261013/protection.json。

## 原验收证据，不冒充本轮重测

[刷新验收](../reports/TEST_2.0.7_REFRESH_2026-10-12.md)：新鲜完整构建548步骤/27目标，16组定向回归、100时间线断言、3内容真值回放、四最终ZIP启动。先前[整合验收](../reports/TEST_2.0.7_REPORT_2026-10-12.md)28组、378格式/15GUI/52几何状态及XeSS真实GUI。VFG/XeSS/T911数据均标明开发时条件，没有新增发布性能A/B或全硬件长稳。

扩展NR+DLSS热切换回放31/34，原2.0.7同场33/34，冷初始化后虚拟时间线追赶超时；并发不是已证唯一根因，失败保留。NV长期显存、BMD/Elgato/HDR现场及AMD/Intel仍有未解决/未验项。

## 上传固定输入

| 主Release文件 | 字节 | SHA256 |
| --- | ---: | --- |
| Veyra-2.0.7-NVIDIA-win64-portable.zip | 714741907 | 3105d62545a76b6e971c1e32f094bb312d5af22332e310c93c19f28550fd9036 |
| Veyra-2.0.7-AMD-win64-portable.zip | 355675904 | 24ea0ed5f8ce6a65d3769e946a870c2eda3383c30864ead612fc3af673b46a2d |

应用EXE `6fd35109d3c668f51fd7ce1609c8176e164227e2510473ef5a6276a34b1d713d`。对应源SHA及路径见[构建说明](BUILD_2.0.7.md)。本轮外部发布目录以硬链接保存同一不可变ZIP，未重复复制/解压整套工程；保护脚本再次验证原包未变。记录在E:/项目/Veyra/{archives,logs,tmp,reports,releases}/release-2.0.7-20261013。远端发布完成必须另有服务器digest/大小/正文/标签/latest与公开链接回执，本页不代替远端成功。
