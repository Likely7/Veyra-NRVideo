# NR / XeSS bounded review result

Follow-up correction: the historical 41.71/43.35 ms below is repeatedly sampled
media-clock deviation after Present, not added filter or screen latency.
New per-Present measurements, natural-video probes, true-SR limits and the
official VFX creation failure are recorded in
[the follow-up acceptance](NR_FG_FOLLOWUP_ACCEPTANCE_2026-09-21.md).

Branch: codex/playback-nr-20260920. Starting checkpoint:
checkpoint/pre-nr-quality-perf-20260921 (bcdbfbf).
This closes the bounded local investigation, not universal picture-quality
or hardware acceptance. No publication or release package was requested here.

## Decisions for items 1 / 2 / 3 / 4 / 6

1. Temporal NR: fix valid-zero residual decay, protection/feather exclusion and
   invalid source intervals. Keep default off and the existing 80 ms history
   constant. A groupshared halo eliminates repeated neighborhood observations;
   no added presentation wait, reduced resolution or changed FG multiplier.
2. Independent denoise: not implemented. Magpie uses NvVFX VideoSuperRes,
   NvCV and CUDA, not direct NGX's VSR quality enum. Required runtime/model
   identity and GPU-only interop are not available in the configured Veyra
   dependency set. Upstream per-frame synchronization/HDR CPU readback cannot
   be copied into the real-time graph. Do not advertise the NR rename or the
   temporal filter as delivery of this separate denoiser.
3. Product shader tests: D3D12 17x13 FP16 tests cover reset, zero residual,
   invalid time, motion rejection, protected/feathered history and signed HDR.
   Added patterned frames with fractional motion and full-frame fingerprints.
4. Guidance audit: no demonstrated frame-identity/lifetime defect requiring a
   patch. Existing leases, epochs, fences and current-to-previous direction
   remain. Synthetic motion tests do not prove all NVOF fast-motion quality.
6. XeSS: keep 1.4.3-equivalent provider pacing. Previously speculative timing
   feedback is already withdrawn (dd69e1f). Do not add a nine-sample source-time
   estimator until the caller carries that source timestamp contract. No
   synchronous duplicate detector or default AMD flow was added.

## Temporal experiment

RTX 5070, p001.mp4, NR on, SR off, XeSS 4X, 30 seconds each. Same diagnostic
app, only NrTemporal.dxil changed for the shader comparison. Previous shader
comparison uses the current host constants: this isolates shader cost, not a
complete historical app comparison. All five enabled runs exit 0.

| Shader / run | Source frames | Generated | Final source FPS | Late P95 ms | Residual GPU P95 ms | Skips* |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original shader | 1555 | 4476 | 58 | 45.96 | 2.056 | 62 |
| Corrected, before optimization | 1578 | 4503 | 58 | 46.48 | 2.014 | 67 |
| Tiled | 1619 | 4848 | 60 | 41.71 | 1.369 | 1 |
| Tiled confirmation | 1642 | 4887 | 60 | 43.35 | 1.042 | 1 |
| Before optimization confirmation | 1576 | 4485 | 57 | 46.00 | 2.023 | 71 |

*Skips and GPU P95 use the final player-timing window; source/generated use
the shutdown smoke summary. They have different boundaries. Final FPS is not
the whole-run average. None is physical scanout or screen-to-eye latency.

The tiled shader matches the corrected non-tiled shader's full-frame FP16
fingerprints on all 41 test frames. Both test runs pass and report zero D3D12
debug errors. This proves equality on this corpus only. Natural-video
flicker/detail/ghosting has NOT passed subjective acceptance. The historical
off smoke P95 was 1.65 ms, but its comparison to 41.71 ms did not isolate added
filter latency. The follow-up's corrected per-Present ABBA measurements
supersede that interpretation. Actual SR plus temporal still has a measured
performance penalty; do not claim the feature is free. No further tuning.

## Release comparison and limits

See NR_PERFORMANCE_POWER_REVIEW_2026-09-21.md and
MAGPIE_NR_XESS_COMPARISON_2026-09-21.md. Short ABBA runs do not demonstrate a
blanket 1.4.3 regression: XeSS power +2.1%, DLSS -3.4% in trimmed samples.
Thermal state/content positions are not controlled and SR was off. DLSS 1.4.3
had far fewer expired generated frames/slot waits. Do not revert that fix
because of power alone. Existing fixed-6X uniform-presentation limits remain.

## Reproduction / artifacts

E:/项目/Veyra/tests/nr-quality-perf-20260921:
- compare-temporal.ps1 (baseline,current then tiled then tiled,current confirm)
- temporal-isolate-*.log and temporal-gpu-{current,tiled,accepted}.log
- NrTemporal-{baseline,current,tiled}.dxil and baseline HLSL evidence
- Existing release comparison script, application logs and power CSVs

Build: E:/项目/Veyra/build/playback-nr-20260920, MSVC vcvars64 + CMake
--build <build> --target veyra_nr_temporal_gpu_tests veyra -j 4.
DXC: -T cs_6_0 -E main -O3 -Qstrip_debug -Qstrip_reflect.
During A/B a baseline shader was copied over the build output; the last
incremental build therefore correctly reported no work by timestamp. The
final shader was explicitly recompiled from source, hash verified and tested.
Final SHA256: DE74C9611D2032AD07C28E4F98F84E14D6521BEF297414F163F07DBF13C84A4C.

app/Veyra.exe is diagnostic staging, with the final shader restored. Its
inherited package manifest is stale: NOT a redistributable release package.
No DLL/model added to Git; runtimes unchanged. No main merge or push.
User acceptance still needs real viewing (especially moving fine detail),
NR+SR combinations, real capture, and GPUs other than the tested RTX 5070.
