# Same-pair bidirectional flow experiment

Cycle74, base3fc3e22. Phase7 optimization remains in progress. The shared `NvOfSession` now accepts optional reverse flow/cost textures together. Defaults are null and preserve forward-only behavior. No UI, EnhanceGraph default, export integrity, model, lookahead or shader was changed.

## Contract and actual test

Local NVIDIA Optical Flow SDK5.0.7 D3D12 exposes `NV_OF_PRED_DIRECTION_BOTH`, `bwdOutputBuffer` and `bwdOutputCostBuffer`. With current B as input and previous A as reference, primary vectors are current→previous and reverse vectors previous→current. Both use the same pair; no C frame is required. Reverse R16G16_SINT/R8_UINT textures must match hardware-grid extent, be single-slice/mip/sample, and not alias primary outputs. Partial reverse pairs reject before disturbing a live session. Registration rollback and unregister/release/destroy include reverse handles in reverse order. Runtime remains System32 NVOF.

`veyra_bidirectional_flow_tests [width height]` uses the product session, real GPU output readback only in this diagnostic, and known translated nonperiodic textures. It validates +8/-8 source-pixel directions, wrong-format/alias rejection, preservation on partial-pair rejection, actual output, and teardown/debug errors. Novel constant foreground exists only in current B: its interior has no previous correspondence. The CPU diagnostic compares existing cost/bounds/photometric acceptance with an additional reverse-cost and nearest-grid round-trip error <1.5px test. This is not a production shader or perceptual-quality oracle.

Build command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root "$PWD" -Preset x64-release` (`build-cycle74-contract.log`, exit0).

Final maker matrix: `logs/optimization-goal-20260908/bidir-matrix-e47ef77e21b44baaa4eace596d6080cd/`. Runner `logs/optimization-goal-20260908/run-bidir.ps1` executes360p/1080p/4K separately with275-second watchdogs. Each invokes four cases, all exit0/debug0. NVOF init/execute/unregister statuses0. Direction median endpoint error .0442px for both directions. Tests read InfoQueue after unregister/resource release/shutdown. Initial and previous matrices retained, no hidden failures.

| Source | Novel-region grid points | Accepted by base test | Still accepted with consistency | Correct-background accepted before/after |
|---|---:|---:|---:|---:|
| 640×360 | 1,274 | 644 | 4 | 8,338 / 8,338 |
| 1920×1080 | 13,416 | 6,330 | 12 | 101,392 / 101,392 |
| 3840×2160 | 55,616 | 25,992 | 44 | 432,992 / 432,992 |

Measurements are warmed throughput: four warmup calls, then60 repeated evaluations of the **same fixed pair**, final GPU fence completion included. CPU+GPU elapsed divided by60; not single-frame latency, GPU timestamp duration or P95. Input upload/readback excluded. No temporal hints. The forward and BOTH harness cases allocate the same textures; the reverse output allocation below is an incremental requirement if integrated, and excludes internal driver allocations.

| Source | Forward mean ms/pair (plain/occluded) | BOTH mean ms/pair | Reverse output texture allocation |
|---|---:|---:|---:|
| 640×360 | .229 / .265 | .349 / .367 | 131,072 bytes |
| 1920×1080 | .825 / .836 | 1.497 / 1.510 | 786,432 bytes |
| 3840×2160 | 3.073 / 2.974 | 5.703 / 5.571 | 2,818,048 bytes |

## Decision and limits

The hypothesis is supported for this constructed occlusion: consistency rejects many wrong correspondences that luma/cost checks retain. Cost is material at4K (about2.6ms extra in this experiment). Keep the capability as an opt-in candidate, **do not enable it by default or claim reduced NR/FG artifacts yet**. No product path requests reverse outputs, so these measurements do not prove changed user-visible quality. Before adoption, test changing/natural frame pairs, unequal motions and repeated/isoluminant textures, GPU consistency sampling, then actual NR/FG output against baseline. Reduced/zero motion alone does not prove safe exclusion of model history.

Independent gate/review is recorded separately after execution. Physical capture, long stability, natural reconstruction and Q7 remain outside this scoped experiment.

Reviewer first verdict FAIL identified a diagnostic-only P2: non-512-aligned cost readback offsets for accepted odd grid heights (640x129). Product/default paths and standard-size evidence unaffected. Fixed per-footprint placement alignment and total buffer extent. Maker actual640x129 all fourcases exit0/debug0, bidir-odd-428ef6956ced4584808b6ccd2e95da90; medianEPE.0442, novel base191->73, correctbackground1411unchanged. The weaker small-height occlusion rejection reinforces that benefits are case-specific. Final reviewer revalidation pending; initial verdict retained.
