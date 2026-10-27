# DLSS MFG recovery repair, 2026-09-20

## Scope and evidence

Baseline: `c8a3828`; checkpoint: `checkpoint/pre-dlss-recovery-20260920`;
branch: `codex/dlss-recovery-20260920`. No release or push.

The running 1.4.3 package on RTX 5070 accepts 2560x1440 NV12 at about
59.94 Hz and processes DLSS 6X at 3840x2160. In a 10.123-second sample:
607 captured, 583 accepted, 560 FG candidates rejected before evaluation,
2355 evaluations, 680 warmup evaluations, 1491 generated presentations,
184 generated frames expired. Presentation submissions are not scanout FPS.

A rejected pair invalidates FG history. Previously, the next admitted 6X
pair issued five reset evaluations and discarded all five outputs. This
amplifies the cost of recovery. It does not prove sufficient steady GPU
throughput for every 4K 6X workload. Independent stage P95s must not be summed.

## Implementation

- Seed reset history using one complete evaluation group with count/index 1.
  The next steady pair uses the selected multiplier. Do not leave a partial
  five-call group, skip temporal reset, or present reset outputs.
- Pass the actual graph reset state into admission. Use measured whole-warmup
  cost for warmup only; when unavailable, retain the steady estimate. Do not
  divide a 6X measurement by five or relax the deadline.
- Record `fgSkippedForReset` separately from rejected work and executed warmup.
  Candidate accounting is evaluated + admission-skipped + reset-skipped.
- Preserve generated-resource fences even when warmup outputs are not shown.
- Unknown readiness logs as -1, instead of subtracting a deadline from zero.

The SDK exposes count/index per evaluation rather than feature creation.
This supports the proposed group change but is not runtime acceptance evidence.

## Verification

CPU regression uses the observed deadline and predicted cost to distinguish
cheap measured recovery from steady 6X work. Integration coverage extends to
6X, checks reset accounting and valid generated outputs immediately following
startup and two rejection bursts, and checks D3D12 debug errors.

Build passed for the application, scheduler unit tests, admission tests,
sustained tests and live presentation tests. The last two were compiled only.
Scheduler tests: 105 PASS, exit 0.

On RTX 5070, driver 32.0.16.1656, 1080p test media with NR and DLSS:

| Multiplier | Real / NR | Rejected candidates | Evaluations | Reset-skipped | Valid generated | Debug errors |
| --- | --- | --- | --- | --- | --- | --- |
| 2X | 40 / 40 | 8 | 32 | 0 | 28 | 0 |
| 4X | 40 / 40 | 24 | 88 | 8 | 84 | 0 |
| 6X | 40 / 40 | 40 | 144 | 16 | 140 | 0 |

All exit 0. These final tests include an explicit midstream history reset,
two rejection bursts, next-frame validity/PTS checks, nonblack generated
pixel checks after recovery, and debug-layer inspection. Readback is test-only.
The user's application remained running, so these are correctness results,
not timing or throughput measurements. No capture device was opened.

Matched-workload physical capture testing is recorded below. It did not
eliminate rejection or reach the 4K 6X target.
RTX 30/40 and image-quality comparisons are not covered by this test.

Build: `E:/项目/Veyra/build/slider-reset-20260919` (existing isolated build).
Logs: `E:/项目/Veyra/logs/dlss-recovery-*-20260920.log`.
Tests: `E:/项目/Veyra/tests/dlss-recovery-20260920`, final2/4/6 stdout and
engine logs; `scheduler.log`. TEMP/TMP: `E:/项目/Veyra/tmp/dlss-recovery-20260920`.
Runtime identities and binary files are unchanged. The user's portable
package remains the prior build; no new package or publication was made.

## Physical capture comparison after user closed the application

The user authorized testing the previous settings. Matched original log
revision 18, not the final effects-off revision: `capture:0:14:0:0`, NV12
2560x1440 nominal 60 Hz (measured about 59.94), DLSS SR to 3840x2160,
original NR at realtime 1920x1080, NVOF Performance, DLSS 6X at 4K.
Order SR -> NR -> residual -> FG. HDR/pacing/VSync off, tearing on,
1795x816 presentation client, three swapchain buffers. GPU RTX 5070.

Baseline product source is detached `c8a3828` at
`E:/项目/Veyra/worktrees/dlss-recovery-baseline-20260920`; only the identical
sustained harness was copied there. Build at
`E:/项目/Veyra/build/dlss-recovery-baseline-20260920`, with the same approved
runtime junction and six FFmpeg/dav1d DLLs as the repaired build.
Baseline executable SHA256:
`1564C11D28DA94AE8DFC6A769F382202BB628ACBF144CA53C652C591A5777C1E`.
Repaired executable SHA256:
`3B992A76563029F7FEC53361CB35273784B7F5EC4D39E0578E6ED543CF4685E8`.

Harness profile `live-1440p` sets the above options and disables the normal
injected 55 ms stall. Run with `scripts/run-short-test.ps1`, arguments
`capture:0:14:0:0 OUTPUT 6 on 120 on 0 live-1440p`, timeout 175 seconds.
Both runs exited 0, sequentially with no concurrent build. A third diagnostic
used admission `off`, duration 60, timeout 115. This changes only the test
option; product admission remains enabled. Minimum ratio 0 means no throughput
assertion: the harness's `throughput=1` is NOT acceptance of the 360 Hz target.

Rates below are cumulative counter deltas over t=22..119 (97 seconds), or
t=22..59 (37 seconds) for the diagnostic. They are software submissions,
not physical scanout or HDMI-to-eye latency. Live content was not a repeated
controlled recording, so small differences cannot establish a performance gain.

| Measurement | Baseline | Repaired | Repaired, admission off |
| --- | ---: | ---: | ---: |
| Received input / s | 59.94 | 59.94 | 59.92 |
| Accepted input / s | 58.01 | 57.54 | 49.86 |
| Real + generated presentations / s | 202.57 | 204.10 | 184.76 |
| Rejected generated candidates / s | 57.94 | 54.12 | 0 |
| Executed warmup calls / s | 67.58 | 13.22 | 10.05 |
| Generated frames expired / s | 19.98 | 20.94 | 64.32 |
| Capture mailbox overwrites / s | 1.93 | 2.40 | 10.05 |
| Final rolling capture-age P95, ms | 32.91 | 33.23 | 35.05 |
| Final rolling scheduling-wait P95, ms | 9.02 | 9.65 | 15.02 |

Conclusion: single-call recovery removes repeated invalid work, but does not
solve the main sustained limitation for these settings (throughput delta
only +0.76%). Disabling admission worsened accepted input and effective
output, despite more generated candidates. Do not remove deadlines as a fix.
The five-frame FG GPU span was around 10 ms in this workload; this span can
include submission/dependency gaps, and does not prove pure compute saturation.
Further work must isolate GPU execution from command-slot/owner-thread stalls
and presentation phase, while retaining real source cadence and bounded queues.
No claim of stable 6X, reduced physical latency, or universal hardware coverage.

Evidence prefixes under `E:/项目/Veyra/tests/dlss-recovery-20260920`:
`live-baseline-clean`, `live-fixed-clean`, `live-fixed-no-admission`; each has
stdout/stderr and a directory with engine.log. Initial `live-fixed` overlapped
baseline compilation and is excluded from the comparison. Initial baseline
startup failed because PresentBlit shaders were absent; explicitly building
`veyra_shader_present_blit` resolved it. The root CMake engine dependency now
includes that target so standalone engine harness builds produce the shaders.
One shell invocation had a misspelled timeout argument and never launched.
No user's application was killed or overwritten; no package was produced.
