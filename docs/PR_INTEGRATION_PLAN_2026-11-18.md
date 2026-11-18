# 截图八个PR整合计划

用户明确要求全部合并并做好兼容/适配：Likely7/Veyra-NRVideo #140/#136/#133/#127/#115/#99/#96/#75。当前授权包含完成审查、适配、构建验证后更新GitHub main；不包含新Release、运行包替换、关机或无关外发消息。

隔离分支codex/pr-integration-20261118，起点本地main c11e166（包含未推送的完整虚拟摄像机与项目整理）；当前远端bf596e1，开工标签checkpoint/pre-pr-integration-20261118。全部产物E:/项目/Veyra/{build,tests,reports,logs,tmp,downloads,deps}/codex-pr-integration-20261118。单Agent，编译并发2/≤900秒，每测试≤300秒。

## 固定输入

| PR | 固定head | 重点 |
|---|---|---|
| 140 | 9194693cdee681a8e74106a9b5cca60f81aaecb6 | Xbox损坏帧丢弃/IDR恢复，软件解码仅Xbox可选，缩放行数 |
| 136 | db5a4e53b29be50182988af14ea7d6674a43e798 | Dolby Vision P7/EL、B站页面/弹幕/字幕、浏览器桥；保留当前虚拟摄像机/剔除/回放/时序/默认值 |
| 133 | 01fef670fb9fdb14ddd1c41d916ad0f38eb793e5 | Chiaki反馈包回移、独立依赖stage与测试，不覆盖现依赖 |
| 127 | 9e2b53919bea45aa2c596e5aa6e957631e41753c | Elgato4KX UVC一次探测、HDR元数据/BT2020，其他设备和手动选择 |
| 115 | 4cab746301868841802261346b695d9b15a4baed | 韩语/字体/格式化/布局；补本轮新增字符串，原语言不退化 |
| 99 | e18103423c704e426ef42fbfb060c5930e57b36c | SDR视频截图不透明，静态透明图片与HDR JXR保留 |
| 96 | 719e2d89eaa3ff126d4130a4e3f7c2f48f8021d6 | 独立Linux OBS滤镜，Windows顶层构建不被改成Linux或依赖libobs |
| 75 | 4422a85d95a9d2f7f85a850e8c904ae61e77626a | 音量0–300%，Unity/静音/限制器、所有控件与配置，采集录音不依赖本机监听 |

## 执行

1. GitHub连接器读取元数据与完整patch；git fetch固定head，审查每个PR及组合冲突，不自动执行贡献分支脚本。报告存PR-info/patches.json与AUDIT.json。
2. #75/#96已有冲突，#96是draft；在本隔离树处理和验证，不覆盖main/脏旧树。较小修复先整合，大型#136后对齐所有接口、颜色与时间戳。
3. #136把已编译libdovi.dll/dovi.lib放在deps目录；沿用项目源码/二进制隔离，适配为E盘外部可重建依赖，不直接把DLL/LIB带入源码或新公开历史。libdovi来源/许可证/固定版本及构建记录须完整。若原PR历史无法直接合入，保留作者与来源的干净移植提交，准确说明原PR状态与已采用功能，不伪称原head已合并。
4. 浏览器/HTTP入口审查输入、Origin、URL/协议、cookie日志与持久化；默认/现有偏好保持，不自动安装扩展或改系统协议注册。
5. 用当前完整SDK配置构建Windows主程序与新测试；相关单元/UI、SDL/反馈、采集颜色、截图透明度、音频、DV合成和虚拟摄像机回归。Linux环境可用则真实构建/测试，环境缺项必须明确，不能称Windows通过等于Linux通过。
6. 汇总组合验证与实际未验硬件，审查diff/依赖/源文件，最后按用户授权更新main并核对远端与PR状态；原发布ZIP/用户配置不动。
