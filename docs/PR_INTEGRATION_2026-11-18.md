# 八项 PR 的兼容整合与验证

用户要求整合 #140、#136、#133、#127、#115、#99、#96、#75，并更新 GitHub main。本轮仍为 2.1.9 主线开发版本，不替换既有 Release 下载包。固定输入与授权见 [计划](PR_INTEGRATION_PLAN_2026-11-18.md)。

## 最终功能

| PR | 合入内容与适配 |
|---|---|
| #140 | Xbox 损坏/隐藏损坏帧拒收、等待关键帧恢复；可选软件解码；修正输出尺寸变化时的缩放行数。普通视频和其他来源不自动切软件解码。 |
| #136 | B 站浏览、搜索、收藏/历史、分P、画质/编码、登录、评论/互动、字幕/弹幕；浏览器扩展、HTTP 和 veyra 协议桥；Dolby Vision P5/P7 RPU、EL 配对/合成和动态亮度跟随；默认关闭的自动补帧倍率。 |
| #133 | 四项 Chiaki 上游反馈历史修复、组合按键/触摸历史编码；在独立 stage 编译，不覆盖已有依赖。 |
| #127 | Elgato 4K X 的 UVC HDR 探测及颜色合同；保留其他设备、手动格式/色彩选择与 MK2 例外。 |
| #115 | 韩语、字体、格式化状态和长文案布局；补齐本轮新增内容和虚拟摄像机文案。 |
| #99 | SDR 视频截图强制不透明并保留 RGB；透明静态图片与 HDR JXR 不被统一抹掉透明度。 |
| #96 | 独立 Linux OBS 滤镜：3D LUT、曝光/对比/饱和、FXAA、锐化。不是 Windows 全播放器移植，Windows 不引入 libobs 依赖。 |
| #75 | 0–300% PCM 音量、平滑增益与峰值限制；当前分离式音频面板、快捷键、节点控件、影院条及配置保存全部对齐。 |

七项 PR 保留原 head 为合并祖先。#136 原历史携带 DLL/LIB，按项目源码/二进制隔离规则做**保留作者和固定来源的纯源码导入**，不把它的原 head 或已编译依赖合进公开历史；原 PR 的 GitHub 状态须与这一事实区分。来源及许可证见 [第三方说明](../THIRD_PARTY_NOTICES.md)。

## 整合时额外修正

- 新自动倍率字段只在预设 v22 写入/读取，修复旧 v16–21 预设兼容；节点编辑器会话 v17 的嵌套载荷采用相同上限。保留旧文件和扩展侧文件策略。
- 当前 main 的 `volume` 设置优先，兼容贡献版本的 `audioVolume`；只恢复用户明确保存的增益，新配置仍为 100%。录制/回放 PCM 不依赖本机监听开关。
- 去除旧内联显示面板的重复嵌套；新 HDR/Dolby 控件放回共享显示面板，修复 HDR 观察状态，并保留折叠。保留 GPU 默认高、VFG 自动调控默认关。
- 拖动进度条的重复 seek 抑制只作用于同一次拖动，避免后一次拖到相同位置不响应。
- B 站页面解析移出 GUI 线程；片源切换和停止使过期解析失效。防止用新 future 覆盖在途任务而阻塞 GUI；列表等待槽保留最新请求。
- HTTP 只监听回环地址，校验 Host/扩展 Origin、长度和 JSON/URL/cookie，限制连接、请求体和时间；不输出 Cookie 或带签名 URL。登录信息沿用 DPAPI。协议安装脚本验证实际 EXE 路径，未替用户注册或安装扩展。
- RPU 解析不再逐帧写开发者 D 盘日志。FFmpeg 负责 metadata 分配/对齐；修复 MMR 常量遗漏整数部分，使用匹配源码构建的 libdovi。
- Linux LUT 拒绝空文件、重复尺寸、非有限数、非法域、缺/多数据；坏 LUT 选择清除旧效果；LUT 使用 texel 中心；保留预乘透明度；默认效果关闭。
- B 站侧栏按译文宽度适配，韩语长标签、登录按钮不再溢出。五种界面语言缺失与占位符问题均为 0。

## 本轮证据

目录统一为 `E:/项目/Veyra/{build,tests,logs,reports,tmp,deps}/codex-pr-integration-20261118`。构建并发 2，每个测试进程不超过 300 秒。旧失败记录保留，最终通过结果按下表指定回执，不把初次失败算通过。

| 检查 | 结果 / 回执 |
|---|---|
| Windows 完整产品与相关测试目标 | 构建成功；build/app，logs 下 build-* 回执。保留 patched FFmpeg。 |
| 预设/旧格式/新自动倍率/节点编辑器 | 634 PASS、0 FAIL；PRESET-TEST.json / preset-final.log。 |
| QtQuick 真实控件、拖动与布局 | 220 通过、0 失败、0 跳过；quick-final.txt。 |
| 五语言 | UiLanguageTests 0 失败；extract.py --check 全语言缺失 0、占位符问题 0。 |
| PCM 增益与真实 bridge 保存/重启 | AudioGainTests 通过；volume-final.log 16 检查、0 失败。初次 stage 缺 Qt offscreen/test 组件导致停滞，补齐后通过；不是静默略过。 |
| 音频 DSP / replay | AudioWaveform 51/0，ReplayBuffer 37/0；AUDIO-TRANSPORT.json / TESTS.json。 |
| 修复合同及 Dolby 数学 | RepairContract 370/0；实时调度/效果链通过。 |
| 实际 libdovi / FFmpeg ABI | 六组上游 P5/P8/FEL/MEL/整数系数 RPU，256/0；DOVI-TEST.json。 |
| HTTP | 20/0，含分段请求、跨域/Host、重复长度、超长、非 HTTP、Cookie 注入等；PlaybackServerTests。 |
| 截图 / 采集颜色 / 压缩解码 | 全通过；截图 GPU debug 无错误，真实 H.264 软/硬解、尺寸变化、恢复/PTS 与旧帧生命周期通过。 |
| PS5 反馈 | patched stage 精确校验；两个原生 sender/upstream 回归程序通过；实际 Takion 加密和回环 UDP 的 8 个历史包通过。未连接主机。 |
| 摄像机 | GPU 523/0、debugErrors=0；32/64 位传输各 73 项；实际 4X→60/120 FPS 的短测均 0 重复、0 倒序；APP-CADENCE.json。 |
| 产品联动 | 摄像机+录制+回放同时启用；监听静音且增益 300% 时，两份录像分别 267/296 帧，AAC RMS 0.08822/0.08821，均非静音；PARALLEL-AUDIO.json。 |
| 浏览器/界面 | HTTP 交接后停止/换片及 GUI 心跳正常；实际 B 站推荐列表与韩语显示设置截图人工查看，侧栏/折叠正常。使用私有配置及假测试 Cookie。 |
| Linux | [最终提交 Ubuntu 24.04 CI](https://github.com/Likely7/Veyra-NRVideo/actions/runs/44288445800) 编译 libobs 模块、14 个 LUT 输入检查和安装布局通过。 |
| 打包入口 | 新开源 DLL 与 MIT LICENSE 跟随已构建前缀打包；PACKAGING-TEST.json 验证复制及缺失构建运行时拒绝。没有制作/上传新 Release。 |

初轮 Quick 失败定位到共享控件布局、重复显示面板和跨拖动 seek 状态，已经修复；offscreen 与真实 Windows 控件的日志均保留。新截图中的黑色视频区域是 Qt grab 不包含原生 D3D 子窗口，不是摄像机或录制黑屏证明；后者用实际输出文件/消费者验证。

## 边界

B站会员、扫码登录和互动写操作未使用真实账号测试；未代用户发送评论/点赞/投币。本轮没有 Elgato 4K X、真实 Xbox/PS5 或其他型号显卡的现场验收；不把回环反馈和 H.264 夹具说成主机端通过。P7 EL 合成保持实验性，缺失/失败的 EL 按既有合同回退基础层；本轮 RPU/数学验证不等于完整双层片库画质验收。Linux 新验证是编译、解析器和安装布局，贡献者的历史 RTX3090 图像测量另行注明来源。虚拟摄像机仍只含 SDR 视频，声音需直播软件单独采集；未改系统摄像机注册、驱动或用户配置。

发布 ZIP、历史标签、BGM 分发规则、暂停研究分支和脏工作树保持原先保全规则。合并远端的实际提交、原 PR 状态以 WORKLOG 和外部 FINAL.json 的远端核验为准。

## 远端合并结果

整合 [PR #146](https://github.com/Likely7/Veyra-NRVideo/pull/146) 已于 2026-11-18 合入 main，合并提交 `bd35431a6fe42ff4757fba4dec887e72a066e545`。最终提交 5d9a2aee 的 [Windows CI](https://github.com/Likely7/Veyra-NRVideo/actions/runs/44288445771) 与两次 Linux CI 均成功。#140/#133/#127/#115/#99/#96/#75 已核对为 merged；#136 已关闭为 adopted，保留原正文并说明纯源码导入与 #146 的对应关系，merged 标志为 false，不冒充原 head 合入。

本地唯一 main 工作树已 fast-forward 到上述合并提交。QA 的 32/64 位临时 HKCU 摄像机注册已移除（仅删除指向本任务 QA 目录的注册），系统级组件与用户配置未改；私有探针已撤下，单份可复用 stage 留在 tests 目录。后续 main 构建使用独立输出目录，构建脚本拒绝复用其他 source-root 的 CMake cache。

main 合并后重复 CI 曾两次在依赖下载阶段超时，实际 runner 的 mirror+file 间接源已适配；[最终 main CI](https://github.com/Likely7/Veyra-NRVideo/actions/runs/44351556868) 全步骤成功。期间测试清单误写已从原发布 ZIP 原子恢复，正式 NVIDIA 目录 1695 个文件逐 SHA 一致，ZIP 未变；测试写入函数已修复并完成硬链接夹具回归。完整失败与恢复回执均保留在本轮报告目录。
