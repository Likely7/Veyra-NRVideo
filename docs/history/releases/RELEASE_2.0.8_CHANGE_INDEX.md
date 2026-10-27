# 2.0.8 完整改动覆盖索引

范围：已发布v2.0.7至用户验证后的main3107e00。覆盖双方全部非merge提交；中间撤回/替换方案标明，不计为最终功能。主说明按最终行为整理，发布文档本身另列工作日志。

| 提交 | 分组 | 内容/状态 |
|---|---|---|
| [7571919](https://github.com/Likely7/Veyra-NRVideo/commit/757191948bba0d847825d84ae9a1602bcaf8713a) | 集成收尾/构建/验收记录 | Record verified Veyra 2.0.7 publication and unchanged package receipts |
| [0f9a61e](https://github.com/Likely7/Veyra-NRVideo/commit/0f9a61ee2d79b280bd241c733577cca61ba3de09) | 启动检查更新 | Add asynchronous startup update checks and transient release banner |
| [72bd143](https://github.com/Likely7/Veyra-NRVideo/commit/72bd14346e844edde2a53e9d44b13958f5f7a3db) | 启动检查更新 | Use explicit QString values in update-check data rows |
| [7628526](https://github.com/Likely7/Veyra-NRVideo/commit/7628526223b5ee704acfdca46bf01197c5de3e8c) | 启动检查更新 | Handle canceled replies quietly and compare HTTP headers without case sensitivity |
| [9d5a42e](https://github.com/Likely7/Veyra-NRVideo/commit/9d5a42e471e98193d116d8e7b78a32d9f70e9d09) | 启动检查更新 | Log automatic versus manual update requests for startup diagnostics |
| [4d944de](https://github.com/Likely7/Veyra-NRVideo/commit/4d944dec4097075573c9f0120da91a6777ace0d9) | 启动检查更新 | Record update-check validation, failure evidence and preserved main |
| [f91c771](https://github.com/Likely7/Veyra-NRVideo/commit/f91c7715d71530478364303c41600f05b36bf28f) | 延迟证据 | docs: audit capture NR and XeSS presentation latency |
| [c1dd0a2](https://github.com/Likely7/Veyra-NRVideo/commit/c1dd0a2d66f762624bdcc2d57243bb2559f95882) | 延迟/显示/VRR | 呈现提交策略与显示积压优化 |
| [f9e6dfc](https://github.com/Likely7/Veyra-NRVideo/commit/f9e6dfc1afc9624405318c67ab730e86d7c576f9) | 延迟/显示/VRR | 输出预算的中间实现，后由e550f96替换为只提示，不作为最终自动降倍率行为发布 |
| [5f15a66](https://github.com/Likely7/Veyra-NRVideo/commit/5f15a663e1003ee937d4dea163de800417f35331) | 旋转/播放区域/BMD | fix: align node viewport and rotate playback with explicit BMD inputs |
| [25601e4](https://github.com/Likely7/Veyra-NRVideo/commit/25601e44607cee7be664a57281fe011c17a9f28d) | 旋转/播放区域/BMD | test: cover playback display matrices and BMD connection persistence |
| [d64d7af](https://github.com/Likely7/Veyra-NRVideo/commit/d64d7afda94f9ae3acf606ce9a61c8992a0ec431) | 旋转/播放区域/BMD | fix: keep portrait viewport consistent when rotation metadata has no SAR |
| [7bcae2b](https://github.com/Likely7/Veyra-NRVideo/commit/7bcae2bb6f8fdf1fe121f40e5bad041e882f24d6) | 旋转/播放区域/BMD | docs: record verified issue repairs and BMD hardware limits |
| [345f87f](https://github.com/Likely7/Veyra-NRVideo/commit/345f87fefffa82a5d3c779a4cd958df9a726b701) | 旋转/播放区域/BMD | docs: clarify final translation audit and evidence counts |
| [ada49ff](https://github.com/Likely7/Veyra-NRVideo/commit/ada49ff01ec2abf6d54cb4f7c37d2c37b1d390f5) | 延迟/显示/VRR | latency-min B3: explicit VSync keeps a one-deep display queue |
| [3c2d6ca](https://github.com/Likely7/Veyra-NRVideo/commit/3c2d6caae8ee7f9bb55293b261e9c1cb8fc202b8) | 延迟/显示/VRR | latency-min B1: XeSS presents right after submit, one XeLL cycle per loop |
| [dca66de](https://github.com/Likely7/Veyra-NRVideo/commit/dca66de35f4021afee5f6b4fa4a911acccbbf766) | 延迟/显示/VRR | latency-min C1: report the monitor's variable-refresh state (NVIDIA) |
| [0676c78](https://github.com/Likely7/Veyra-NRVideo/commit/0676c785765ab4578a01dde1f615645bd650eed6) | 延迟/显示/VRR | latency-min C1: attribute the VRR query to the existing NVAPI (MIT) notice |
| [8182472](https://github.com/Likely7/Veyra-NRVideo/commit/81824724f1dfb4d498721ca33ffeab70964180cd) | 延迟/显示/VRR | latency-min C2/C3: measure presentation modes and the capture copy |
| [f7da409](https://github.com/Likely7/Veyra-NRVideo/commit/f7da409002bd9ac6a001dfcf71a6a023caff8916) | 延迟/显示/VRR | latency-min: record B1-C3 completion in AGENTS.md |
| [4844dce](https://github.com/Likely7/Veyra-NRVideo/commit/4844dce576981184f45a5bb103aef1043aa7921a) | 延迟/显示/VRR | diag(present): date earlier VSync presents through the bound only in bursts |
| [e550f96](https://github.com/Likely7/Veyra-NRVideo/commit/e550f966117c8eabdbe3b3b0794b73b2f85710a8) | 延迟/显示/VRR | latency-min: FG output hint instead of reduction; 可变刷新 switch |
| [edce7d1](https://github.com/Likely7/Veyra-NRVideo/commit/edce7d18f31a718fe5d400251a0daa9e51a7d1a0) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: DLSS 性能模式, compute GPU choice, fullscreen Home panel from 极简/节点 |
| [2e43f7b](https://github.com/Likely7/Veyra-NRVideo/commit/2e43f7b99e690ee2c7edbe9d94d21f7310b5c32c) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: record the DLSS-G 310.9.16 bundling approval; PC game passthrough plan |
| [0f1b5de](https://github.com/Likely7/Veyra-NRVideo/commit/0f1b5de2e92d01b0d938d1398a737c936c1a2c34) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: PC 游戏模式 (click-through screen capture); borderless monitor capture |
| [ada4f03](https://github.com/Likely7/Veyra-NRVideo/commit/ada4f03c41386d5f703cf6555eeab3ad8673e027) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: game mode record, VFG findings, 310.9.16 release staging |
| [d295327](https://github.com/Likely7/Veyra-NRVideo/commit/d2953277ff085f06532640e6f896ec567c547ccb) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: screen capture hint to cap at 60 fps for DLSS5 (NR) |
| [af07e9f](https://github.com/Likely7/Veyra-NRVideo/commit/af07e9f721d34b05ced38842958c3eba1d14c706) | 游戏模式/双卡/DLSS性能/手柄 | fg-gpu-home: PS5 controller-only + capture card plan for 2.0.x (PR #7) |
| [a79a740](https://github.com/Likely7/Veyra-NRVideo/commit/a79a7408cb7c883dfb7bf29390981a43af87c7ae) | 游戏模式/双卡/DLSS性能/手柄 | ps5-control: PS5 controller-only Remote Play beside the capture card (PR #7, option A) |
| [f6ec60c](https://github.com/Likely7/Veyra-NRVideo/commit/f6ec60c8c82b49c196ec2f65c835d4f0964d9f1f) | 游戏模式/双卡/DLSS性能/手柄 | ps5-control: test-only switch letting a playing file stand in for the capture card |
| [a7d8598](https://github.com/Likely7/Veyra-NRVideo/commit/a7d8598fe7c6d6d158f7b66f25f330f3c73329d9) | 游戏模式/双卡/DLSS性能/手柄 | ps5-control: do not restart while the capture run is stopping; "Pair…" label |
| [6a97dc1](https://github.com/Likely7/Veyra-NRVideo/commit/6a97dc1ce8e967022e3a36b474adf38b487b00cf) | 游戏模式/双卡/DLSS性能/手柄 | ps5-control: GUI and regression results, r6 test package |
| [d2239c6](https://github.com/Likely7/Veyra-NRVideo/commit/d2239c635b856f9aa74955e5f74325aae50d5582) | 采集/UI现场修复 | Fix capture dialog hang with huge format lists and UI recovery after GPU resets |
| [227af7b](https://github.com/Likely7/Veyra-NRVideo/commit/227af7b1b9efd3007f4214ea30ae4244b2454d47) | 采集/UI现场修复 | Record field test packages for the 2026-10-15 fixes |
| [5d5c936](https://github.com/Likely7/Veyra-NRVideo/commit/5d5c9365ff150786d192fba2e4252a888f7200a3) | 采集/UI现场修复 | Correct the test count in the 2026-10-15 worklog entry |
| [6fb7df2](https://github.com/Likely7/Veyra-NRVideo/commit/6fb7df2807e658250d37a74d9dcebe5764b34fa5) | 采集/UI现场修复 | List common capture frame rates by default with a show-all switch |
| [37d975f](https://github.com/Likely7/Veyra-NRVideo/commit/37d975f2cfe721afe9487c016c8a75b55fcbe21e) | 采集/UI现场修复 | Record r2 test packages with the frame-rate menu change |
| [d06ed04](https://github.com/Likely7/Veyra-NRVideo/commit/d06ed048cfa74a07daebd259582ec5c4cefded7c) | 采集/UI现场修复 | Skip native-size RTX Video SR for uncompressed capture |
| [ff4ba03](https://github.com/Likely7/Veyra-NRVideo/commit/ff4ba037635ba9fe75bd4f9aceeed5b9cbf0179d) | 采集/UI现场修复 | Record r3 test packages with the native-size SR change |
| [18fa6f3](https://github.com/Likely7/Veyra-NRVideo/commit/18fa6f3abfab21f1e15aa8eb7be8edc730e84a96) | 采集/UI现场修复 | Record the 2.0.8 pre-release audit |
| [d7d2b0a](https://github.com/Likely7/Veyra-NRVideo/commit/d7d2b0a09f0a0bc0cf92f88df519db77a4ca7887) | 集成收尾/构建/验收记录 | 2.0.8 fixes: slider jank, window resize rebuilds, log size, switch bindings, GC550 audio, Douyin companion |
| [d5dce3a](https://github.com/Likely7/Veyra-NRVideo/commit/d5dce3a6d786b879d7d8572029182ca6fa563fd4) | 集成收尾/构建/验收记录 | Add the 2.0.8 fresh build script |
| [41ec119](https://github.com/Likely7/Veyra-NRVideo/commit/41ec119da84492a53a3e0f27ae7fb6009834e556) | 集成收尾/构建/验收记录 | Build the realtime preview tests as UTF-8; add the 2.0.8 package script |
| [3db8800](https://github.com/Likely7/Veyra-NRVideo/commit/3db8800126fd133d95669df9f9cf715bb7e35c25) | 集成收尾/构建/验收记录 | Add VEYRA_TEST_LOG_ROTATE_BYTES for log rotation tests |
| [0e89c62](https://github.com/Likely7/Veyra-NRVideo/commit/0e89c621ddc36ebf8dfd3e68ff65a4f477026a8c) | 集成收尾/构建/验收记录 | Export page test covers the stop-playback switch; quieter VIcon in tests |
| [1c2a1f8](https://github.com/Likely7/Veyra-NRVideo/commit/1c2a1f8012598604fd61ff75c55143ad62bc1fb9) | 集成收尾/构建/验收记录 | Publish the applied revision after its metrics window |
| [f074e40](https://github.com/Likely7/Veyra-NRVideo/commit/f074e408b9cc3d8a9ca7bd30498c4114ffa72188) | 集成收尾/构建/验收记录 | Record the user's decisions after the 2.0.8 audit |
| [99435ae](https://github.com/Likely7/Veyra-NRVideo/commit/99435ae371f28f871a78748b319aa653046a336d) | 集成收尾/构建/验收记录 | test: separate startup and timer scheduling from steady-state checks |
| [71bba7e](https://github.com/Likely7/Veyra-NRVideo/commit/71bba7e046dc76a9671b05f1220cbce5989695ec) | 集成收尾/构建/验收记录 | Document 2.0.8 handoff validation and test delivery limits |
| [80a3d7c](https://github.com/Likely7/Veyra-NRVideo/commit/80a3d7cc3b66e9b49cd64f97c9552fe879098912) | 集成收尾/构建/验收记录 | Use the native PowerShell security module for runtime identity checks |
| [3107e00](https://github.com/Likely7/Veyra-NRVideo/commit/3107e0076a0a8ad3cfdee767a0ea36152ed402a4) | 集成收尾/构建/验收记录 | Record completed 2.0.8 local test packages and cold-start checks |

共51个非merge提交、118个最终变更路径，其中72个产品/组件声明路径。下面列出最终产品路径，构建/测试/文档路径可由完整diff核对。

- `CMakeLists.txt`
- `THIRD_PARTY_NOTICES.md`
- `apps/veyra-qml/main.cpp`
- `i18n/catalog.json`
- `include/veyra/Log.h`
- `include/veyra/engine/DisplayFgBudget.h`
- `include/veyra/engine/EngineController.h`
- `include/veyra/engine/EnhancementSettings.h`
- `include/veyra/engine/GraphDescription.h`
- `include/veyra/engine/PresentationSettings.h`
- `include/veyra/engine/PreviewView.h`
- `include/veyra/engine/VideoPresenter.h`
- `include/veyra/gfx/D3D12DeviceContext.h`
- `include/veyra/gfx/GpuSchedulingPriority.h`
- `include/veyra/gfx/NvidiaVrr.h`
- `include/veyra/gfx/PresentSink.h`
- `include/veyra/gfx/PresentationHooks.h`
- `include/veyra/ngx/DlssFgBackend.h`
- `include/veyra/ngx/DlssgLite.h`
- `include/veyra/ngx/NgxCoreCache.h`
- `include/veyra/ngx/NgxCoreHost.h`
- `include/veyra/pipeline/EnhanceGraph.h`
- `include/veyra/remoteplay/ChiakiBackend.h`
- `include/veyra/remoteplay/ControllerInput.h`
- `include/veyra/source/CaptureCardSource.h`
- `include/veyra/source/CaptureFormatSelection.h`
- `include/veyra/source/CaptureFrameIntervals.h`
- `include/veyra/source/DeckLinkInputConnection.h`
- `include/veyra/source/RemotePlaySessionSource.h`
- `include/veyra/source/RemotePlaySource.h`
- `include/veyra/source/ScreenCaptureSource.h`
- `include/veyra/ui/GamePassthrough.h`
- `include/veyra/ui/QmlPlayerBridge.h`
- `include/veyra/ui/UpdateChecker.h`
- `qml/Veyra/DialogHost.qml`
- `qml/Veyra/ExportPage.qml`
- `qml/Veyra/HomePage.qml`
- `qml/Veyra/Main.qml`
- `qml/Veyra/NodePage.qml`
- `qml/Veyra/NrLayerEditor.qml`
- `qml/Veyra/ProPage.qml`
- `qml/Veyra/SettingsPage.qml`
- `qml/Veyra/UpdateBanner.qml`
- `qml/Veyra/VIcon.qml`
- `qml/Veyra/VRow.qml`
- `qml/Veyra/VSwitch.qml`
- `qml/Veyra/qmldir`
- `shaders/PresentBlit.hlsl`
- `shaders/PresentMotion.hlsl`
- `shaders/PresentationRotation.hlsli`
- `src/base/Log.cpp`
- `src/engine/EffectChain.cpp`
- `src/engine/EngineController.cpp`
- `src/engine/VideoExportJob.cpp`
- `src/engine/VideoPresenter.cpp`
- `src/gfx/D3D12DeviceContext.cpp`
- `src/gfx/GpuSchedulingPriority.cpp`
- `src/gfx/PresentSink.cpp`
- `src/gfx/XessPresenter.cpp`
- `src/ngx/DlssFgBackend.cpp`
- `src/ngx/NgxCoreHost.cpp`
- `src/pipeline/EnhanceGraph.cpp`
- `src/remoteplay/ChiakiBackend.cpp`
- `src/remoteplay/ControllerInput.cpp`
- `src/source/CaptureCardSource.cpp`
- `src/source/DeckLinkCapture.cpp`
- `src/source/DeckLinkDriver.cpp`
- `src/source/RemotePlaySessionSource.cpp`
- `src/source/RemotePlaySource.cpp`
- `src/source/ScreenCaptureSource.cpp`
- `src/ui/QmlPlayerBridge.cpp`
- `src/ui/UpdateChecker.cpp`

最终方案：刷新预算只提示并保留所选倍率；VFG无模型更新；研究的多线程采集拷贝/零拷贝未作为新功能；真实VRR和对应硬件未由模拟验收替代。
