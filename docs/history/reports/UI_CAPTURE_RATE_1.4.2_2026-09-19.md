# 1.4.2 UI 与采集设备帧率验收

## 范围

分支 `codex/screen-capture-20260919`，工作树基线 `12238df`。保留此前屏幕采集、P010 HDR、XeSS 拖动回退及全部隔离修复，构建显示版本 1.4.2。只做本地测试包，不合并 main、不推送、不发布。

- AppShell.cpp：最近打开与 PS5 原本同在 y=316，移动最近打开到 372，恢复 PS5 点击入口。
- Theme.h / PopupSelector.h / SettingsWindow.cpp：原生热跟踪和自绘竞争改为统一缓冲绘制，重复选中值不再重绘；滚动容器组合绘制并刷新子控件，弹出列表同一行反复悬停不重复 invalidate。
- CapturePanel.cpp / CapturePreferenceStore.h：设备帧率数字输入、0 默认、小数、重连应用及 v3 持久化；v1/v2 配置默认帧率为0。
- CaptureCardSource.h/.cpp / CaptureFrameRate.h / EngineController.cpp：URI 携带帧率、DirectShow VIDEOINFOHEADER/2 的 AvgTimePerFrame 协商、连接后检查实际媒体类型、具体错误反馈与重连保留参数。
- UiContractTests.cpp / PopupSelectorTests.cpp / CaptureSourceTests.cpp：非法值、旧配置迁移、悬停重绘和设备实际回调帧率回归。
- package-portable.ps1：补齐窗口采集上游 MIT 许可。

## 帧率边界

输入范围为 1–1000 FPS，0 沿用所选格式。范围不是设备能力承诺。接受约0.11%差异以兼容30/29.97等标称档位。驱动拒绝或返回其他媒体帧率时明确报错，未增加软件丢帧限速。

设备媒体类型仍是驱动报告值，不能保证每个驱动诚实执行；日志另有实际回调速率可核对。本机虚拟设备实收验证不是物理采集卡支持证明。精确60→30且游戏每帧重复两次时，均匀采样通常能得到连续独立帧；50→30、可变帧率或游戏节奏不稳时无法保证112233变123。没有新增画面去重或伪造时间戳。

## 实际验证

产物：`E:/项目/Veyra/{build/screen-capture-20260919,logs/ui-1.4.2-20260919,tests/ui-1.4.2-20260919,tmp/ui-1.4.2-20260919}`。

构建：`scripts/build-isolated.ps1 -Root . -BuildDirectory E:/项目/Veyra/build/screen-capture-20260919 -DependencyCache E:/项目/Veyra/build/frame-pacing-20260918/CMakeCache.txt -TempDirectory E:/项目/Veyra/tmp/ui-1.4.2-20260919 -DisplayVersion 1.4.2`，目标 veyra、veyra_ui_contract_tests、veyra_capture_tests、veyra_popup_selector_tests。build1/2/3 均 exit0。

测试均通过 scripts/run-short-test.ps1 执行，单次限时60秒，屏幕引擎120秒：

| 测试 | 实际结果 |
| --- | --- |
| UI contracts（prefs2） | exit0，含旧v1/v2配置、帧率保存和非法值检查 |
| PopupSelector --test | exit0，case0–14；重复悬停不重绘，点击提交正确 |
| Capture --list | 只有 WebcastMate/OBS 虚拟摄像头；VC-007PRO 未连接 |
| Capture --rate-test capture:1:0:-1?fps=30 30 | exit0；重连前/后实收29.981/29.989 FPS，PTS单调 |
| Capture --rate-test capture:1:0:-1?fps=40 40 | exit0；39.796/40.156 FPS，PTS单调 |
| Capture --rate-test capture:1:0:-1 0 | exit0；默认59.956/60.202 FPS，PTS单调 |
| ScreenCapture --engine | exit0，failures=0；含WGC/DXGI真实RGB、自动100Hz、生命周期、NR+DLSS4X与停止释放 |

最后一项实际 NR CreateFeature18=0x1、seh=0，584 源帧进入处理、1752 FG 执行、1455 生成帧呈现、294 生成帧过期；不宣称零丢帧或屏幕端满4X。

Computer Use 目视检查：PS5 与最近打开分离，PS5 面板可打开；设置连续滚动、SR下拉打开/关闭没有发现残留重影，帧率输入及全部采集设置显示完整。静态截图不能证明所有机器动态无闪烁，交付后仍需用户观察。

测试失误：首次 rate30 使用旧 capture URI，实际29.78FPS通过但重连失败，因为生产重连只支持稳定设备身份。修正测试装配使用产品 makeCapturePath 后重测通过。首次 GUI 启动传入不存在的 --log 参数，被当成媒体路径；随后用正确 --smoke-empty --smoke-seconds 240 --smoke-view pro 重开通过。

## 交付与未执行项

目标包：`E:/项目/Veyra/test-packages/1.4.2/Veyra-1.4.2-test-win64-portable.zip` 与对应 `Veyra-1.4.2-test-source.zip`。运行库沿用已批准身份，保留 patched FFmpeg、RemotePlay 对应源码、许可证和双二维码。

便携包已生成，467110278字节，SHA256 `23E4427065B36AE94D9FA118A0441DA12F5017D3EAB8047E8DB87A183826BD7E`。压缩包114文件按manifest回读核验通过；支持区两图width=220。`scripts/acceptance/portable-smoke.ps1` 七组均通过，包含默认效果全关、基础播放、原版/社区SR+NR+FG、视频SR及Ampere NR；仅证明这些运行库在本机5070执行，不代表30/40硬件验收。实际输出截图已检查。日志见 `portable-smoke.log`，结果见tests同任务`portable-smoke/result.json`。

未执行反馈者5060、RTX30/40实机、VC-007PRO硬件帧率协商及物理HDR显示。本轮没有更换NVIDIA/Intel运行组件。下一步是用户实卡验证30/40 FPS是否接受及游戏节奏。
