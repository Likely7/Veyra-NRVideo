# 2.0.6→2.0.7 变更索引

比较起点v2.0.6（e3aa842），终点已验应用树67ab696。主说明见[完整更新报告](RELEASE_NOTES_2.0.7.md)。下面保留全部非merge提交，包含记录、诊断、实验及revert；出现一个提交不代表其功能最终保留，应以终点树和报告为准。尤其60a93f3的完全相同指纹已由8d11cad噪声容差方案取代；VFG自动最大组/连续CUDA、交换链预等、SR静态实验均不得作为发布收益。

## 全部非 merge 提交

- [`96a7c8d`](https://github.com/Likely7/Veyra-NRVideo/commit/96a7c8de36bc195240161de6814739ad810722f1) docs: record verified Veyra 2.0.6 publication
- [`ff8fdf4`](https://github.com/Likely7/Veyra-NRVideo/commit/ff8fdf445734993b78f17fe8d40fd8393eed244f) perf(vfg): test one producer dependency per immutable input pair
- [`adb1c8b`](https://github.com/Likely7/Veyra-NRVideo/commit/adb1c8be544af84513f74a9b8d6908f790a35b80) fix(vfg): budget queued reduced groups from measured single-frame work
- [`ba38f7e`](https://github.com/Likely7/Veyra-NRVideo/commit/ba38f7e4c677969826f202a05ff36e031bed6fe1) fix(vfg): retain severe group estimates with bounded recovery probes
- [`e189420`](https://github.com/Likely7/Veyra-NRVideo/commit/e18942096bb65789a1701f0468acdc64f1be415e) test(vfg): record matched playback and GPU export regressions
- [`71723e0`](https://github.com/Likely7/Veyra-NRVideo/commit/71723e0249a17764696796fd204a6eb69d6da276) docs(vfg): close verified local candidate delivery
- [`5dc256b`](https://github.com/Likely7/Veyra-NRVideo/commit/5dc256b8c7405c0df4fb255500f4095faa4b4879) docs: record VFG main merge and verified project storage cleanup
- [`6309783`](https://github.com/Likely7/Veyra-NRVideo/commit/6309783499538ba239e788d9c185c4ecfe7798ed) docs: record automatic approval limit for final temporary cache cleanup
- [`dbfceab`](https://github.com/Likely7/Veyra-NRVideo/commit/dbfceabee804cd81bac4a4781f0b7b4cf272902e) docs: record desktop artifact cleanup and preservation
- [`3fd3cde`](https://github.com/Likely7/Veyra-NRVideo/commit/3fd3cde2b04fe4117eb544ba24f809ebc97fc185) diagnostics: capture input and presented pixels for FG black flashes
- [`a210ec1`](https://github.com/Likely7/Veyra-NRVideo/commit/a210ec18a4affc930f23f8b6ae48c2c85b7b4d11) diagnostics: add NVIDIA VRAM field collection package
- [`ce50d18`](https://github.com/Likely7/Veyra-NRVideo/commit/ce50d18f6295aa45b4255b64a8b106b8994be99c) Add reversible NVIDIA HDR frame-generation presentation compatibility path
- [`886b461`](https://github.com/Likely7/Veyra-NRVideo/commit/886b461bb0ae5db2b5bf36588c9e1b112685f2c3) fix(capture): adapt BMD raw input to upstream allocator negotiation
- [`f6a8fa2`](https://github.com/Likely7/Veyra-NRVideo/commit/f6a8fa270773e9a2af03632750cbd3f18de7b462) Add opt-in HDR frame generation presentation compatibility setting
- [`0e7018a`](https://github.com/Likely7/Veyra-NRVideo/commit/0e7018adab9b211d76dd05da40cfbaeb9d77cf2c) Validate merged capture and opt-in HDR compatibility fixes
- [`a29243c`](https://github.com/Likely7/Veyra-NRVideo/commit/a29243c1753ca75a63ad1d9e7051ff35178f9a11) fix(subtitles): decode compressed PGS and external bitmap formats
- [`27cf4b4`](https://github.com/Likely7/Veyra-NRVideo/commit/27cf4b4d89bd4ccb2c40b4cb63a67969cf839ed2) docs(subtitles): record code-only delivery preference
- [`51591bc`](https://github.com/Likely7/Veyra-NRVideo/commit/51591bc8afbece4571a0be9728ab66a8fa76ee56) fix: honor render endpoint changes during capture audio
- [`e6ead9f`](https://github.com/Likely7/Veyra-NRVideo/commit/e6ead9f10957e53f69de0231293b62bf975fb039) feat(ui): add persistent folder playlists and floating controls
- [`973fc9b`](https://github.com/Likely7/Veyra-NRVideo/commit/973fc9b816602ed6f11a40e88a54fc8913a2a87c) fix(ui): keep playlist gap drops near the target row
- [`1d7cd55`](https://github.com/Likely7/Veyra-NRVideo/commit/1d7cd55c7790cd3705caa88f99dd4926addeb2f4) docs(capture): trace BMD black frames and native input gaps
- [`af2edf4`](https://github.com/Likely7/Veyra-NRVideo/commit/af2edf4052857c7db99d7c33f92827f02ab34319) fix(capture): add opt-in Blackmagic native input
- [`108cbaa`](https://github.com/Likely7/Veyra-NRVideo/commit/108cbaabce11f3584c03aedc1c2983ebcf7a978c) Fix settings recovery, native video SR and multilingual UI layout
- [`30c6f60`](https://github.com/Likely7/Veyra-NRVideo/commit/30c6f6095fc099963908945fcb1f11a3c84de5ff) Clarify case-level SR evidence and superseded rollback failure
- [`c664a67`](https://github.com/Likely7/Veyra-NRVideo/commit/c664a675c7b7a5c6cb54ed79c07b8a72127f4dfb) fix: keep inactive effect edits off the live GPU path
- [`48d8f34`](https://github.com/Likely7/Veyra-NRVideo/commit/48d8f3464d64ef5e7ee672bbca3526d1bccfca7e) Withdraw SR quality experiments and prepare local fixes test package
- [`5ba186a`](https://github.com/Likely7/Veyra-NRVideo/commit/5ba186a91e868c28b4b5ec46c9166aa321d90998) Record tested fixes merge and current main status
- [`b7dc381`](https://github.com/Likely7/Veyra-NRVideo/commit/b7dc381570e5782682982d1bbbcf23877f907899) diag(vfg): test-only CUDA event split of Run time and stream idle
- [`c4711f5`](https://github.com/Likely7/Veyra-NRVideo/commit/c4711f55c41cf04ff337df4a39675309719791d0) perf(vfg): keep VFG history when a pair is skipped for budget
- [`53ac8ee`](https://github.com/Likely7/Veyra-NRVideo/commit/53ac8ee48582265ecf6f82577fe4f1219ce4ed01) perf(vfg): live VFG falls back to the largest group that fits
- [`3b9da6f`](https://github.com/Likely7/Veyra-NRVideo/commit/3b9da6f3be3d298dfca4d99a0a65dc2ffa7b887d) Revert "perf(vfg): live VFG falls back to the largest group that fits"
- [`d49b145`](https://github.com/Likely7/Veyra-NRVideo/commit/d49b14571b976d9b35c30774ddf657fbbab49d66) diag(present): log Present-to-scanout latency and composition mode
- [`406eb99`](https://github.com/Likely7/Veyra-NRVideo/commit/406eb99f92d8601d6c086858d9bd206859abb03d) perf(present): live frames wait for swap-chain capacity, stale generated frames yield
- [`733bc2a`](https://github.com/Likely7/Veyra-NRVideo/commit/733bc2a0e1989b6deaca173f632f133c503d161b) perf(capture-fg): learn the pair phase from duplicate/disabled groups too
- [`3d93a2e`](https://github.com/Likely7/Veyra-NRVideo/commit/3d93a2e38892f840b48a959cdb683bccdb8f0fca) diag(vfg): resolve profile events in order instead of dropping late ones
- [`548a0f8`](https://github.com/Likely7/Veyra-NRVideo/commit/548a0f8d005cf9d13cbb89dfafd42bf35ac8f0e1) Revert "perf(present): live frames wait for swap-chain capacity, stale generated frames yield"
- [`f95f1b3`](https://github.com/Likely7/Veyra-NRVideo/commit/f95f1b3e03d4c0cac5bcdc7716cdd26a3256511f) diag(present): date presents shown between two statistics samples
- [`7e54b90`](https://github.com/Likely7/Veyra-NRVideo/commit/7e54b90daa0c491796cc7340e2fa9d03a61e224d) perf(vfg): one producer dependency per input pair
- [`6d64882`](https://github.com/Likely7/Veyra-NRVideo/commit/6d64882b0047d9ebffff6c60151c0da509a97e39) ui(present): tell users how to trade tearing or VRR for latency
- [`95248cc`](https://github.com/Likely7/Veyra-NRVideo/commit/95248cc5b312b0342189cf49aef1461132f5908c) ui(vfg): explain why VFG cannot hold the requested multiplier
- [`965951b`](https://github.com/Likely7/Veyra-NRVideo/commit/965951b7d6ea3f8b79b3eaf215503226d3715acf) Revert "perf(vfg): one producer dependency per input pair"
- [`ade8af0`](https://github.com/Likely7/Veyra-NRVideo/commit/ade8af0d0a715cbbba7bccafd5f5a10ca444baf1) fix(capture-fg): recognise duplicate groups after hasGenerated is cleared
- [`d3803ab`](https://github.com/Likely7/Veyra-NRVideo/commit/d3803ab74de1422dc6366d034e5c9aea9d61b81c) docs(vfg): record the 2026-10-10 VFG optimisation, A/B evidence and reverts
- [`be834fd`](https://github.com/Likely7/Veyra-NRVideo/commit/be834fdc9a174de8f9098f27efc1c15c7d8f93b0) docs(pipeline): record the 2026-10-10 software-chain work, PresentMon evidence and reverts
- [`b391b04`](https://github.com/Likely7/Veyra-NRVideo/commit/b391b04b287d796b16559910d9084cec8e67aee8) fix(freeze): software UI with NVIDIA overlay/GamePP, engine hang watchdog, bounded exit
- [`d0694d7`](https://github.com/Likely7/Veyra-NRVideo/commit/d0694d71d7317581ffbab2bb1e9153dae58c14ad) docs(freeze): record logs(15) evidence, overlay compatibility, watchdog tests and regressions
- [`85cbd5a`](https://github.com/Likely7/Veyra-NRVideo/commit/85cbd5a4ce72f970df707e6407bea6a9e226846d) feat(cadence): drop repeated pictures before GPU work and give kept frames content timestamps
- [`67574f2`](https://github.com/Likely7/Veyra-NRVideo/commit/67574f265f1d041795a9363cbdf98ad4fe2d3273) docs(cadence): record GTA6 replay A/B, regressions and limits of the content-cadence redesign
- [`4ceb2df`](https://github.com/Likely7/Veyra-NRVideo/commit/4ceb2df78ebcefef8c2b564afb54eed66b81d595) chore: update star history chart [skip ci]
- [`fb2ee9d`](https://github.com/Likely7/Veyra-NRVideo/commit/fb2ee9df896a5e52449b43690154c164f63dad8f) fix(audio): read file packets ahead on their own thread so slow storage cannot freeze playback
- [`94cbabd`](https://github.com/Likely7/Veyra-NRVideo/commit/94cbabd940488c46eac6fb960d0a5c487c49cd12) fix(capture): Elgato HDR colour and tonemapper parity with Nitlink and OBS beyond the MK.2
- [`87ca1e5`](https://github.com/Likely7/Veyra-NRVideo/commit/87ca1e506ecd1cd56a1d4b067ce11f28fcda1848) docs(audio): record logs16 evidence, read-ahead A/B and regressions
- [`147e76c`](https://github.com/Likely7/Veyra-NRVideo/commit/147e76ccd5f9a97d10d14a33a16071257daffa6c) docs(capture): record the Elgato 1.x coverage gap, Nitlink/OBS comparison and what still needs a field log
- [`29e33df`](https://github.com/Likely7/Veyra-NRVideo/commit/29e33dfd5a8aa88cba9685009a4488d8e96ffc97) Fix clipped software UI icons without full-window repaint
- [`9c14ee2`](https://github.com/Likely7/Veyra-NRVideo/commit/9c14ee28a9175aa326fb0aa09c15bfa5808e0f0e) Integrate tested Claude fixes and detach watchdog safely at shutdown
- [`049902b`](https://github.com/Likely7/Veyra-NRVideo/commit/049902bd941785d63199b025bb5b414da50bd53f) Record Claude fixes merge and integration validation
- [`58dc123`](https://github.com/Likely7/Veyra-NRVideo/commit/58dc1235566c71a244eee0cadd1ab5e13ec3094d) VFG mixed tiers and a GPU shot-change test per input pair
- [`1d4c33f`](https://github.com/Likely7/Veyra-NRVideo/commit/1d4c33f5fe2415d13bb4bfd7a48a6b1657a81c81) Record VFG mixed tiers and GPU shot-change test
- [`e8300d9`](https://github.com/Likely7/Veyra-NRVideo/commit/e8300d90c2a3cddaef3e92ae8a989105647ba16c) Add DLSS 4.5 SR model selection and preset persistence
- [`4557ab6`](https://github.com/Likely7/Veyra-NRVideo/commit/4557ab64e4fb6e53769d3b9360b88d479f41d716) fix(ui): account for intentional cadence and display rate limits
- [`adcf509`](https://github.com/Likely7/Veyra-NRVideo/commit/adcf509e8a752d8a60645c894262cd5dd43f33ad) Record current fixes integration validation
- [`2f191e6`](https://github.com/Likely7/Veyra-NRVideo/commit/2f191e6564c9b4bae1f9edf136a0bbfbdbc76b69) Record current fixes main merge and verification
- [`76f41b3`](https://github.com/Likely7/Veyra-NRVideo/commit/76f41b3813972cf36b52fa0b1446f79713a1ba3e) Document checkout line ending verification
- [`f1f53ac`](https://github.com/Likely7/Veyra-NRVideo/commit/f1f53ace3b9a7bca72f72d908af58ed1471e54f3) Split capture format selection into resolution, frame rate, and pixel format.
- [`66e2407`](https://github.com/Likely7/Veyra-NRVideo/commit/66e240748f4650ba4cc69e8a8ce9b0c6bec11df2) test(fg): J0 motion-input experiment for character-edge jelly
- [`7fe3ea6`](https://github.com/Likely7/Veyra-NRVideo/commit/7fe3ea652b24db16c04557c7b58624f7efebbd53) test(fg): XeSS motion-input experiment through the present path
- [`f468dfb`](https://github.com/Likely7/Veyra-NRVideo/commit/f468dfbd3b0078e2e88b3232fbdada8b5ca5ad2c) fix(xess): refine optical-flow motion boundaries without changing NR guidance
- [`e7af5d8`](https://github.com/Likely7/Veyra-NRVideo/commit/e7af5d8f15f17780ac54b0003299234916d0e080) Fix FSR-only optical flow initialization and complete handoff checks
- [`f0fafdf`](https://github.com/Likely7/Veyra-NRVideo/commit/f0fafdfde9833f02d313830e399e65a52aeaf820) Document XeSS visual A/B measurements and frame comparisons
- [`b60830c`](https://github.com/Likely7/Veyra-NRVideo/commit/b60830c9d97e6927d41b5a2f4c78c1936ff07a88) fix(capture): enumerate all formats and persist complete capture settings
- [`a167831`](https://github.com/Likely7/Veyra-NRVideo/commit/a167831bb2686057de2f6c8a9878ef454d543be0) docs: record PR34 capture compatibility validation and merge
- [`34f479e`](https://github.com/Likely7/Veyra-NRVideo/commit/34f479e6f95229cd45fd81780eb874d8472e1f30) Fix capture cadence resets after short driver timestamps
- [`e1183e7`](https://github.com/Likely7/Veyra-NRVideo/commit/e1183e700a55dd8932c7fd446215ec44bff9747a) fix(xess): fill uncovered background using NVOF forward-backward consistency
- [`e46bbdf`](https://github.com/Likely7/Veyra-NRVideo/commit/e46bbdf08569e740c80f636e3c910a3d0c51e553) feat(xess): Edge Aegis switch and refined motion on AMD optical flow
- [`c125cad`](https://github.com/Likely7/Veyra-NRVideo/commit/c125caddeae7e8f50d647242fe409f849cbaa186) Fix professional video viewport overlapping playback controls
- [`d883216`](https://github.com/Likely7/Veyra-NRVideo/commit/d8832162ee0ab23a568db3a06332bbc9754ce1c2) docs: record final concurrent worktree audit
- [`811fe0d`](https://github.com/Likely7/Veyra-NRVideo/commit/811fe0d1450dc099324d1a5d217adcde818d5835) Prepare integrated 2.0.7 local test build
- [`6501a3e`](https://github.com/Likely7/Veyra-NRVideo/commit/6501a3ebb52cdec9f3f4d2895f5336f2b8f43233) Add reproducible fresh 2.0.7 test build
- [`8c80a62`](https://github.com/Likely7/Veyra-NRVideo/commit/8c80a6235b8ca77674b09ad8071b662cf27a2d0b) Generate matching import libraries for audited subtitle FFmpeg
- [`4a4952f`](https://github.com/Likely7/Veyra-NRVideo/commit/4a4952f4ef44a07b76ca2941b749ad630060ae96) Require supported capture resolution and explicit pixel format without fallback
- [`5a71cd1`](https://github.com/Likely7/Veyra-NRVideo/commit/5a71cd1a7d4679137234f8da728dc74b4f612195) Add audited dual-vendor local 2.0.7 test packaging and source instructions
- [`757dc0b`](https://github.com/Likely7/Veyra-NRVideo/commit/757dc0b0f3da0d9affa8f6568eefa67d284a4232) Stop bounded capture safety probe before requesting GUI exit
- [`721c2db`](https://github.com/Likely7/Veyra-NRVideo/commit/721c2db207151c61de0b27f6282133c2722cc601) Preserve Unicode source paths in local test source archive
- [`5b868e7`](https://github.com/Likely7/Veyra-NRVideo/commit/5b868e7f16b60b190f042de9b4c2ccf5ed5ebb23) Record integrated 2.0.7 validation and capture format safety results
- [`60a93f3`](https://github.com/Likely7/Veyra-NRVideo/commit/60a93f30569d2ace5056742387eb90a8d00fff01) fix(cadence): restore the plain 60->30 sampler; drop only bit-identical repeats
- [`8d11cad`](https://github.com/Likely7/Veyra-NRVideo/commit/8d11cadbe0af1a5b13420db24fa735bce9a2d7fc) fix(cadence): judge repeats per pixel within the capture-noise band
- [`f87e745`](https://github.com/Likely7/Veyra-NRVideo/commit/f87e745fb6ae402c3964c091adb9874702fa67d2) Prepare isolated 2.0.7 cadence refresh build and packaging
- [`e67c0e9`](https://github.com/Likely7/Veyra-NRVideo/commit/e67c0e93e6bf03b82c120451232cfa531eaba545) Preserve Unicode process paths in local package startup receipts
- [`2310c9c`](https://github.com/Likely7/Veyra-NRVideo/commit/2310c9cad5b7ce89776e8ada794fb082fe4eede3) Record rebuilt cadence acceptance and shared-GPU regression limitation
- [`323eb5d`](https://github.com/Likely7/Veyra-NRVideo/commit/323eb5dc9557c179046689228a269a3cd77f5d58) Force and verify actual software renderer in final ZIP startup tests
- [`a94a1ce`](https://github.com/Likely7/Veyra-NRVideo/commit/a94a1cec10f371dcc485e0b61d2d3edcfe3952ec) Exercise actual OBS software UI entry point during package verification

## 最终产品文件差异

共80个产品路径；测试、文档、脚本另见Git完整比较。

```text
M	CMakeLists.txt
M	apps/veyra-qml/main.cpp
M	apps/veyra/ui/LiveStatusHistory.h
M	include/veyra/core/SceneCadenceAnalyzer.h
M	include/veyra/diagnostics/CpuStallTrace.h
A	include/veyra/engine/ContentDedup.h
M	include/veyra/engine/EffectChain.h
M	include/veyra/engine/EngineController.h
A	include/veyra/engine/EngineWatchdog.h
M	include/veyra/engine/EnhancementSettings.h
M	include/veyra/engine/GraphDescription.h
M	include/veyra/engine/PresentationSettings.h
M	include/veyra/engine/Subtitles.h
A	include/veyra/engine/VfgOverloadEstimate.h
A	include/veyra/engine/VfgQueueBudget.h
M	include/veyra/engine/VideoPresenter.h
M	include/veyra/gfx/PresentSink.h
M	include/veyra/gfx/PresentationHooks.h
M	include/veyra/ngx/DlssSrBackend.h
M	include/veyra/pipeline/CaptureUploadFrame.h
M	include/veyra/pipeline/ColorMetadata.h
M	include/veyra/pipeline/EnhanceGraph.h
M	include/veyra/pipeline/FramePacket.h
A	include/veyra/pipeline/PictureCompare.h
M	include/veyra/pipeline/VfgBackend.h
M	include/veyra/sink/WasapiAudioSink.h
M	include/veyra/source/CaptureCardSource.h
M	include/veyra/source/CaptureFormatSelection.h
A	include/veyra/source/CaptureFrameIntervals.h
M	include/veyra/source/CapturePixelFormat.h
A	include/veyra/source/DeckLinkCapture.h
M	include/veyra/source/ElgatoHdrControl.h
M	include/veyra/source/NativeCaptureSink.h
A	include/veyra/ui/PlaylistModel.h
M	include/veyra/ui/QmlPlayerBridge.h
M	qml/Veyra/CineBar.qml
M	qml/Veyra/DialogHost.qml
M	qml/Veyra/FullscreenBar.qml
M	qml/Veyra/HomePage.qml
M	qml/Veyra/Main.qml
M	qml/Veyra/NodePage.qml
A	qml/Veyra/PlaylistWindow.qml
M	qml/Veyra/ProPage.qml
M	qml/Veyra/SettingsPage.qml
M	qml/Veyra/VButton.qml
M	qml/Veyra/VIcon.qml
M	qml/Veyra/VMenu.qml
M	qml/Veyra/VRow.qml
M	qml/Veyra/VSelect.qml
M	qml/Veyra/VSliderValue.qml
M	qml/Veyra/qmldir
A	shaders/FlowClaim.hlsl
A	shaders/FlowFill.hlsl
A	shaders/FlowRefine.hlsl
M	shaders/PackedCaptureToLinear.hlsl
A	shaders/VfgShotChange.hlsl
M	shaders/YuvToLinearRgb.hlsl
M	src/core/SceneCadenceAnalyzer.cpp
M	src/engine/EffectChain.cpp
M	src/engine/EngineController.cpp
A	src/engine/EngineWatchdog.cpp
M	src/engine/PresetLibrary.cpp
M	src/engine/PresetStore.cpp
M	src/engine/Subtitles.cpp
M	src/engine/VideoExportJob.cpp
M	src/engine/VideoPresenter.cpp
M	src/gfx/PresentSink.cpp
M	src/gfx/XessPresenter.cpp
M	src/ngx/DlssSrBackend.cpp
M	src/pipeline/EnhanceGraph.cpp
M	src/pipeline/VfgBackend.cpp
M	src/sink/CaptureAudioSession.cpp
M	src/sink/WasapiAudioSink.cpp
M	src/source/CaptureCardSource.cpp
A	src/source/DeckLinkCapture.cpp
A	src/source/DeckLinkDriver.cpp
M	src/source/ElgatoHdrControl.cpp
M	src/source/NativeCaptureSink.cpp
A	src/ui/PlaylistModel.cpp
M	src/ui/QmlPlayerBridge.cpp
```
