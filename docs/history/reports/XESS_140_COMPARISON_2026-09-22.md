# XeSS 4X: 1.4.0 / 1.4.3 / current comparison

## Scope and conditions

User requested testing the downloaded 1.4.0 package. No product edits, build,
package, runtime replacement or release were performed. This is a bounded
XeSS 4X comparison, not DLSS, live capture or physical screen acceptance.

Host: local RTX 5070. Each case ran sequentially for 30 seconds, fullscreen,
with VSync off and a verified 2560x1440 XeSS swapchain after startup.
NR uses the realtime 1920x1080 internal extent, NOT native 4K NR.

- `native-nr-xess4`: p001.mp4 3840x2160/60 input, NR, no SR, XeSS 4X.
- `sr-nr-xess4`: derived1080 1920x1080/60 input, RTX Video SR quality 3
  to 3840x2160, realtime NR, XeSS 4X.
- Enhancement output is 3840x2160 in both cases; final swapchain is 2560x1440.
- Smoke mode supplies explicit test settings and bypasses normal preference
  load/save; no manual user preference edits were made.

Executables / SHA256:

| Version | Executable | SHA256 |
| --- | --- | --- |
| 1.4.0 | E:/App/Veyra-1.4.0-win64-portable/Veyra.exe | 261fa42005e81c685da2ff3aa6472d6c07bd7d5b29fbead6ab64187b7dfbf89c |
| 1.4.3 | E:/项目/Veyra/releases/1.4.3/final/Veyra-1.4.3-win64-portable/Veyra.exe | f2a1e407d2fe136c32771bcf25068a6f595d51cc6b4f15766964b6a358b80676 |
| current | E:/项目/Veyra/tests/bounded-repair-20260922/app/veyra.exe | 29c0e9a6693ec46a44f6f362999ce8583535e2be96323608f2957ab3854b8139 |

The loaded NR, XeSS FG and XeLL files have identical hashes across all three:

- nvngx_dlssnr.dll: E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E
- libxess_fg.dll: EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27
- libxell.dll: D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7

This checks file identity for comparison, not a new distribution/signature audit.

## Results

Retain log timestamps 10 through 29 seconds after the first log record.
Source rate is delta presentId / elapsed seconds between retained XeSS counter
records; generated rate is delta generatedTotal over the SAME interval.
Periodic log granularity makes the actual interval differ slightly by case.
The sum is SDK-accounted throughput, NOT measured screen refresh or proof of
uniform per-frame presentation. GPU is device-wide NVML average over harness
elapsed seconds 10..29, not per-engine occupancy or exact per-frame workload.

| Case | Version | Counter interval (s) | Source/s | Generated/s | Sum/s | Suppress/resume events in window | GPU mean | Power mean (W) |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: |
| native | 1.4.0 | 18.002 | 59.993 | 179.980 | 239.973 | 0/0 | 93.62% | 216.63 |
| native | 1.4.3 | 17.990 | 60.033 | 169.261 | 229.294 | 2/1 | 90.45% | 216.63 |
| native | current | 18.018 | 59.940 | 179.820 | 239.760 | 0/0 | 93.24% | 222.82 |
| SR+NR | 1.4.0 | 17.217 | 59.244 | 32.933 | 92.177 | 13/12 | 80.74% | 215.13 |
| SR+NR | 1.4.3 | 17.411 | 58.584 | 36.184 | 94.768 | 13/13 | 80.82% | 215.14 |
| SR+NR | current | 18.308 | 39.327 | 88.486 | 127.813 | 0/0 | 72.02% | 185.76 |

All six processes exited 0 and reported failed=false. This means the runs
completed, not that they achieved the target quality/cadence. 1.4.0/1.4.3 do
not emit the requested newer trace file; missing traces are not zero jitter.
No screen capture/scanout analysis or blinded visual comparison was performed.

## Interpretation

1. 1.4.0 can sustain approximately 60 source + 180 generated/s in the tested
   no-SR case. Current is similar in average throughput; uniform screen cadence
   has not been established. The single fullscreen 1.4.3 run had two suppression
   events, so do not replace its observed 229/s with an assumed 240/s.
2. 1.4.0 is NOT free of the heavy-load XeSS problem. With SR, its gate repeatedly
   disables/restores FG while preserving nearly 60 source submissions/s.
   1.4.3 behaves similarly here. Neither sustains 240/s.
3. Current increases generated throughput but reduces source submissions to
   about 39/s. Relative to 1.4.0 this is about one third fewer source submissions
   per second. This is the concrete meaning of worse source continuity in this
   comparison. It does not mean the decoder itself necessarily runs at 39/s.
4. Greater generated throughput alone is not sufficient evidence of improved
   motion quality. Missing source samples and uneven delivery can coexist with
   a higher counter. These measurements cannot determine which version looks
   better on every scene or prove that lower GPU utilization is a hardware limit.
5. No old suppression gate was restored and no new scheduling experiment was
   attempted. Do not label this result a successful FG repair or extrapolate
   it to DLSS, RTX30/40/5090, capture or all input/output resolutions.

## Evidence and reproducibility

Final evidence root:
`E:/项目/Veyra/tests/fg-140-compare-actual-fullscreen-20260922/`.
1.4.0 cases are directly underneath; other versions are in `143/` and `current/`.
Each case retains argv and executable hash in result.json, app/stdout/stderr
logs and telemetry.json. Temporary directory:
`E:/项目/Veyra/tmp/fg-140-compare-20260922/`.

Harness: scripts/acceptance/fg-utilization-matrix.py. Arguments for each version:

```text
--exe <executable from table>
--native E:/项目/Likely7 个人账号/Deepseek Grok/p001.mp4
--derived E:/项目/Veyra/tests/nr-fg-followup-20260921/p001-derived1080.mp4
--output <version evidence directory>
--temp E:/项目/Veyra/tmp/fg-140-compare-20260922
--cases native-nr-xess4 sr-nr-xess4 --seconds 30
```

The harness hardcodes pro view. For the final runs, Python `runpy.run_path`
invoked it with an in-process subprocess.Popen wrapper that changes the argument
after `--smoke-view` to `fullscreen` before launching. No test script or product
file was edited. The mutated argument list is recorded in each result.json.
1.4.0 log confirms resize from 1280x712 to 2560x1440; later builds initialize
2560x1440 directly. Sampling excludes startup/resize.

Earlier exploratory evidence is retained but excluded from the final table:

- `E:/项目/Veyra/tests/fg-140-compare-20260922/`: pro-mode initial run.
- `E:/项目/Veyra/tests/fg-140-compare-fullscreen-20260922/`: mistakenly still
  pro mode despite the directory name; its result.json records pro. This was
  caught and corrected by the final actual-fullscreen runs above.

In pro mode 1.4.0 shrinks its swapchain to 770x494 while newer builds retain
monitor-sized buffers. Those results must not be presented as matched pixel
workloads. This is a comparison confound and not permission to undo the newer
resize/lifetime fixes without regression testing.
