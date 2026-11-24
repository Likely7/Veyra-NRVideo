# In-depth audit of Veyra 2.2.1

- **Repository:** `Likely7/Veyra-NRVideo`
- **Version / commit:** `2.2.1` (`CMakeLists.txt` line 3) · `bc59d457ce0b87141f3db2ebafb0f4812b1cea39`
- **Audit date:** 2026-11-24
- **Scope:** static and architectural review (read-only) of the C++20 / D3D12 / HLSL / Qt 6 QML / CMake code, Python/PowerShell scripts and GitHub Actions workflows.
- **Summary:** **19 confirmed defects** (7 high, 7 medium, 5 low) and **4 known functional/hardware limitations**.
- **Fixes:** see [HANDOFF_2.2.1.md](HANDOFF_2.2.1.md) and the pull request that adds this file. Each item below ends with its **status** in that pull request. The fixes have **not been compiled or tested**.

> [!IMPORTANT]
> Every defect below was checked directly in the source at `bc59d457`. Raw scanner hits (Gitleaks on SHA-256 constants, Oxlint on `.pragma library` in QML JS files, Bandit `B310` on test scripts) were filtered out. Line numbers refer to `bc59d457`.

---

## A. Security, IPC and privilege boundaries (4 defects)

### A1. [HIGH] TOCTOU and heap overflow (~12 MB) in the `VeyraVirtualCamera` shared memory (protocol v2)
- **CWE:** CWE-787, CWE-367, CWE-190, CWE-284
- **Location:** `third_party/obs_virtualcam/ipc/shared-memory-queue.c` L111-L199, `third_party/obs_virtualcam/module/virtualcam-filter.cpp` L298-L346, `third_party/obs_virtualcam/ipc/tiny-nv12-scale.c` L30-L115, `third_party/obs_virtualcam/libdshowcapture/source/output-filter.cpp` L433-L498
- **Cause:** `video_queue_create()` creates the `Local\VeyraVirtualCameraVideoV2` section and its mutex with `lpSecurityAttributes = NULL`, and `video_queue_open()` maps the header `FILE_MAP_READ | FILE_MAP_WRITE` in every consumer process (OBS, Discord, browsers). `vq->frame_bytes` is computed once at open, but `video_queue_get_info()`, `video_queue_write()` and `copy_out()` read the live, writable `vq->header->cx/cy`. Rewriting `cx/cy` from `3840×2160` (`vq->frame_bytes = 12 441 600`) to `640×480` makes `direct = true` and runs `memcpy(dst, ..., 12441600)` into a 460 800-byte DirectShow sample buffer (heap overflow in the host process). Large `cx/cy` also overflow 32-bit `int` arithmetic in `tiny-nv12-scale.c` and `output-filter.cpp`.
- **Fix (45 min):** keep immutable `cx`, `cy`, `interval` in `struct video_queue` from `video_queue_open()`, check the `dst` size before `memcpy`, separate a read-only frame mapping from the consumer slots, use `size_t` for size arithmetic and apply a current-user DACL.
- **Regression test:** open a `3840×2160` queue, rewrite `header->cx/cy` to `640×480` and `65536×4096`, and verify that `video_queue_get_info()` returns the stored size and `copy_out()` rejects mismatched sizes without overrunning the buffer.
- **Status:** fixed (snapshot of size, interval and slot offsets at create/open; scaler checked against it; new `hostile()` test). Not done: a separate read-only mapping and explicit DACL. All consumers run as the same user, so a DACL cannot tell producer from consumer, and protocol v3 would force every user to reinstall the camera component. The scaler (`tiny-nv12-scale.c`) now sizes and indexes in 64-bit / `size_t` arithmetic, and `output-filter.cpp` sizes the sample buffer in 64 bits from the consumer's format, refuses sizes a `long` cannot hold, and checks the allocator's and each sample's actual size before a frame is written.

### A2. [HIGH] Local privilege escalation (split-token `HKCU` COM hijack + `VEYRA_TEST_VIRTUALCAM_MODULE_DIR`) in the camera installer
- **CWE:** CWE-367, CWE-426, CWE-427
- **Location:** `src/virtualcam/CameraSetup.cpp` L69-L97, `src/engine/VirtualCamera.cpp` L27-L45
- **Cause:** the elevated `veyra-camera-setup.exe`, right after removing the `HKCU` keys (L95), calls `verifyDevice()` → `entry->BindToObject(...)` (L75, L96). Despite the comment in L1-L2 ("Never loads a payload DLL into this elevated process"), `BindToObject` loads the `InprocServer32` COM server through the merged `HKCR` into the elevated process. With split-token UAC, an unelevated process of the same user shares `HKCU` and can inject its own DLL in the window between L95 and L96. In addition, `VirtualCamera.cpp` L29 reads `VEYRA_TEST_VIRTUALCAM_MODULE_DIR` in production builds and passes the result to `ShellExecuteExW(..., L"runas")`.
- **Fix (25 min):** remove `BindToObject` from the elevated `CameraSetup.cpp` (check only the `HKLM` registry entries there; run the DirectShow `BindToObject` probe only in the unelevated parent), disable `VEYRA_TEST_VIRTUALCAM_MODULE_DIR` outside test builds and add `FILE_FLAG_OPEN_REPARSE_POINT` in `copySecure()`.
- **Status:** fixed. `BindToObject` removed; the elevated process checks that the HKLM `InprocServer32` paths name the files it installed. The environment override is removed (no test used it). `copySecure()` opens with `FILE_FLAG_OPEN_REPARSE_POINT` and rejects a reparse point or directory by handle. No bind probe was added to the parent; the existing `machineCurrent()` file comparison stays.

### A3. [MEDIUM] Always-on HTTP server on `127.0.0.1:18765` without a token, and no `protocol_whitelist` in `DemuxSession`
- **CWE:** CWE-918, CWE-306, CWE-20
- **Location:** `apps/veyra-qml/main.cpp` L886-L889, `src/ui/bridge/PlaybackServer.cpp` L19-L24, `src/media/DemuxSession.cpp` L393-L435, `src/media/FFmpegDemuxer.cpp` L139-L145
- **Cause:** `playbackServer.listen(18765)` always starts, with no UI switch and no authentication token. It accepts any `chrome-extension://[a-p]{32}` origin, and any local process that sends no `Origin` header, to force `openNetworkAddress(url)`. At the same time `DemuxSession::openContext()` and `FFmpegDemuxer::open()` (unlike `src/engine/Subtitles.cpp` L539) set no FFmpeg `protocol_whitelist` for nested HLS playlists and HTTP redirects.
- **Fix (20 min):** add an opt-in settings switch (off by default) for `PlaybackServer` and set a `protocol_whitelist` that matches the input scheme (`"http,https,tcp,tls,crypto"` for HTTP/HTTPS; `"rtsp,tcp,udp,rtp"` for RTSP; `"udp,rtp"` for UDP/RTP).
- **Status:** fixed. New switch Settings → Network → 浏览器扩展连接, **off by default** (a behaviour change: the extension falls back to `veyra://play` when the port is closed), applied live. `media::networkProtocolWhitelist()` is set at all three network open sites (`DemuxSession`, `FFmpegDemuxer`, `WasapiAudioSink`); the HTTP list also allows `httpproxy` and `data`, the RTSP list `rtsps`, `srtp`, `tls` and HTTP tunnelling.

### A4. [MEDIUM] No SHA-256 check when loading `AnrProvider`, global `SetDllDirectoryW`, unpinned `star-history.yml` workflow
- **CWE:** CWE-494, CWE-427, CWE-1395
- **Location:** `src/ngx/AnrProvider.cpp` L109-L121, `src/gfx/FsrSrBackend.cpp` L79-L87, `.github/workflows/star-history.yml` L12-L34
- **Cause:** the 2.2.1 ANR downloader (`src/ui/bridge/AnrDownload.cpp` L10-L42) checks SHA-256 when downloading, but `AnrProvider::open()` loads `%LOCALAPPDATA%\Veyra\components\anr\FG-V.B15\nvngx_dlssg.dll` without a hash check and with `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`. `FsrSrBackend` changes the process-wide DLL directory through `SetDllDirectoryW` (`MAX_PATH`) while other threads work (`CaptureGraphPreparation`). `star-history.yml` runs unpinned `actions/checkout@v7` and `kitswas/generate-star-history@v1` with `permissions: contents: write` and `git push` to `main`, although `README.md` (L115-L120) no longer uses `assets/star-history.svg`.
- **Fix (25 min):** verify `computeFileIdentity` before `LoadLibraryExW` in `AnrProvider::open()`, call `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)` in `main.cpp`, replace `SetDllDirectoryW` with `AddDllDirectory`/`RemoveDllDirectory`, and remove `star-history.yml` or pin it to full 40-character SHAs with `persist-credentials: false`.
- **Status:** workflow fixed (actions pinned to SHAs, `permissions: {}` at workflow level and `contents: write` only for the job, `persist-credentials: false`, explicit push URL; zizmor reports no findings). Not changed: the SHA-256 lock (AGENTS.md: the software must not reject user-replaced DLLs by fixed hash or signature) and the DLL search order (the FidelityFX loader finds its provider through the normal search order, which `AddDllDirectory` does not affect without a process-wide `SetDefaultDllDirectories`; that change would alter how NGX, Qt, FFmpeg and AMF load their dependencies and needs testing first). The `MAX_PATH` part is fixed: `DirectoryGuard` now saves the previous DLL directory at its real length, so a longer one is no longer restored from an unfilled buffer.

---

## B. D3D12 engine, GPU and new 2.2.1 pipeline paths (5 defects)

### B1. [HIGH] Race on writes to `SHADER_VISIBLE` descriptor heaps without a GPU queue drain, and `DescriptorStager` bypassed
- **Location:** `src/engine/EngineController.cpp` L2378-L2382, `src/pipeline/EnhanceGraph.cpp` L5076-L5101, `src/pipeline/ColorDetailPass.cpp` L39-L47, `src/gfx/RifeDmlBackend.cpp` L258-L266
- **Cause:** editing live parameters (`requested.revision == previous.revision`) calls `graph->applySettings()` without `ring.drainQueue()`, while `setNrEnabled()` rewrites in place the single `blitPass_.heap` or `tailPasses_` descriptors used by command lists still in flight on the GPU. In addition `ColorDetailPass::prepare()` (5× `makeSrv`) and the new 2.2.1 `RifeDmlBackend::initialize()` write SRVs straight into `SHADER_VISIBLE` heaps instead of `DescriptorStager::stageSrv`; according to the workaround documented in `include/veyra/pipeline/GpuPassUtils.h` (L267-L274), on an RTX 5070 that causes `DXGI_ERROR_DEVICE_REMOVED` on flip-model `Present`.
- **Fix (35 min):** add `ring.drainQueue()` before the descriptor update in `applySettings()` (or version descriptors per ring slot and skip the write when `wasEnabled == nrEnabled_`), and use `DescriptorStager::stageSrv` in `ColorDetailPass` and `RifeDmlBackend`.
- **Status:** fixed. `setNrEnabled()` returns early when NR did not change (the live colour-edit path); on a real NR toggle it drains the queue once and writes through `stager_`. `ColorDetailPass` and `RifeDmlBackend` (input SRV and three null SRVs) use a local `DescriptorStager`.

### B2. [HIGH] Races and lifetime errors in the capture-preview cold start (`CaptureGraphPreparation`, new in 2.2.1)
- **Location:** `src/engine/EngineController.cpp` L569-L574, L2219-L2228, L2698-L2852, `src/engine/CaptureGraphPreparation.cpp` L22-L51
- **Cause:** five problems in the path from commit `a321daea`:
  1. `CapturePreviewWindow` sets its size once in `open()` and never follows the main window during the 15–25 s model load or after a permanent fallback to the raw preview.
  2. Changing `audioSync`/`audioOffsetMs`/`captureFlipVertical` during the load breaks `startupRequest->settings == target` (L2772) and throws the prepared graph away, while changes to `captureAudio`/`captureBuffer` are lost (L2226).
  3. The `!prepared.presenter` branch in `promote()` (L2743) calls neither `ring.drainQueue()` nor `presenter->prepareGraphChange()`.
  4. The source-size change in L2719 checks `captureStarting ? nullptr : &coreCache` instead of `(captureStarting || capturePreparation.pending())`, so `coreCache` can be used concurrently with the `discard()` thread.
  5. `retiredCapturePreview` (declared inside `do{...}while(false)` at L1106) is destroyed when the loop exits, before `ring.drainQueue()`, `presenter->close()` and `graph->shutdown()` (L4199–4203), and `discard()` destroys the swapchain on a `std::async` thread.
- **Fix (60 min):** resize `CapturePreviewWindow` with the main window, compare only video fields when taking the graph, add `ring.drainQueue()` and `prepareGraphChange()` in `promote()`, guard `coreCache` with `capturePreparation.pending()`, move `retiredCapturePreview` above `presenter`/`graph` and release the swapchain on the engine thread.
- **Status:**
  1. Not a bug: `VideoPresenter::present()` already resizes the temporary surface with `SetWindowPos` when the main window's client size changes.
  2. Fixed: source-level settings (audio sync, offset, capture flip, capture audio, buffer mode) apply to the capture immediately during the load, are ignored when deciding whether the prepared graph is current, and are kept across promotion.
  3. Not a bug: `promote()` calls `openPreparedPresenter()` → `VideoPresenter::open()`, which itself calls `prepareGraphChange()` (reused chain) or `close()`.
  4. Fixed differently: with no model load in progress, a pending task is a cleanup worker, so the resize waits for it with `capturePreparation.finish()` before a graph borrows the core cache. (Passing `nullptr` would create a second, independent NGX core.)
  5. Fixed for teardown: `retiredCapturePreview` is declared outside the session block and released right after `ring.drainQueue()`, `presenter->close()` and `graph->shutdown()`. The `discard()` worker path is unchanged on purpose: it runs only after the preview window has been hidden (`rawSurfaceRevealed`), the swapchain is windowed, and releasing it on the engine thread could stall the cutover.

### B3. [HIGH] `StateTracker` out of sync in `LiveRecorder::Segment::discardPending()`
- **Location:** `src/engine/LiveRecorder.cpp` L625-L635, L673-L677
- **Cause:** `Segment::capture()` moves the `y` and `uv` states to `D3D12_RESOURCE_STATE_COPY_SOURCE`. When `~CaptureGuard` (`src/engine/VideoPresenter.cpp` L548) cancels the frame before the list is submitted, `discardPending()` does not restore the previous states (unlike `src/engine/VirtualCamera.cpp` L474-L478), so the next frame issues an invalid `COPY_SOURCE → UNORDERED_ACCESS` barrier for a resource that is in `COMMON`.
- **Fix (15 min):** remember the previous `y`/`uv` states before `capture()` and restore them in `discardPending()`.
- **Status:** fixed as described.

### B4. [MEDIUM] VRAM budget underestimated in `EnhanceGraph::initialize()` for `VideoDenoise` and `RIFE DirectML`
- **Location:** `src/pipeline/EnhanceGraph.cpp` L910-L1011, L523-L532, `src/gfx/RifeDmlBackend.cpp` L215-L218
- **Cause:** the `estimate` + `sdkReserve` formula leaves out the full-frame `RGBA16F` texture of `VideoDenoise` (~66 MiB at 4K, ~265 MiB at 8K) and the FP32 tensor buffers and ONNX DirectML session of `RifeDmlBackend` (>600 MiB of `planes`/`times`/`outputs` buffers alone at 4K 3× plus the DML session), so a graph over the WDDM budget can pass admission.
- **Fix (20 min):** add `videoDenoiseOutput_`, the tensor buffers and a `RifeDmlBackend` session reserve to `estimate`/`sdkReserve`.
- **Status:** fixed for the textures and tensors (source-extent RGBA16F denoise output; RIFE planes, times and outputs at the 64-aligned work extent plus the RGBA8 input copy). No session reserve was added: its size is not known without measurement.

### B5. [MEDIUM] `GpuTimer` slot left open on the first frame after a `RIFE` reset
- **Location:** `src/pipeline/EnhanceGraph.cpp` L4635-L4687, `include/veyra/diagnostics/GpuTimer.h` L40-L47
- **Cause:** with RIFE active, `EnhanceGraph` opens the `GpuStage::FgBatch` and `Fg1` markers (L4635) and skips `gpuTimer_.resolve()`/`submitted()` (L4644, L4647), then on `!rife_->hasPair()` hits `break;` (L4687) without closing the markers or reporting the fence (`pending_[active_].fence` stays `0`), losing that frame's GPU timings.
- **Fix (15 min):** condition the `FgBatch`/`Fg1` markers and the `if (!runFg)` branch on `(!rife_ || rife_->hasPair())`, or close and report `gpuTimer_` before `break;`.
- **Status:** fixed. When RIFE has no pair after `capture()`, the spans end on the main list, which then resolves and reports the timing.

---

## C. Engine state, sessions, presets, models and image export (5 defects)

### C1. [HIGH] `TiledImageProcessor` rejects large images for RTX 50 `NvidiaOriginal`, AMD `LmxxfAmd`, multi-layer NR, `Beauty` and `scRGB`
- **Location:** `src/engine/TiledImageProcessor.cpp` L63-L84, `src/engine/EngineControllerImage.cpp` L70-L71, `src/pipeline/EnhanceGraph.cpp` L4912-L4975
- **Cause:** 2.2.1 added the `videoDenoise` fields to `tileSettings` but left out `nrRuntime` (default `Ampere`), `nrTemporal`, `nrAntiFlicker`, `nrPolicy`, `nrLayers`/`nrLayerCount`, `hdrOutputMode` and `beauty*`. `graph.applySettings(tileSettings)` at L83 detects the mismatch against `desc_` and aborts the tiled export/preview.
- **Fix (20 min):** pass the source `EnhancementSettings` to `TiledImageProcessor::process()` or copy the missing fields from `desc` before `applySettings()`.
- **Status:** fixed by mirroring every field the tile graph's description carries (`nrRuntime`, `nrTemporal`, `nrAntiFlicker`, `nrMotion`, `hdrOutputMode`). Layers, NR policy and beauty are not described by the caller for tiles, so their defaults already match.

### C2. [HIGH] Cascading sidecars in `PresetLibrary` and `ChainSessionStore` exceed `MAX_PATH` (260 characters), and v17 `.auto-fg` is skipped
- **Location:** `src/engine/PresetLibrary.cpp` L180-L198, L290-L359, L625-L628, L1319-L1483
- **Cause:** migrations between versions append up to 20 extensions in a row (`chain-session.v1.p3-editor...fg-options.video-denoise.tmp` — the file name alone reaches **212 characters**, and over 290 with the user folder), then use `std::ofstream`, `CreateFileW` and `MoveFileExW` without the `\\?\` prefix (also approaching NTFS's hard limit of 255 characters per name component). `ChainSessionStore::save()` also skips version `17` (`.auto-fg`), and `PresetLibrary::applyToEditor()` (L625–627) loses `fgAutoMultiplier` and uses the flat `settings` instead of `values` for a `Chain` preset in Node mode.
- **Fix (40 min):** build the sidecar name from the base stem (`chain-session.v1.<newest-tag>`), support `\\?\` paths, handle v17 in `ChainSessionStore::save()`, and pass `values` and keep `fgAutoMultiplier` in `applyToEditor()`.
- **Status:** fixed without renaming the existing chain, which older builds rely on:
  - session v17 is saved to its own `.auto-fg` file, as library v22 is;
  - both loaders also find `.output-controls.auto-fg`, which a save could already produce but no load looked for;
  - when a chained name would pass 200 characters or `MAX_PATH`, the store continues in one short `<base>.compact` file, which later loads prefer and later saves reuse (new `testCompactSidecar`, skipped when the test's temporary folder is too deep);
  - the application manifest declares `longPathAware`;
  - `applyToEditor()` keeps the document's own FG backend and VFG quality (captured before the preset is applied) and `fgAutoMultiplier`.

### C3. [MEDIUM] The `doviFollowAverage` switch does not work when toggled live and is not saved to preferences
- **Location:** `src/pipeline/EnhanceGraph.cpp` L5012-L5014, `src/ui/bridge/Colour.cpp` L507-L509, `src/engine/VideoPresenter.cpp` L339
- **Cause:** `requiresGraphRebuild()` does not rebuild the graph on a `doviFollowAverage` change, and `EnhanceGraph::applySettings()` updates `desc_.doviFollow` but not `desc_.doviFollowAverage`, so `VideoPresenter` sees the old value. `setDoviFollowAverage()` also does not save the setting to `prefs` (unlike `setDoviFollow` and `setDoviP7Compose`).
- **Fix (15 min):** add `desc_.doviFollowAverage = s.doviFollowAverage;` in `EnhanceGraph::applySettings()` and save/restore `prefs["doviFollowAverage"]` in `Colour.cpp` and `QmlPlayerBridge.cpp`.
- **Status:** fixed as described.

### C4. [MEDIUM] `ModelAssets::remove()` destroys files and fails for packages with subfolders; no timeout for `tar.exe`
- **Location:** `src/ui/TranslationAssets.cpp` L350-L359, L428-L455
- **Cause:** `ModelAssets::remove()` deletes only the files in `asset.members` and leaves empty subfolders (e.g. `adapters/`, `kernels/`), so `QDir().rmdir(retired)` fails, the emptied folder is restored and the temporary `remove-XXXXXX` folder is left behind. `extractArchive()` also runs `tar.exe` without `--no-same-owner --no-same-permissions` and without a time limit.
- **Fix (20 min):** remove empty member subfolders, deepest first, before `rmdir(retired)`, and add a timeout timer and the `tar.exe` safety flags as in `apps/veyra-portable/UpdateWorker.cpp` L121-L125.
- **Status:** fixed as described (10-minute limit; the stage folder is also removed when the model folder is restored).

### C5. [LOW] `dynamicHudState`, `hudLockState` and `dlssgLiteState` missing from `sameSnapshot()`
- **Location:** `src/ui/PlayerUiFacade.cpp` L47-L79, `src/ui/bridge/Status.cpp` L465-L468
- **Cause:** `Status.cpp` shows warnings based on `dynamicHudState` (new in 2.2.1), `hudLockState` and `dlssgLiteState`, but `sameSnapshot()` does not compare them, delaying the UI refresh while paused or on a static frame.
- **Fix (5 min):** compare these three fields in `sameSnapshot()`.
- **Status:** fixed as described.

---

## D. Unicode/ANSI paths, UI/QML, crash diagnostics and documentation (5 defects)

### D1. [HIGH] Loading all 108 `.dxil` shaders through ANSI `CreateFileA` and `.string()` crashes the application on Unicode paths outside `CP_ACP`
- **Location:** `include/veyra/pipeline/GpuPassUtils.h` L109-L114, L226-L231, `src/gfx/RifeDmlBackend.cpp` L77, `src/pipeline/EnhanceGraph.cpp` L2031, `src/ngx/FgCompatibilitySession.cpp` L120
- **Cause:** `veyra_qml_ui.exe` has no `activeCodePage=UTF-8` in its Windows manifest (`apps/veyra-qml/main.cpp` L917-L923). In a portable folder with characters outside the current ANSI code page (e.g. CJK on Polish/English Windows, or Polish characters on Chinese Windows), `(runtime::shaderDirectory() / name).string()` throws an unhandled `std::system_error` in MSVC, or `CreateFileA` fails for all 108 `.dxil` shaders, and `.string()` in the `RifeDmlBackend`/`EnhanceGraph`/`XessPresenter`/`FgCompatibilitySession` logs aborts initialisation.
- **Fix (25 min):** replace `CreateFileA` + `.string()` in `GpuPassUtils.h` with `CreateFileW` + `.wstring()`, and log paths through a UTF-8 conversion (`WideCharToMultiByte(CP_UTF8, ...)`).
- **Status:** fixed. Both shader loaders use `CreateFileW`; a new `runtime::pathUtf8()` replaces about 35 throwing `path::string()` log calls across 19 files; the GPU DIS shaders are opened from a UTF-8 directory string (noted in `third_party/gpu-dis/VEYRA_INTEGRATION.md`). `activeCodePage=UTF-8` was not added: it would change how existing ANSI-encoded (e.g. GBK) data is read on Chinese systems.

### D2. [MEDIUM] `VSlider.qml` leaves `[from, to]` on arrow keys, and `NrStabilizer` differs between List and Node
- **Location:** `qml/Veyra/VSlider.qml` L37-L42, L83-L88, `qml/Veyra/ProPage.qml` L1731-L1752, `qml/Veyra/NodePage.qml` L1524-L1544
- **Cause:** `VSlider::nudge()` clamps the value before `Math.round(next / keyStep) * keyStep`, and `commitValue()` clamps only when `stepSize > 0`. For `list-hold-tolerance` (`0.005..0.10`) the left arrow rounds to `0` (`< from`), and in `qml/Veyra/LiveTranslationDialog.qml` L174-L199 (`to: 0.95`) a step of `1` jumps to `1.0` (`> to`). `ProPage.qml` also omits `valueFromModel: true` (breaking the binding after the first edit) and uses `0.005..0.10` where `NodePage.qml` uses `0..1`.
- **Fix (20 min):** always clamp the result in `commitValue()` to `[Math.min(from, to), Math.max(from, to)]`, add `valueFromModel: true` in `ProPage.qml` and unify the `NrStabilizer` tolerance range.
- **Status:** fixed (Node now uses the List range `0.005..0.10`).

### D3. [LOW] Deadlock risk in `crashFilter()`, `Rip` error in `EngineWatchdog`, heap contents in `.dmp`
- **Location:** `apps/veyra-qml/main.cpp` L493-L563, `src/engine/EngineWatchdog.cpp` L53-L68, `include/veyra/diagnostics/Redaction.h` L5-L12
- **Cause:** before `MiniDumpWriteDump`, `crashFilter()` allocates on the heap (`std::format`, `QString`), takes the `Logger` mutex and calls `.filename().string()` (ANSI); after heap corruption or a stack overflow this can stop the process before the minidump is written. `MiniDumpWithIndirectlyReferencedMemory` can put heap secrets into the `.dmp` (translator API keys, Xbox tokens), and `EngineWatchdog::unwindSuspended()` uses `context->Rip` instead of `context->Rip - 1` for return addresses (`count > 0`).
- **Fix (30 min):** run `MiniDumpWriteDump` (with `MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules`) first, without heap allocation; use `Rip - 1` for return frames in `EngineWatchdog`; add redaction of `Bearer`/`Cookie`/query tokens in `Redaction.h`.
- **Status:** fixed as described (static buffers, `_snwprintf_s` with `_TRUNCATE`; the dump keeps less memory for debugging; new redaction check in `RepairContractTests.cpp`).

### D4. [LOW] Untranslated message in `PresetLibrary.cpp` and unused DOM-XSS prototypes in `prototypes/`
- **Location:** `src/engine/PresetLibrary.cpp` L1005, `prototypes/ui-redesign-2026-09-25/board.js` L49-L67
- **Cause:** `PresetLibrary.cpp` L1005 returns the raw English text `"Invalid exclusion region mode"` among Chinese messages; the archived `prototypes/ui-redesign-2026-09-25/` folder triggers Opengrep errors (`innerHTML`).
- **Fix (10 min):** use a catalogued message in `PresetLibrary.cpp` L1005 and remove or exclude the historical `prototypes/` folder.
- **Status:** fixed. The message reuses the existing, already translated `预设库保护区域损坏`. `prototypes/` is excluded through `.semgrepignore`, not deleted: `tools/qt_probe/make-icons.py` builds `qml/Veyra/IconData.js` from its `icons.js`, which `THIRD_PARTY_NOTICES.md` also cites.

### D5. [LOW] Stale version metadata in `AGENTS.md` and `CURRENT_STATUS.md`, hard-coded drive `E:` in `prepare-rdna3-source.py`
- **Location:** `AGENTS.md` L3-L9, `docs/CURRENT_STATUS.md` L7-L45, `scripts/amd-nr/prepare-rdna3-source.py` L10
- **Cause:** `AGENTS.md` still describes the source as `2.2.0` and the public release as `2.1.9`; `CURRENT_STATUS.md` keeps old `2.2.0`/`2.1.9` headings; `prepare-rdna3-source.py` forces `relative_to('E:/项目/Veyra')`.
- **Fix (15 min):** update `AGENTS.md` and `docs/CURRENT_STATUS.md` to 2.2.1 and parameterise the base folder in `prepare-rdna3-source.py`.
- **Status:** fixed for `AGENTS.md` and the script (`--root`, default unchanged). `CURRENT_STATUS.md` already leads with 2.2.1; its 2.2.0 section is history and was left as is.

---

## Functional and hardware limitations still present in 2.2.1

1. **No HDR live recording or Instant Replay on AMD/Intel:** `src/engine/LiveRecorder.cpp` L1359 explicitly rejects `hdrMode != 0` without NVENC.
2. **Initial crosshair ghosts and dynamic HUD limits:** `docs/RELEASE_NOTES_2.2.1.md` L44 and `src/pipeline/HudProtect.cpp` L59-L67 — the first frames before `confidence_` converges can leave single crosshair ghosts; dynamic HUD needs a thin, steady frame.
3. **Node mode, RIFE and virtual camera limits:** Node mode supports neither offline export nor a global NR protection region (`README.md` L197); `RIFE DirectML` works only for SDR `RGBA8` `2×`/`3×` (`src/gfx/RifeDmlBackend.cpp` L194-L195); the virtual camera converts HDR to SDR `NV12` and skips frames generated by the XeSS presenter.
4. **No Authenticode signature on the installer and unverified hardware combinations:** the Inno Setup installer has no publisher signature (`docs/CURRENT_STATUS.md` L15); physical AMD RX 9000 `lmxxf` inference, long Xbox sessions, 4K60 NV12 on some cards and overlays other than RTSS 7.3.7 remain unverified on hardware.

---

## Verified as correct (false positives removed)

- **UAC re-entrancy in `VirtualCamera` (2.2.1 fix):** `src/engine/VirtualCamera.cpp` L192-L235 sets `installing = true` under the mutex before `ShellExecuteExW`.
- **Portable updater Ed25519 and `VEYRA_UPDATE_TEST_PORT`:** `CMakeLists.txt` L1369-L1395, `PortableUpdateManifest.cpp`, `PortableUpdate.cpp`, `UpdateWorker.cpp` — signature verified before parsing, downgrade blocked (`*to <= *from`), bounded redirects, `IAttachmentExecute`, file lock and `inspectExtractedTree`.
- **Quality and i18n scanners:** `scripts/i18n/extract.py --check` (3325 entries, 0 missing, 0 placeholder errors), `ruff` (0 syntax/bug findings), `actionlint` (4/4 workflows pass), `trivy` IaC (0 findings), `FullscreenFrameStats.hlsl` (`glyphs[3]` correctly holds 13 characters in line 2).
- **Cryptography and remote sessions:** DPAPI (`CRYPTPROTECT_UI_FORBIDDEN`) + `SecureZeroMemory` in PS5/Moonlight/Xbox, X.509 certificate pinning in Moonlight, `user` role isolation and loopback-port PID check for `llama-server` in `LiveTranslation`.

---

## Remediation plan (priority order, about 7 h 20 min in total)

| Package | Scope | Key files | Estimate |
| --- | --- | --- | --- |
| **P0** | Camera IPC security, UAC COM hijack and CI | `shared-memory-queue.c`, `CameraSetup.cpp`, `VirtualCamera.cpp`, `star-history.yml` | 75 min |
| **P1** | Unicode shader paths and D3D12 races | `GpuPassUtils.h`, `ColorDetailPass.cpp`, `RifeDmlBackend.cpp`, `EnhanceGraph.cpp`, `LiveRecorder.cpp` | 90 min |
| **P2** | Capture cold start (`CaptureGraphPreparation`) | `EngineController.cpp`, `CaptureGraphPreparation.cpp` | 60 min |
| **P3** | `TiledImageProcessor`, `MAX_PATH` sidecars, `doviFollowAverage`, `ModelAssets::remove` | `TiledImageProcessor.cpp`, `PresetLibrary.cpp`, `Colour.cpp`, `TranslationAssets.cpp` | 95 min |
| **P4** | `VSlider`/QML, `PlaybackServer`/`DemuxSession`, crash handler, documentation | `VSlider.qml`, `ProPage.qml`, `DemuxSession.cpp`, `main.cpp`, `EngineWatchdog.cpp`, `AGENTS.md` | 120 min |
