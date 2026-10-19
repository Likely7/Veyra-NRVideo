# 2.0.9 → 2.1.0 全部变更索引

范围：v2.0.9（08832f6）至已验main d91ec9f；共86提交（非merge 74），223变更路径。包含双方提交、云端PR、合并与验收文档；下列提交标题不是逐条新功能声明。产品总结见 RELEASE_NOTES_2.1.0.md。Lecram实验不在本范围。

## 全部提交

- `d68d6be9c371` Record verified Veyra 2.0.9 publication and unchanged asset digests
- `1295d6bc6c90` Fix window game capture entry, recovery and resize geometry
- `09ac54bd31ce` Fix capture capability discovery and restore legacy format picker
- `9c7b342a9f53` Merge commit '09ac54bd31ceb28f613f38beab6bbb05b486efdb' into codex/pr57-capture-window-20261017
- `c9e6e2c93a49` Scope PR57 Windows integration and native capture validation
- `f21e7f5d2bdd` Isolate scaled game mouse gestures and keep MJPEG warnings in the legacy picker
- `18c7cabfc729` Cover static capture-window resize independently of QML animation timing
- `83884092e64d` Recover static WGC resize and scope capture picker to Basic controls
- `1f283dc9ccbd` Recreate the WGC session and pool together during static resize recovery
- `7e2157d3a427` Record PR57 Windows capture and game-mode acceptance with baseline limits
- `26c0d61c395a` Record confirmed PR57 GitHub merge receipt
- `95c730959bdd` Record the beauty node authorization, archive and plan
- `9a3025950b2c` Start #48 network stability and server buffering: authorization, plan, worklog
- `df8028af15d6` Start E0 + #53 anti-aliasing: authorization, plan, slot regression harness
- `50e40862de2a` Record E0 + #53 start state in WORKLOG (baseline recorded, product code unchanged)
- `902ea4e78f7f` WIP (unbuilt, not wired into CMake): YuNet GPU face detector sources and build script
- `c6d933c8bd56` Add shared demux session for network and server sources (C2/C3/C4/C7 core)
- `c4da72d53fbe` Engine integration of the demux session, test server and network buffer tests
- `2ad5559547d2` Live reconnect fixes: flush instead of skip_frame, skip damaged network audio, tolerate Waiting in the preview skip loop
- `0d07c9de5011` Network playback settings, signal-panel line and buffering UI (C6/C7)
- `3e513c1ebd0e` Subtitle scan of a file on a server waits for playback and reads at a bounded rate (C5)
- `d46d47a108e7` Demux session: timestamp-independent flow control, restarted live timelines, 2 s reconnect cap
- `5d9e57f8baf5` Finish #48: HLS best variant, live reconnect tuning, interlaced damage tolerance, verified matrix
- `f0cbbbf1ef7c` chore: update star history chart [skip ci]
- `f01bf4454471` E0: effect infrastructure for anti-aliasing and beauty (byte-identical colour slots)
- `eee3cd16482c` Scope isolated AMD NR 0.44 integration and pin the hotfixed upstream runtime
- `4db24dada137` fix(capture): capture 5.1 audio from AVerMedia cards through the vendor audio graph
- `09f364c0b326` Integrate AMD NR 0.44 with isolated Small and Original models
- `78c3359e8ff9` Keep lmxxf source patches applicable on Windows checkouts
- `bf1c433cc34d` Avoid Windows small macro in real-runtime metadata test
- `83aa06f0f5a5` Validate loaded AMD runtime configuration and refresh FSR reset contract
- `6fb570f6588f` Audit both AMD 0.44 model modules in the publisher packaging recipe
- `bd6395e4c36e` Require both AMD model modules in 0.44 package manifests
- `c0ead6f9602c` Validate effective AMD controls inside the runtime CRT
- `0d08268f9a49` Exercise real runtime config rejection across separate CRTs
- `8144df486ef4` Pin final audited AMD modules with effective-control introspection
- `94861a0d30f2` Reset injected fixture controls in the owning runtime CRT
- `e1a65084bc55` Record AMD NR 0.44 integration evidence and hardware limits
- `f6c4fd9259bd` Use the audited 0.44 parameter-file count in integration documentation
- `33ec229dafd7` fix(ui): preserve fullscreen click origin and native video cursor policy
- `2065efb546b5` fix(ui): retain picture double-click transport state across fullscreen exit
- `6571e5d37765` #53: spatial anti-aliasing effect (FXAA / SMAA / MLAA, experimental temporal SMAA)
- `af415f3efece` Merge claude/aa-53-20261017 (E0 + #53 anti-aliasing) into the beauty branch
- `5642fc5c63f8` Beauty P0: YuNet GPU face detector builds and matches OpenCV
- `3a6ac1d4fa8d` docs: record fullscreen click and native cursor acceptance
- `81474966be28` Merge PR63 AVerMedia vendor audio graph for local integration
- `1f96e8f8e9e5` fix(capture): adapt PR63 audio identity, carrier transitions and bounded vendor recovery
- `9ef2aeacf5f3` fix(capture): preserve audio fallback state and guard failed vendor shutdown
- `a5a762c145f5` test(capture): exercise missing vendor ABI and audio carrier boundaries
- `a22b9ac9def1` fix(capture): retain verified non-PCM arming on the vendor STA
- `0720b560bc0b` test(capture): use real codec layout reference and exact PE export names
- `6f683ef331b5` test(capture): recognize the process main STA apartment
- `273acbbf6455` fix(audio): preserve sample alignment when fragmented carriers return to PCM
- `3a77635ff374` fix(audio): bound stalled codec accumulation during vendor capture
- `020bfca1f254` docs: record PR63 audio integration tests and hardware limits
- `642f6087c34a` Merge pull request #63: AVerMedia multichannel capture audio
- `8463a60b034c` Merge GitHub PR63 result into validated audio integration
- `786efeb2c99d` docs: finalize PR63 main integration record
- `7e86637bd494` Beauty node engine: face restore + skin smoothing, GPU face tracking, formats
- `a078eb5b4cd8` Beauty node UI, export/node-order fixes, docs and acceptance
- `91eeae761d65` Beauty tracking: detect every frame, One Euro smoothing (no lag, no overshoot)
- `a7034d67ab75` fix(capture): arm AVerMedia passthrough and decode AC-3 frames immediately
- `2019128b8052` Beauty: MediaPipe 468-point face mesh for real face contours and feature exclusion
- `390882eb7f5d` Docs: drop the trailing blank line in the beauty implementation record
- `289da1f50ae9` Merge PR64 AVerMedia arming and framed decoding for local adaptation
- `2ac8dca045e4` fix(capture): adapt PR64 SDK readback and bounded audio delivery recovery
- `d4ee06704f46` fix(capture): log SDK query faults and correct prior audit counts
- `130906e2dafa` docs: record PR64 arming, framed latency and compatibility tests
- `1708aa316998` fix(capture): serialize passthrough arming before vendor monitoring
- `c55a4bf547ed` fix(capture): resume monitoring on same-device default apply
- `357781d324d8` docs: record final serialized monitor acceptance for PR64
- `f21b27317180` Merge pull request #64: AVerMedia passthrough arming and immediate framed audio decoding
- `4facb8144a44` Merge GitHub PR64 result into validated audio integration
- `1dc8c18869c2` docs: finalize PR64 main integration record
- `20ab49304c35` feat(ui): home sponsor line and 设置 → 赞助作者 page
- `dcd95b640177` fix(ui): sponsor page puts Ko-fi first, the codes for mainland China
- `c4e7872b16b2` Merge codex/fullscreen-input-20261018 (with AMD NR 0.44) into 2.1.0
- `92f04d100fee` Merge claude/network-buffer-48-20261017 (#48 network buffering) into 2.1.0
- `576ed84bf734` Merge claude/beauty-20261017 (E0 effects infrastructure, #53 anti-aliasing, beauty with face mesh) into 2.1.0
- `a4bd6dd1656d` Merge claude/sponsor-20261019 (home title, 设置 → 赞助作者) into 2.1.0
- `a9f38fc3d76b` Version 2.1.0
- `f3ab441daf36` 2.1.0 test build records, beauty model packaging rule, task-aware beauty GUI test
- `e3453c2aa8f7` docs: 2.1.0-test package and cold-start receipts
- `22f58cfc5623` Fix author Bilibili homepage for 2.1.0 test packages
- `66b321051bef` Record corrected 2.1.0 local build and packaging recipe
- `d91ec9f7214f` Record 2.1.0 corrected package verification

## 全部路径（A新增 / M修改 / D删除）

```text
M	.gitattributes
M	AGENTS.md
M	CMakeLists.txt
M	THIRD_PARTY_NOTICES.md
M	apps/veyra-qml/main.cpp
M	apps/veyra/TelemetryWindow.cpp
M	assets/star-history.svg
A	cmake/VeyraBeauty.cmake
M	cmake/VeyraShaders.cmake
A	docs/AMD_NR_044_PLAN_2026-10-18.md
A	docs/AMD_NR_044_REPORT_2026-10-18.md
A	docs/BEAUTY_IMPLEMENTATION_PLAN_2026-10-17.md
A	docs/BILIBILI_2.1.0_FIX_2026-10-19.md
A	docs/BUILD_2.1.0_TEST.md
A	docs/CAPTURE_FORMAT_FIXES_2026-10-17.md
A	docs/EFFECTS_INFRA_AA_53_PLAN_2026-10-17.md
A	docs/FULLSCREEN_INPUT_PLAN_2026-10-18.md
A	docs/FULLSCREEN_INPUT_REPORT_2026-10-18.md
A	docs/NETWORK_BUFFER_48_PLAN_2026-10-17.md
A	docs/PR57_INTEGRATION_PLAN_2026-10-17.md
A	docs/PR57_INTEGRATION_REPORT_2026-10-17.md
A	docs/PR63_AVERMEDIA_AUDIO_PLAN_2026-10-18.md
A	docs/PR63_AVERMEDIA_AUDIO_REPORT_2026-10-18.md
A	docs/PR64_AVERMEDIA_PLAN_2026-10-19.md
A	docs/PR64_AVERMEDIA_REPORT_2026-10-19.md
A	docs/RELEASE_2.0.9_REPORT_2026-10-17.md
A	docs/RELEASE_2.1.0_TEST_2026-10-19.md
A	docs/SPONSOR_PAGE_2026-10-19.md
A	docs/WINDOW_GAME_FIXES_2026-10-17.md
M	docs/WORKLOG.md
M	i18n/catalog.json
A	include/veyra/beauty/FaceMeshGpu.h
A	include/veyra/beauty/FaceTracker.h
A	include/veyra/beauty/OnnxLite.h
A	include/veyra/beauty/TfliteLite.h
A	include/veyra/beauty/YuNetGpu.h
M	include/veyra/diagnostics/EnhancementProcessingTime.h
M	include/veyra/diagnostics/FrameMetrics.h
M	include/veyra/diagnostics/GpuTimer.h
A	include/veyra/engine/AntiAliasSettings.h
A	include/veyra/engine/BeautySettings.h
M	include/veyra/engine/EffectChain.h
M	include/veyra/engine/EngineController.h
M	include/veyra/engine/EnhancementSettings.h
M	include/veyra/engine/ExportFeaturePolicy.h
M	include/veyra/engine/GraphDescription.h
M	include/veyra/engine/RecentGraphCache.h
M	include/veyra/engine/Subtitles.h
A	include/veyra/media/DemuxSession.h
M	include/veyra/media/FFmpegDemuxer.h
M	include/veyra/media/FFmpegVideoDecoder.h
M	include/veyra/media/NetworkInput.h
A	include/veyra/media/NetworkSettings.h
A	include/veyra/pipeline/AntiAliasPass.h
A	include/veyra/pipeline/BeautyPass.h
M	include/veyra/pipeline/EnhanceGraph.h
M	include/veyra/pipeline/GpuPassUtils.h
M	include/veyra/pipeline/LmxxfNrBackend.h
A	include/veyra/pipeline/SmaaLookupTables.h
M	include/veyra/sink/BitstreamAudio.h
M	include/veyra/sink/WasapiAudioSink.h
A	include/veyra/source/AverMediaAudioCarrier.h
M	include/veyra/source/AverMediaAudioSwitch.h
A	include/veyra/source/AverMediaVendorAudio.h
A	include/veyra/source/AverMediaVendorProtocol.h
A	include/veyra/source/CaptureAudioDeliveryPolicy.h
A	include/veyra/source/CaptureCapabilityRange.h
M	include/veyra/source/CaptureCardSource.h
M	include/veyra/source/CaptureFormatSelection.h
M	include/veyra/source/CaptureFrameIntervals.h
A	include/veyra/source/DirectShowFormatDiscovery.h
M	include/veyra/source/IFrameSource.h
M	include/veyra/source/MediaFileSource.h
M	include/veyra/ui/GameHotkeys.h
A	include/veyra/ui/GameModePolicy.h
M	include/veyra/ui/GamePassthrough.h
M	include/veyra/ui/QmlPlayerBridge.h
M	prototypes/ui-redesign-2026-09-25/icons.js
A	qml/Veyra/AntiAliasEditor.qml
A	qml/Veyra/BeautyEditor.qml
A	qml/Veyra/CaptureLegacyPicker.qml
M	qml/Veyra/DialogHost.qml
M	qml/Veyra/HomePage.qml
M	qml/Veyra/IconData.js
M	qml/Veyra/Main.qml
M	qml/Veyra/MinimalPage.qml
M	qml/Veyra/NodePage.qml
M	qml/Veyra/NrLayerEditor.qml
M	qml/Veyra/ProPage.qml
M	qml/Veyra/SettingsPage.qml
A	qml/Veyra/SponsorPanel.qml
M	qml/Veyra/qmldir
A	qml/Veyra/sponsor/alipay.png
A	qml/Veyra/sponsor/alipay.svg
A	qml/Veyra/sponsor/bilibili.svg
A	qml/Veyra/sponsor/douyin.svg
A	qml/Veyra/sponsor/github.svg
A	qml/Veyra/sponsor/kofi.svg
A	qml/Veyra/sponsor/wechat.png
A	qml/Veyra/sponsor/wechat.svg
A	qml/Veyra/sponsor/x.svg
A	qml/Veyra/sponsor/youtube.svg
A	scripts/acceptance/aa-53-gui.py
A	scripts/acceptance/aa-53-gui.qml
A	scripts/acceptance/beauty-gui.py
A	scripts/acceptance/beauty-gui.qml
A	scripts/acceptance/claude-beauty-build.py
A	scripts/acceptance/sponsor-gui.py
A	scripts/acceptance/sponsor-gui.qml
M	scripts/amd-nr/build-lmxxf-runtime.py
M	scripts/amd-nr/msvc-compat.h
A	scripts/amd-nr/stage-044-runtime.py
A	scripts/amd-nr/veyra-runtime-model.cpp
A	scripts/beauty/facemesh-reference.py
A	scripts/beauty/yunet-reference.py
A	scripts/generate-smaa-lookup-tables.py
M	scripts/package-qml-release.py
M	scripts/package-vendor-release.py
A	shaders/AaFxaa.hlsl
A	shaders/AaLuma.hlsl
A	shaders/AaMlaa.hlsli
A	shaders/AaMlaaBlend.hlsl
A	shaders/AaMlaaCounts.hlsl
A	shaders/AaMlaaEdges.hlsl
A	shaders/AaSmaaBlend.hlsl
A	shaders/AaSmaaEdges.hlsl
A	shaders/AaSmaaWeights.hlsl
A	shaders/AaTemporalResolve.hlsl
A	shaders/AntiAlias.hlsli
A	shaders/Beauty.hlsli
A	shaders/BeautyApply.hlsl
A	shaders/BeautyBlur.hlsl
A	shaders/BeautyFaces.hlsli
A	shaders/BeautyMesh.hlsli
A	shaders/BeautyMeshConv.hlsl
A	shaders/BeautyMeshCrop.hlsl
A	shaders/BeautyMeshDecode.hlsl
A	shaders/BeautyMeshDepthwise.hlsl
A	shaders/BeautyMeshPointwise.hlsl
A	shaders/BeautyRoi.hlsl
A	shaders/BeautyStats.hlsl
A	shaders/BeautyTrack.hlsl
A	shaders/BeautyYuNet.hlsli
A	shaders/BeautyYuNetDecode.hlsl
A	shaders/BeautyYuNetDepthwise.hlsl
A	shaders/BeautyYuNetPointwise.hlsl
A	shaders/BeautyYuNetPool.hlsl
A	shaders/BeautyYuNetPrep.hlsl
A	shaders/BeautyYuNetStem.hlsl
A	shaders/BeautyYuNetUpAdd.hlsl
A	shaders/Smaa.hlsli
A	src/beauty/FaceMeshGpu.cpp
A	src/beauty/FaceTracker.cpp
A	src/beauty/OnnxLite.cpp
A	src/beauty/TfliteLite.cpp
A	src/beauty/YuNetGpu.cpp
M	src/engine/EffectChain.cpp
M	src/engine/EngineController.cpp
M	src/engine/EngineControllerImage.cpp
M	src/engine/ExportJobManager.cpp
M	src/engine/PresetLibrary.cpp
M	src/engine/PreviewGpuSession.cpp
M	src/engine/Subtitles.cpp
M	src/engine/TiledImageProcessor.cpp
A	src/media/DemuxSession.cpp
M	src/media/FFmpegDemuxer.cpp
M	src/media/FFmpegVideoDecoder.cpp
A	src/pipeline/AntiAliasPass.cpp
A	src/pipeline/BeautyPass.cpp
M	src/pipeline/EnhanceGraph.cpp
M	src/pipeline/LmxxfNrBackend.cpp
A	src/pipeline/SmaaLookupTables.cpp
M	src/sink/BitstreamAudio.cpp
M	src/sink/WasapiAudioSink.cpp
A	src/source/AverMediaAudioCarrier.cpp
M	src/source/AverMediaAudioSwitch.cpp
A	src/source/AverMediaVendorAudio.cpp
M	src/source/CaptureCardSource.cpp
M	src/source/MediaFileSource.cpp
M	src/source/ScreenCaptureSource.cpp
M	src/ui/PlayerUiFacade.cpp
M	src/ui/QmlPlayerBridge.cpp
A	tests/fixtures/FakeAverMediaSdk.cpp
A	tests/fixtures/FakeAverMediaSdkFull.def
A	tests/fixtures/FakeAverMediaSdkNoGet.def
A	tests/fixtures/FakeAverMediaSdkNoSet.def
A	tests/fixtures/FakeAverMediaVendor.cpp
A	tests/fixtures/FakeAverMediaVendor.def
A	tests/fixtures/FakeAverMediaVendorMissing.def
A	tests/integration/AntiAliasGpuTests.cpp
A	tests/integration/BeautyDetectorTests.cpp
A	tests/integration/BeautyGpuTests.cpp
A	tests/integration/EffectSlotRegressionTests.cpp
M	tests/integration/ExportWorkflowTests.cpp
M	tests/integration/GamePassthroughTests.cpp
M	tests/integration/LmxxfNrGraphTests.cpp
A	tests/integration/LmxxfNrRuntimeMetadataTests.cpp
A	tests/integration/NetworkBufferTests.cpp
M	tests/integration/ScreenCaptureTests.cpp
A	tests/network/make_media.py
A	tests/network/netlib.py
A	tests/network/run_network_tests.py
A	tests/network/slow_server.py
A	tests/qml/quick/tst_anti_alias_editor.qml
A	tests/qml/quick/tst_beauty_editor.qml
A	tests/qml/quick/tst_capture_legacy.qml
M	tests/qml/quick/tst_components.qml
A	tests/qml/quick/tst_sponsor_panel.qml
A	tests/unit/AverMediaCarrierTests.cpp
A	tests/unit/AverMediaPassthroughTests.cpp
A	tests/unit/AverMediaVendorAudioTests.cpp
A	tests/unit/CaptureCapabilityTests.cpp
A	tests/unit/DirectShowFormatDiscoveryTests.cpp
M	tests/unit/EffectChainTests.cpp
A	tests/unit/GameModePolicyTests.cpp
M	tests/unit/LmxxfNrFakeRuntime.cpp
M	tests/unit/LmxxfNrTests.cpp
M	tests/unit/NrModelSettingsTests.cpp
M	tests/unit/PresetLibraryTests.cpp
M	third_party/lmxxf/VEYRA_ORIGIN.md
A	third_party/lmxxf/veyra-model-isolation.patch
M	tools/bitstream_audio_test/main.cpp
A	tools/qt_probe/make-sponsor-icons.py
```
