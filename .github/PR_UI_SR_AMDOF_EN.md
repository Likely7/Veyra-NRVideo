# PR 2: ui — migrate unavailable saved SR to FSR; enable AMD optical flow for FG

## Why

Two cross-cutting defects, independent of the Xbox transport:

1. **SR gray-lock on non-NVIDIA GPUs.** The SR card's availability resolves to
   the *saved* algorithm. A profile saved on an NVIDIA card (DLSS SR / RTX Video
   SR) greys the whole SR card on an AMD/Intel card and disables the algorithm
   dropdown, so the user can never pick the FSR upscaler that this GPU *does*
   support. Dead lock.
2. **Frame generation with optical flow disabled itself.** `VEYRA_HAS_AMD_OF`
   was unset because the FidelityFX SDK 1.1.4 optical-flow libs were absent, so
   `AmdOpticalFlow::initialize` returned false, the whole enhance graph failed
   init, and backend recovery turned FG off. Zero-motion worked only because it
   never creates an optical-flow session. Affected FSR and XeSS FG alike.

## What changed

- `src/ui/bridge/Hardware.cpp`: at load, if the saved SR algorithm is
  unavailable on this GPU and the cross-vendor FSR upscaler runtime is present,
  migrate `videoSrQuality` to FSR (mirrors the existing FG-backend migration).
- `qml/Veyra/EffectSupport.js`: `status(api,"sr")` now reads
  `api.videoSrQuality` explicitly so picking FSR re-evaluates the card switch
  even though the availability call is an arbitrary invokable.
- Build: compile `ffx_opticalflow_x64` / `ffx_backend_dx12_x64` from FidelityFX
  SDK 1.1.4 with `/MT` (matches Veyra's static CRT) into the existing
  `VEYRA_FIDELITYFX_ROOT` layout; `VEYRA_HAS_AMD_OF` now defined. No CMake
  variable or manifest change.
- ui-check snippets: `sr-ungray`, `sr-non-nvidia`, `fg-optical-flow`,
  `fg-amd-flow`.

## Scope / dependencies

- No `vcpkg.json` / `.gitignore` / `VEYRA_*_ROOT` change. The optical-flow libs
  are built from the AMD MIT SDK already referenced by upstream
  (`third_party_local/amd/FidelityFX-SDK`, gitignored, not committed).
- SR behaviour change applies to all sources (file/capture/stream), hence a
  separate PR from the Xbox transport work.

## Verification

- `sr-ungray.qml`: force saved DLSS (quality 0) on a non-NVIDIA GPU, then pick
  FSR → `switchAvailable` goes gray→true without restart; SR enables.
- `fg-amd-flow.qml`: force `setOpticalFlowChoice(1)` (I-card path) + FSR FG +
  optical flow → real console streaming with `amd-of Create result=0`,
  `Dispatch result=0`, `fg=1 motion=flow` stays enabled.
- `fg-optical-flow.qml`: N-card NVOF path likewise stays enabled.

## Not covered

- No change to SR/FG quality presets or export; optical-flow quality on Arc
  still needs per-device A/B.