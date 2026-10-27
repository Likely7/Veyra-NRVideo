# PS5 HDR、画质审计、PSN 登录与主机保留执行方案

日期：2026-09-12
状态：方案待实施；本轮仅静态审计、上游核对和文档，不代表功能已接入。
基线：本地 HEAD 5190487，分支 codex/ps5-scheduler-telemetry-decode。实施前重新确认 git status，保存本地开工节点，不覆盖后续变更。

## 1. 用户目标与完成边界

- PS5 增加 H.265 HDR 接收；HDR 屏正确呈现 HDR，SDR 屏正确做色调映射。不能以只接收10-bit、最终截成8-bit SDR冒充原生HDR完成。
- 定位当前串流“糊、灰”是源端压缩还是本机解码、颜色、缩放、增强、呈现的问题，凭分段证据修复。
- 配对一次后保留主机；重启、软件升级/换目录、IP变化不应无故要求重新配对。
- 接入 PSN 浏览器授权，自动获得 Account ID、保存并刷新授权；保留不登录PSN的手动局域网配对。
- 本轮不发布，不新增未经授权的运行时，不承诺原生1440p/4K串流。2K/4K仍是本机增强输出。HDR不等于无损，也不保证比SDR锐利。
- 用户本次要求扩展旧SDR边界；实施时同步 AGENTS.md 与产品规格中相冲突条目，只扩展明确验证的路径，不全局删除HDR防错检查。

## 2. 已核对的事实与尚未确认的问题

| 位置 | 当前行为 | 结论 |
| --- | --- | --- |
| apps/veyra/ui/RemotePlayPanel.cpp | H264/H265 SDR，720/1080、30/60，5–100Mbps选项；默认H264/15Mbps | 码率是请求值，不是实测值 |
| src/remoteplay/Validation.cpp、ChiakiBackend.cpp | 仅两种codec；映射CHIAKI_CODEC_H264/H265；auto_regist=false | HDR与PSN自动注册尚未接入 |
| src/source/RemotePlaySource.cpp | 每帧resolveFrameColor后检查isHdrPath并拒绝；实际尺寸仍限制1080p | 不能只放开UI |
| src/pipeline/EnhanceGraph.cpp | HDR输入显式拒绝 | 必须建立HDR颜色契约及模块能力判断 |
| src/remoteplay/ProfileStore.cpp | DPAPI、原子写入、保存Account ID和注册密钥；版本1只接受两种codec | 已有持久化，不能声称此前未保存 |
| src/base/RuntimePaths.cpp | localDataDirectory取applicationRoot/runtime_local，root向EXE上级寻找 | 数据受软件所在目录影响，不是稳定用户目录 |
| RemotePlayPanel.cpp | 主机列表按host/IP选择；搜索只填第一个IP；load失败只返回空 | 易混淆主机、读取失败不可诊断，尚未证明就是用户本次重配根因 |

本机存在一份344字节加密主机存档，未读取/输出其明文凭据。需要区分用户所说8位关联设备码与连接后登录PIN，不能把二者混为一谈。

已有颜色修复：PS5 SDR显示意图已有BT.1886相关处理，帧元数据经resolveFrameColor解析；不能仅看到初始化Limited/BT709就断言硬编码导致发灰。旧测试通过不代表本次用户画面正常。继承 docs/REMOTEPLAY_COLOR_UI_CONTROLLER_REPAIR_PLAN_2026-09-12.md 与施工记录，重新对比真实链路。

## 3. P0：画质基线与分段审计

### 3.1 观测与对照

1. 固定同一游戏、静止暗部/灰阶/文字以及一段运动场景。记录PS5 HDR状态、请求codec/fps/bitrate、实际解码尺寸/格式/位深/range/matrix/transfer/primaries、Windows HDR状态、显示器与缩放尺寸。
2. 先关闭所有增强，以1:1及相同窗口尺寸对比Veyra与chiaki-ng；两程序顺序测试，不同时争抢串流。采集卡作为辅助对照，记录其HDMI与USB格式，不能假设两个出口处理一致。
3. 增加实际接收视频码率：按单调时钟窗口统计视频负载字节，明确不含/是否含FEC、重传和协议开销；请求码率与实测值分别显示，不以设置值充数。记录接收帧率、丢包/FEC、解码输出率和队列。
4. 诊断模式可保存短时压缩码流及少量中间纹理到ignored目录，对同一码流做确定性回放。正常播放禁止逐帧GPU回读。所有诊断默认关闭、有大小/时长上限，不写账号或令牌。

### 3.2 检查顺序

压缩输入 → AVFrame → YUV上传/硬解纹理 → range/matrix/transfer变换 → 缩放 → NR/SR/FG → 输出编码/交换链 → Windows合成。

逐项检查：Limited/Full只转换一次；UV顺序、色度位置、pitch/plane尺寸；软硬解一致性；BT709显示曲线与sRGB输出是否重复；NR parity/residual是否重复gamma或裁剪；多次缩放、半像素偏移；视频alpha必须不透明、不得透入毛玻璃背景；普通/直播模式和窗口/全屏色彩一致。

修复以“哪一步首次偏离参考”为依据，不默认加饱和度、压黑或锐化遮盖错误。依次测试单个增强及标准SR→NR→FG、低延迟NR→SR→FG。保存修复前后同帧证据，区分静态验证与真实PS5结果。

## 4. P1：主机持久化与连接流程

### 4.1 数据与迁移

- 新增独立RemotePlay用户数据目录：%LOCALAPPDATA%/Veyra/remoteplay。SDK/runtime路径保持原样，不全局替换localDataDirectory而牵连其他模块。
- 使用版本化档案，按稳定主机身份 + Account ID区分账户，IP仅为最后地址。核对上游可提供的主机ID及发现ID映射，不凭IP或名称合并凭据。
- 自动发现只更新匹配主机地址；身份不明确时让用户选择主机，禁止把第一台的IP写入另一台档案。
- 从当前旧目录迁移remoteplay-profile.dat及remoteplay/*.dat；DPAPI解密、校验、写新路径、回读成功后标记迁移。保留原件，失败不删除；不遍历整盘搜凭据。
- DPAPI仍绑定当前Windows用户/设备，不承诺复制到另一台电脑即可解密。跨机导入另立功能，不输出明文密钥。
- 明确区分无存档、解密失败、格式不支持、权限/写盘失败、主机拒绝凭据、网络不可达；网络错误不触发删除/重新配对。
- 保存最后选择主机、codec/码率/fps/解码方式/观看模式；不跨账户串用。更新采用原子写入，新增字段兼容旧档案。

### 4.2 UI

默认“我的主机”：名称、在线/待机/未知、连接、唤醒、设置；添加主机才显示首次配对流程。离线发现不到也保留已配对卡片及手填地址入口。

八位配对码只在首次配对或明确重新配对时使用，不保存临时码。主机登录PIN是独立选项：提供“记住登录PIN”，默认不选、DPAPI保存，仅收到真实PIN请求时提交一次，拒绝后停止自动重试并让用户输入，防止无限重试。删除配对与退出PSN分开，不连带删除本地仍有效的连接凭据。

## 5. P2：PSN 登录

- 复用固定chiaki-ng的授权流程与必要协议逻辑，保持AGPL及OpenSSL exception归因。Win32/WinHTTP适配，不为登录引入第二套Qt播放器。
- 通过系统浏览器访问Sony授权页，密码和二次验证由用户在Sony页面完成，Veyra不收集密码。
- 第一版支持粘贴授权后的回调URL；验证scheme/host/path、参数、长度、一次性授权会话，拒绝任意URL请求。研究自动捕获回调时不能随意更换Sony要求的redirect_uri；只有协议支持并实际验证后才做自动返回。不能声称仅加按钮就可无缝自动登录。
- 授权码换token、自动查询Account ID、加密保存access/refresh token与到期时间；刷新采用单任务与有界退避，处理过期、撤销、取消、网络错误和刷新令牌轮换。日志过滤URL中的code/token以及所有授权header。
- PSN登录失败不阻塞已有局域网配对连接；不要求租赁账号用户必须重新登录才能使用已有凭据。
- 有条件的免PIN注册：核对固定上游PSN会话/设备查询/自动注册实现及依赖，连接主机身份与账户，成功后落入同一档案。能力不可用时明确转到一次手动配对，不假报自动注册成功。
- 本轮交付PSN登录、Account ID和可用的自动注册；公网穿透完整串流不是本轮承诺，若自动注册需其部分协议可最小引入，但不对外宣称公网已验收。

## 6. P3：HDR接收、颜色处理与显示

### 6.1 输入

- 增加H265_HDR枚举，贯通Types/Validation/ProfileStore/面板/后端、patched视频回调与VideoIngress，版本迁移保留旧H264/H265值。
- 实际帧元数据决定处理方式，不因用户选择HDR就把所有帧标成PQ。请求HDR但收到SDR要报告实际状态；不匹配必须诊断，不能强套颜色转换。
- HEVC Main10：硬解处理实际P010及平面视图/资源状态；软解支持实际10-bit planar格式与stride。不能把10-bit数据按8-bit NV12读，也不能混淆P010高位对齐与planar低位有效值。
- 保存BT2020/PQ、range、位深、mastering/CLL元数据的已知/未知状态；缺失时仅采用有依据且标注的fallback，不捏造峰值。

### 6.2 共享颜色契约

建立显式HDR工作空间（线性浮点、明确色域和亮度单位），处理range→YUV矩阵→PQ EOTF→工作色域，不在中途无条件saturate到0..1。审计Nv12Upload.hlsl及共享上传代码；增加P010/10-bit路径和独立颜色数学测试。

SDR屏：HDR → 色调映射及色域映射 → 现有SDR增强工作空间 → 输出。UI明确“HDR输入 / SDR映射输出”。采用固定稳定的基础映射，避免自动曝光呼吸；白点/峰值设置有默认值和说明。

HDR屏：目标实现原生HDR旁路显示，优先评估RGBA16F scRGB交换链及DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709；若采用HDR10交换链则正确配置PQ/BT2020、10-bit编码及所需metadata。依据现有Presenter实测选择，不同时维护两套未经验证的路径。检测Windows Advanced Color与显示器切换、HDR开关、全屏/resize，必要时重建并reset；SDR UI按参考白合成，视频alpha不透明。

### 6.3 增强兼容性，不做假HDR

逐项审计NR Feature18/parity、DLSS SR、RTX Video SR、DLSS FG、XeSS对浮点格式、HDR标志、亮度尺度和输入范围的要求。每项必须有API证据及真实Create/Evaluate/画质结果。

- 能保留HDR的组合：接入同一个EnhanceGraph及生命周期，验证高光未裁切、原帧/生成帧亮度一致。
- 仅支持SDR的组合：提供明确“HDR转SDR后增强”路径；若输出到HDR屏也必须标注SDR映射，不能叫原生HDR增强。
- 不能未经验证将NR输入直接塞PQ，或把NR的SDR输出拉亮冒充恢复HDR。HDR原生显示与全增强兼容分别记录，不能用其中一项冒充全部完工。
- 若原生HDR + 当前实验NR无法建立可靠契约，记录具体阻塞与可交付兼容路径，交由用户选择后续；不得默默删掉HDR原生显示目标。

显示与录制分开：WGC/直播兼容也需验证HDR捕获是否正确；SDR录播需明确映射，不能保证所有捕获软件自动正确处理。图片/视频HDR导出不在本次要求中，保持原有SDR导出契约及明确拒绝/转换提示，不放开未验证的导出路径。

## 7. 分阶段验收与Git节点

| 节点 | 验收 | 存档 |
| --- | --- | --- |
| P0 画质定位 | 同码流软硬解、黑白灰阶/色块/运动；定位首次误差；请求与实际码率分开 | 证据与最小修复 |
| P1 主机保留 | 首次配对→退出重开→换EXE目录→IP变化→唤醒连接；多账户、多主机、损坏/旧档案、写失败；不重复索要8位码 | 主机存储与UI |
| P2 PSN | 实际Sony授权/Account ID、重启、刷新/撤销/离线、取消；自动注册能力与手动备用分开；日志无密钥 | PSN功能 |
| P3 HDR基础 | H264 SDR/H265 SDR/H265 HDR，30/60，软硬解；PQ灰阶、黑位/高光、BT2020色块；HDR屏与SDR映射 | HDR基础 |
| P4 HDR增强/恢复 | 各NR/SR/FG组合及两种顺序能力矩阵；resize/全屏/HDR开关/重连；音频/手柄不中断、队列有界 | 综合交付 |

每阶段更新WORKLOG和本计划的实测表，记录命令、日志、失败修复和未测项。每次测试<=300秒，不是整个任务限5分钟。编译、针对性测试后执行必要delivery；不为纯文档变更运行GPU测试。PS5交互登录、游戏画质与真实HDR显示由用户配合验收，不能以合成图或返回码0代替。公共颜色层修改需回归文件/采集/图片和SDR导出，不能只验PS5。

所有本地checkpoint仅提交源码和文档；SDK/DLL/模型、配对档案、令牌、日志和私有媒体保持ignored。无本次发布授权，不push、不上传Release。

## 8. 实施入口与风险

主要修改：RemotePlayPanel.cpp、ProfileStore.cpp、Discovery.cpp、Types.h、Validation.cpp、ChiakiBackend.cpp、RemotePlaySource.cpp、VideoIngress.cpp、共享颜色解析/上传shader、EnhanceGraph.cpp、VideoPresenter.cpp及XeSS呈现路径；新增独立PSN服务和HDR颜色/存储迁移测试。

最大不确定项是实验NR对真正HDR亮度/色域的兼容，其次是Sony授权服务和自动注册条件。提升码率或换HDR不能恢复已被编码丢失的细节；先验证无增强画面正确，才评价增强收益。

下一步唯一入口：实施前本地Git存档，然后执行P0画质证据与P1主机存储修复；本文件不代表已开工产品修改。

## 9. 一手参考

- https://streetpea.github.io/chiaki-ng/setup/configuration/ ：PSN授权/刷新、条件性免PIN注册、独立Console PIN、H265 HDR及设备要求。
- https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range ：Windows Advanced Color、浮点scRGB、色彩空间及输出管理。
- 本地chiaki固定提交0e16950165f06e5c3291537c2eeba6e852be7120：lib/include/chiaki/common.h、session.h，gui/src/psnaccountid.cpp、psntoken.cpp及自动注册调用。在线文档可能比固定源码更新，移植前逐项确认版本能力，不隐式升级依赖。

本轮核对：git status干净基线；源码rg/Get-Content检查及上述网页读取。未运行PS5连接、HDR、OAuth或构建测试；未修改产品代码。

## 2026-09-12 实施状态

方案已进入产品代码，详见 [施工记录](../reports/PS5_HDR_PSN_EXECUTION_2026-09-12.md)。原文“未实施”仅代表规划时点；完成、未验证和未落地以施工记录的分项为准，不能把所有计划项视为已完成。
