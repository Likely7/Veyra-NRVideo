# Release power comparison: bounded evidence

Evidence directory: `E:/项目/Veyra/tests/nr-quality-perf-20260921`.
Runner: `compare-releases.ps1`. Sequential ABBA release runs on RTX 5070,
same p001.mp4, NR enabled, SR disabled. This is not a native-4K NR or
NR-plus-SR performance acceptance. Package identity verification was recorded
before these runs; no new run was performed for this analysis.

Power files have no header: timestamp, power.draw, utilization.gpu,
clocks.gr, temperature.gpu. Analysis uses PowerShell Import-Csv with these
explicit headers and Measure-Object -Average. Samples are approximately
900 ms apart plus query overhead. Trimmed values discard the first six and
last four samples. This reduces startup/exit contamination but does NOT
prove steady state, matched content positions or thermal equilibrium.

| Backend/run | Samples | Whole-run W | Trimmed W | Trimmed GPU % | Temperature C |
| --- | ---: | ---: | ---: | ---: | ---: |
| XeSS 4X / 1.4.2-1 | 48 | 199.87 | 216.45 | 89.13 | 70.42 |
| XeSS 4X / 1.4.3-2 | 48 | 205.49 | 220.55 | 90.08 | 73.87 |
| XeSS 4X / 1.4.3-3 | 48 | 207.70 | 221.41 | 90.05 | 70.89 |
| XeSS 4X / 1.4.2-4 | 48 | 203.80 | 216.58 | 87.58 | 74.18 |
| DLSS 6X / 1.4.2-1 | 32 | 209.06 | 236.18 | 93.64 | 69.27 |
| DLSS 6X / 1.4.3-2 | 32 | 211.11 | 230.08 | 91.09 | 72.32 |
| DLSS 6X / 1.4.3-3 | 32 | 209.54 | 231.89 | 91.82 | 72.23 |
| DLSS 6X / 1.4.2-4 | 32 | 219.12 | 241.82 | 94.14 | 75.18 |

The paired trimmed means are XeSS 216.52 versus 220.98 W (+2.1%) and
DLSS 239.00 versus 230.99 W (-3.4%). Neither establishes an intrinsic
per-frame cost change. Clock means span approximately 2820-2831 MHz;
temperature is not controlled. These short samples cannot diagnose
individual long-frame power transients.

The earlier corresponding logs show comparable XeSS source/generated counts
and lateness, while 1.4.3 DLSS submits substantially more generated frames
with far fewer expirations and slot waits. Thus this experiment does not
support a blanket 1.4.3 performance regression or justify reverting its
presentation fixes to reduce power. Submission counts are not physical
scanout counts; no screen latency or visual smoothness claim follows.

## Outstanding acceptance

The new NR temporal changes have synthetic GPU-pass coverage but still need
real-video on/off execution and natural-image quality review. Independent
VFX denoise prerequisites and guidance/XeSS audit conclusions must be
consolidated into the final report. No release, final acceptance or shutdown
has been performed.

## Real-video temporal smoke: performance acceptance failed

Current built application and shaders were staged in the evidence directory's
`app/`, with unchanged runtime dependencies copied from the previous corrective
audit staging. This is diagnostic staging, not a validated distributable
package (the inherited package manifest does not describe its changed files).
Runs used p001.mp4, `--smoke-seconds 30 --smoke-view pro --nr --no-sr
--fg-xess --fg-multiplier 4`, adding `--nr-temporal` only for the second run.
Process-local TEMP/TMP used the plan's tmp directory. Each run had a 75-second
watchdog; both exited 0 and logged failed=false. No process remains running.

| Mode | Source frames at smoke summary | Generated | Lateness P95 ms | Last source FPS |
| --- | ---: | ---: | ---: | ---: |
| Off | 1635 | 4866 | 1.65 | 60 |
| On | 1561 | 4425 | 46.54 | 58 |

The on run logged temporal=true, NR evaluations and successful graph creation.
Its final player-timing sample has previewSkipped=80 and residual GPU P95
2.057 ms, versus approximately 0.24 ms residual processing without temporal.
Source epochs increased to 76 versus 1. Exit 0 therefore establishes only
bounded lifecycle success, NOT acceptable cadence or image quality.
Evidence: `temporal-video-{off,on}.{log,stdout.log,stderr.log}`.

Do not commit/promote the temporal implementation as accepted. Next compare
the pre-existing temporal implementation against this correction under the
same enabled setting, then perform one bounded optimization or withdraw the
regressing change. On/off alone does not establish which new edit caused
the cost: the previous version already had a temporal pass. Natural-image
visual acceptance is still outstanding.

The accepted continuation optimization is the groupshared 10x10 halo in
`shaders/NrTemporal.hlsl`. It removes duplicate nine-tap work without changing
output fingerprints. Product D3D12 regression and a 30-second p001.mp4
temporal-on smoke passed; the execution plan records the measured cadence and
the limit that this is not a subjective natural-video or physical-display
latency acceptance. No power claim is made from this short run.
