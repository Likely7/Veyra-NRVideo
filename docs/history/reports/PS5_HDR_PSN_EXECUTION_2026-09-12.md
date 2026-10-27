# PS5 HDR / PSN / 主机保留施工记录

日期：2026-09-12。对应方案：PS5_HDR_PSN_HOST_PLAN_2026-09-12.md。
开工源码基线5190487，方案提交37cfdf4，标签checkpoint/ps5-hdr-psn-preimplementation-2026-09-12。

## 已实现

### 主机存储

- 独立固定目录 `%LOCALAPPDATA%/Veyra/remoteplay`；SDK、运行时仍使用原路径。
- 从当前安装根runtime_local迁移旧单档及32位十六进制命名档案，DPAPI解密验证→原子保存→回读；保留原件，用迁移标记避免删除后复活。不能解密则保留并提示，禁止凭网络失败删除档案。
- 档案v2兼容读取v1，新增主机ID和viewOnly。配对结果MAC作为稳定身份；发现按ID更新IP，旧档案只在原IP匹配时补身份；未知多机不取第一台替换已保存主机。
- 同IP不同Account ID不覆盖同一档案。记录最后选中的档案；解码偏好转移至固定目录settings.ini。旧档案未保存的主机ID无法凭空恢复，旧IP已变时仍需一次人工核对。
- 区分Missing/Read/Decrypt/Format加载错误；日志只输出错误类别。连接按钮不要求八位配对码。
- 当前面板保留原配对表单及明确“直接连接”提示，尚未重做主机卡片式布局。登录PIN的记住功能尚未实现；不与8位关联设备码混淆。

### PSN

新增PsnAuth.h/.cpp、WinHTTP和现有json-c依赖、面板登录/提交回调/退出按钮。

- 系统浏览器进入Sony页面；用户复制最终回调URL，点击“从剪贴板提交登录结果”。限定https回调host/path、单一code、长度/字符和十分钟操作窗口，不接受任意URL请求；不抓密码，不自动读取用户浏览器。
- 授权码换token、Account ID查询、DPAPI加密保存、到期刷新；面板打开时和每五分钟检查，离线/撤销失败不影响局域网配对。
- token文件原子替换，命名互斥量防并发授权写入；刷新复用已绑定Account ID，避免额外查询失败丢失旋转后的refresh token。日志只显示状态码，不记录URL、header、code/token。
- 退出PSN不删除主机配对。上游归因已加入THIRD_PARTY_NOTICES.md。
- **尚未真实登录Sony验证。首次主机配对仍用八位码；PSN自动免PIN注册尚未移植。** 固定上游自动注册依赖DUID/PSN会话与holepunch，本项目当前关闭RUDP。不能把账号ID自动填入宣传为免PIN注册或公网连接。

### HDR

- H265Hdr贯穿设置、持久化、AnnexB、Chiaki codec和RemotePlaySource；真实帧元数据覆盖假定值，8-bit降级不硬套PQ。
- Main10软解（P010/YUV420P10LE）及D3D12VA P010 GPU纹理；16-bit plane/正确pitch。共享颜色元数据增加PQ/HLG识别及primaries解析；HLG和不支持的PQ色度明确拒绝。
- YuvToLinearRgb shader正确处理P010高10位与64..940/512/896码值，BT2020 NCL矩阵、ST2084绝对亮度及BT2020→BT709转换。
- 原生HDR：关闭NR/SR/FG且目标显示器Windows HDR启用时，FP16 scRGB（1=80nits）输出。补齐GraphicsPass可配置RTV格式、PresentSink颜色空间、浮点videoFrame及ScaleBlit去除unorm截断。参考画面不二次sRGB编码。每2秒检查输出HDR状态，通过原设置事务重建/reset。
- 增强或SDR屏：先做HDR→SDR映射，使用固定1000nit参考峰值/203nit归一化的稳定亮度肩部及色域夹取；再走已有NR/SR/FG。该映射尚未用用户游戏校准，不宣称最佳算法，也不宣称原生HDR增强。状态与tooltip明确区分。
- 原生HDR截图明确提示尚不支持，不生成伪8-bit HDR图片。公共文件/采集/导出仍保留HDR边界；不借本次扩大产品导出承诺。

### 链路发现

原software sws_scale目标planes/strides只分配2项。新10-bit Full范围测试触发访问异常；扩为完整4项后同组测试全部通过。这是本轮确实修复的缺陷，**不是用户“糊、灰”的已证实根因**。

旧shader注释称P010归一化与8-bit相同，但实现只用/255；已改为明确位深码值。本轮没有用调饱和度/压黑来掩盖颜色错误。现有SessionInbox已经统计实际视频负载Mbps，面板已显示；继续复用，不新增第二套计数。

## 实际验证

每次命令均在300秒内。合成凭据测试不使用真实PSN账号。

| 命令/证据 | 结果与边界 |
| --- | --- |
| cmd /c out/remoteplay/build-extra-delay.cmd | 多次对应构建；最终见logs/ps5-hdr-final5-build.log；构建目标veyra/contract/live/preset/quality/source |
| cmd /c out/remoteplay/build-host-tests.cmd；veyra_remoteplay_profile_tests.exe | DPAPI存取、主机ID/观看模式、迁移/幂等/保留原件/删除不复活、损坏拒绝、callback白名单/重复code/非法字符拒绝。logs/ps5-host-final.log；不等于OAuth成功 |
| veyra_hdr_color_tests.exe | 8组：P010/planar×Limited/Full×SDR/native。黑、100nit、1000nit ST2084参考；SDR归一化误差0.000357312，native最差0.046875 scRGB单位（含10-bit量化）。真实GPU处理、FP16/SDR交换链创建和Present通过；未测物理屏幕亮度 |
| veyra_hdr_color_tests.exe logs/ps5-main10-fixture.mp4 | 真实HEVC Main10测试片（lavfi生成，不是PS5录制），软件/硬解各12帧，硬解实际AV_PIX_FMT_D3D12确认，各NR Evaluate 12次、非黑输出，logs/ps5-hdr-final-tests.log。验证的是HDR转SDR+NR |
| veyra_yuy2_color_tests.exe --remote-yuv | 四组SDR全/有限、601/709回归，sourceError<=1 code value、displayError=0；logs/ps5-sdr-color-regression.log |
| veyra_repair_contract_tests.exe | 90 checks 0 failures；logs/ps5-hdr-contract.log |
| scripts/remoteplay/test-ui.ps1 -PlayerExe out/remoteplay/product-repair/veyra.exe -OutputDirectory logs/ps5-hdr-final-ui | PS5面板开关、非法配对、三个codec与三个decoder选项、PSN按钮、专业/日常及全屏切换通过；logs/ps5-hdr-final-ui.log。不点击真实授权、不连接PS5 |
| scripts/gates/delivery.ps1 -Root . -BuildDirectory out/remoteplay/product-repair -PlayerExe out/remoteplay/product-repair/veyra.exe | 42.73秒软件gate通过，logs/delivery/2e696125b6d741af9bdbfaa71b8afe78/result.json；此gate早于最终RTV格式与错误提示收尾。后续相关HDR/Main10/Present与UI针对性测试单独记录，不能冒称gate哈希就是最终EXE |

失败记录：PSN首次CMake imported target作用域不可见，改为父作用域独立pkg_check_modules，logs/ps5-psn-build.log→build2/build3。HDR测试首次缺ColorMetadata include导致编译失败，补后编译通过。第一次HDR运行在第二组Full范围访问异常（logs/ps5-hdr-color.log，exit -1073741819），扩目标平面数组后logs/ps5-hdr-color2.log通过；后续增加真实Present和Main10解码测试继续通过。没有删除失败日志来伪造首次成功。

## 尚未验证/尚未落地，禁止省略

1. 用户实际Sony登录、token刷新/撤销及真实Account ID；当前只验证代码构建和回调输入边界。
2. 用户PS5 H265 HDR协商、游戏场景黑位/高光/色彩与软硬解对照；尚未证明“50Mbps糊灰”已消失。
3. HDR物理显示器、Windows HDR切换/跨屏、HDR经WGC/OBS捕获；当前只验证交换链及GPU数值。
4. HDR转SDR后的真实游戏画质矩阵仍待验收。合成Main10已验证RTX Video SR + NR + DLSS 2X标准/低延迟顺序；DLSS SR、XeSS及其他倍率未做本轮HDR专项。实验NR的原生HDR处理未接，明确使用SDR映射兼容路径。
5. PSN免PIN自动注册、公网连接、记住主机登录PIN、主机卡片UI尚未实现。八位配对码保存问题通过本地主机档案解决，首次仍需要一次。
6. 旧目录之外的配对文件不全盘搜索，跨Windows用户/电脑不能解密的档案不声称可迁移。

## 用户验收步骤

1. 使用桌面“Veyra PS5 测试版”。打开PS5面板直接“连接并观看”，不重新点配对；退出重开再连，检查是否无需8位码。
2. 如能登录账号：点登录PSN，在Sony页自行完成，复制最终回调地址，点提交登录结果；看到Account ID与成功提示。不要把完整回调或token发到聊天/日志。
3. PS5启用HDR后选“H.265 · HDR（实验）”，重连。先全关增强：HDR屏且Windows HDR开应显示原生HDR路径；普通屏应明确SDR映射。再开NR，确认状态切换为SDR映射后增强。
4. 对照同一静止/运动场景及原H265 SDR，反馈发灰、细节和高光。保留请求/实际码率、codec和输出状态；不要只凭设置50Mbps判断真实带宽。

下一步：完成以上真实验收及HDR组合矩阵；依赖实际PSN会话信息的自动注册独立实施。未发布、未push、未提交运行时或凭据。


### 收尾追加证据

`logs/ps5-hdr-combo.log`：同一Main10测试片，共6组（软件/硬解 × NR单独/RTX Video SR→NR→DLSS2X/NR→RTX Video SR→DLSS2X）。各12次NR Evaluate；组合各12次SR Evaluate、11张有效生成帧；所有返回0。4K输出，顺序2的NR源尺寸1080p。此为真实GPU执行与生成状态，不是PS5真实画质测评。最后测试文件为tests/integration/HdrColorTests.cpp，构建logs/ps5-hdr-combo-build.log。

最终产品构建logs/ps5-hdr-final5-build.log。后续只修改测试/文档，不把临时夹具或运行时纳入Git。桌面快捷方式继续指向out/remoteplay/product-repair/veyra.exe，无需重装。
