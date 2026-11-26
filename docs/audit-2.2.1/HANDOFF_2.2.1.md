# Handoff — Veyra 2.2.1 audit remediation

Findings and per-item status: [AUDIT_2.2.1.md](AUDIT_2.2.1.md).

## 1. Goal and scope

Fix the defects found by a static audit of Veyra 2.2.1 (base commit `bc59d457`) with minimal-boundary changes that follow `AGENTS.md`: no protocol, configuration-format or default-graph changes, except the browser-extension port (see section 5).

## 2. Status

- **Code complete, not compiled, not tested.** The machine that made the changes had no MSVC, Qt or project dependencies, so every change was checked by reading only.
- **Fixed:** A1, A2, A3, A4 (workflow part and the `MAX_PATH` restore in `FsrSrBackend`), B1, B2 (items 2, 4, 5), B3, B4 (textures and tensors), B5, C1, C2, C3, C4, C5, D1, D2, D3, D4, D5.
- **Not bugs after checking the code (no change):** B2.1 (`VideoPresenter::present()` already resizes the temporary surface), B2.3 (`VideoPresenter::open()` already calls `prepareGraphChange()` or `close()`).
- **Deliberately not changed:**
  - A4 SHA-256 lock in `AnrProvider`: `AGENTS.md` says the software must not reject user-replaced DLLs by fixed hash or signature.
  - A4 `SetDllDirectoryW` → `AddDllDirectory` / process-wide `SetDefaultDllDirectories`: the FidelityFX loader finds its provider through the normal search order, and a process-wide change would alter how NGX, Qt, FFmpeg, ONNX Runtime and AMF resolve dependencies. Needs testing on hardware first.
  - A1 separate read-only mapping and explicit DACL (protocol v3): all consumers run as the same user, so a DACL cannot separate producer from consumer, and v3 would require every user to reinstall the camera component. Memory safety is fixed by the snapshot.
  - B4 DirectML session reserve: size unknown without measurement.
  - D1 `activeCodePage=UTF-8`: would change how existing ANSI-encoded (e.g. GBK) data is read on Chinese systems.
  - Deleting `prototypes/`: `tools/qt_probe/make-icons.py` builds `qml/Veyra/IconData.js` from its `icons.js`; excluded from Semgrep/Opengrep through `.semgrepignore` instead.

## 3. Stack

Windows 11 x64, C++20, D3D12, Qt 6.8.3 QML, CMake/Ninja, MSVC 2022. Build roots and dependency locations as described in `AGENTS.md`.

## 4. Changed files

- **Security (A1–A4):**
  - `third_party/obs_virtualcam/ipc/shared-memory-queue.c` — size, interval and slot offsets snapshotted at create/open; copies and the scaler check use the snapshot.
  - `tests/unit/VirtualCameraTransportTests.cpp` — `hostile()`: a process rewrites the shared header; readers and the writer stay inside their buffers.
  - `third_party/obs_virtualcam/ipc/tiny-nv12-scale.c`, `third_party/obs_virtualcam/libdshowcapture/source/output-filter.cpp` — 64-bit sizes and offsets; sample buffer sized from the consumer's format with limits and checked against the allocator and each sample.
  - `src/gfx/FsrSrBackend.cpp` — previous DLL directory saved at its real length.
  - `src/virtualcam/CameraSetup.cpp` — no `BindToObject` in the elevated process; HKLM `InprocServer32` paths checked; reparse-safe source open.
  - `src/engine/VirtualCamera.cpp` — `VEYRA_TEST_VIRTUALCAM_MODULE_DIR` removed.
  - `include/veyra/ui/PlaybackServer.h`, `src/ui/bridge/PlaybackServer.cpp`, `apps/veyra-qml/main.cpp`, `src/ui/bridge/Preferences.cpp`, `qml/Veyra/SettingsPage.qml`, `i18n/catalog.json`, `tools/chrome-extension/README.md` — opt-in extension port (`browserExtensionBridge`, default off, live).
  - `include/veyra/media/NetworkInput.h` + `src/media/DemuxSession.cpp`, `src/media/FFmpegDemuxer.cpp`, `src/sink/WasapiAudioSink.cpp` — per-scheme `protocol_whitelist`.
  - `.github/workflows/star-history.yml` — SHA-pinned actions, job-level write permission, no persisted credentials.
- **D3D12 / pipeline (B1, B3–B5, D1):**
  - `include/veyra/RuntimePaths.h`, `src/base/RuntimePaths.cpp` — `runtime::pathUtf8()`.
  - `include/veyra/pipeline/GpuPassUtils.h` — `CreateFileW` shader loading.
  - 19 files — throwing `path::string()` log calls replaced with `pathUtf8()`.
  - `src/guidance/GpuDisOpticalFlow.cpp`, `third_party/gpu-dis/src/gpu/native_strict_dis_provider.cpp`, `third_party/gpu-dis/VEYRA_INTEGRATION.md` — UTF-8 DIS shader directory.
  - `src/pipeline/EnhanceGraph.cpp` — `setNrEnabled()` (B1), VRAM estimate (B4), GPU timer (B5), `doviFollowAverage` (C3).
  - `src/pipeline/ColorDetailPass.cpp`, `src/gfx/RifeDmlBackend.cpp` — SRVs through `DescriptorStager`.
  - `src/engine/LiveRecorder.cpp` — `discardPending()` restores `y`/`uv` states.
- **Capture cold start (B2):** `src/engine/EngineController.cpp` — source-level settings applied live during the model load and kept across promotion; the prepared-graph comparison ignores them; resize waits for a cleanup worker; retired preview released after the teardown drain.
- **Engine, presets, models (C1–C5):**
  - `src/engine/TiledImageProcessor.cpp` — tile settings mirror the graph description.
  - `src/engine/PresetLibrary.cpp`, `include/veyra/engine/PresetLibrary.h`, `tests/unit/PresetLibraryTests.cpp` — session v17 `.auto-fg`, `.output-controls.auto-fg` lookup, `.compact` sidecar (`testCompactSidecar`), `applyToEditor()` FG keep, catalogued error message.
  - `apps/veyra/veyra.manifest` — `longPathAware`.
  - `src/ui/bridge/Colour.cpp`, `src/ui/QmlPlayerBridge.cpp` — `doviFollowAverage` preference.
  - `src/ui/TranslationAssets.cpp` — subfolder cleanup, stage-folder cleanup, `tar.exe` flags and 10-minute limit.
  - `src/ui/PlayerUiFacade.cpp` — three status fields compared.
- **UI, diagnostics, docs (D2–D5):**
  - `qml/Veyra/VSlider.qml`, `qml/Veyra/ProPage.qml`, `qml/Veyra/NodePage.qml` — clamping, model binding, unified tolerance range.
  - `apps/veyra-qml/main.cpp` — minidump written first, without heap allocation or referenced memory.
  - `src/engine/EngineWatchdog.cpp` — `Rip - 1` for return-address lookups.
  - `include/veyra/diagnostics/Redaction.h`, `tests/unit/RepairContractTests.cpp` — credential redaction and its check.
  - `AGENTS.md`, `scripts/amd-nr/prepare-rdna3-source.py`, `.semgrepignore`.

## 5. Decisions and trade-offs

- **Extension port off by default.** A behaviour change for existing extension users; they enable it once in Settings → Network. When it is off, the extension already falls back to `veyra://play`. Drop this part if the default should stay.
- **Smaller minidumps.** `MiniDumpWithIndirectlyReferencedMemory` was replaced by thread, handle and unloaded-module information, so dumps users send do not carry keys or tokens. Field debugging gets less memory; revert the flag in `apps/veyra-qml/main.cpp` if needed.
- **NR bypass toggle drains the queue once** before its views change, the same CPU wait the NR-preparation handover already uses. Live colour edits no longer rewrite any descriptor.
- **Camera component update.** The camera module sources changed (A1), so a new build produces new camera DLLs; the app will report the installed component as outdated until the user updates it. The queue protocol is still v2.
- **Sidecar compatibility.** The existing chained names are unchanged so older builds keep reading their files; only names that would exceed the limits move to `.compact`.

## 6. Checks run

- `zizmor` on `star-history.yml`: no findings. `actionlint`: pass.
- `scripts/i18n/extract.py --check`: 3327 entries, 0 missing in 9 languages, 0 placeholder problems.
- Bracket balance, encoding, line endings and NUL-byte check over every changed file: pass.
- `gitleaks` on the staged changes: no leaks.

## 7. Not verified

- Nothing was compiled; no CTest run; no GPU or device test.
- New tests not yet run: `VirtualCameraTransportTests::hostile`, the redaction check in `RepairContractTests`, `PresetLibraryTests::testCompactSidecar`.

## 8. Blockers

A build machine with MSVC 2022, Qt 6.8.3 and the project dependencies, plus an NVIDIA RTX GPU and a capture device for the runtime checks.

## 9. Security notes

No secrets or personal data in these changes. The virtual-camera queue protocol stays v2, so installed camera components remain compatible.

## 10. Next step and acceptance criteria

- **Next step:** build this branch.
- **Accept when:**
  1. The build passes.
  2. `veyra_virtualcam_transport_tests` (EXCLUDE_FROM_ALL; build it explicitly), `veyra_repair_contract_tests` and the preset library tests pass, and the rest of CTest is unchanged.
  3. Manual checks pass: capture cold start while editing audio settings, live NR edits and an NR toggle on an RTX 50 GPU, the first RIFE frame after a reset, a large image tiled with a non-default NR runtime, and the browser extension with the new switch on and off.
