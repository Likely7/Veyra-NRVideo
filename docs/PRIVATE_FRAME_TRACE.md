# Private frame trace

Set `VEYRA_PRIVATE_FRAME_TRACE_FILE` to a new absolute CSV path before launching
Veyra. Set `VEYRA_PRIVATE_FRAME_TRACE_MS` to an integer from 250 to 120000
milliseconds. The default is 30000. The first event starts the single capture
interval for the process. The timer continues when playback pauses or stops.
A clean earlier exit writes a shorter trace. If no event was recorded, the
header has zero records and zero start and end stamps.

The writer retains up to 262144 fixed-size events. The `dropped` counter records
events that exceeded capacity. It writes a sibling `.partial` file and renames
it to the requested path. Both creation and the final rename refuse to overwrite
an existing file. A valid trace has the `veyra-frame-trace/1` header and the
`# complete=1` footer. GPU work near either end can have incomplete observations.

| Event | Observation |
| --- | --- |
| `Source` | Engine input that reached the graph. `stage` names the source kind. `detail` holds frame flags and `count` identifies a cached replay. `host` records the end of the engine read scope. |
| `SourceSkipped` | Engine policies such as deduplication, half rate, seek discard or expired preview candidates. Earlier ingress drops are counted by the source metrics. |
| `Submitted` | Graph submission wall time in `ms`, skipped FG candidates in `detail`, evaluated FG candidates in `count`. |
| `Cpu` | Named wall-time samples. Stage spans can overlap. Idle time excluded by the engine's pause accounting stays excluded. `DeadlineWait` can include `DisplayCapacityWait`. |
| `Gpu`, `GpuUnavailable` | Stage timing and query validity. Compare the endpoints of each sample using its own queue frequency. |
| `Ready`, `FrameReady` | CPU observation of readiness. `Ready` carries invalid count in `detail` and valid count in `count`. `FrameReady` names the subframe. GPU work can finish before the observation. |
| `Present` | Successful application submission. `presentBeginHost` and `presentEndHost` bracket the application path. `presentCallHost` marks the DXGI or provider handoff when available. |
| `Discarded`, `Cancelled` | Scheduler discard and batch cancellation. Read `stage` for the individual reason and `count` for the remaining frames in a cancelled batch. |
| `Reset` | Lifecycle transition. `detail` and `count` contain the reason and outcome enums. |
| `XessSleep`, `XessBind`, `XessPresent` | The XeSS provider's host cycle. `preparationCycle` can differ from `providerCycle`. |
| `ProviderOutput`, `ProviderDeadline` | Calls observed by the existing XeSS hooks. Deadline events use the separate deadline-trace switch. |

Host timestamps use one monotonic clock at 10 million ticks per second. A zero
timestamp field represents a missing observation. Sort by timestamp for the
timeline. Use `(session, revision, epoch, source)` to join source work and add
`(batch, detail)` to identify subframes. `sourceA` identifies the previous
endpoint of a continuous A/B pair. Use separate display instrumentation for
display timing. The trace stores only categorical labels, numbers and clocks.

The source-only tool `tools/frame_analyzer/veyra_frame_analyzer.py` reads the
CSV locally with Python 3.10 or later and produces HTML, JSON and a frame table.
Use a fresh output directory.

```powershell
py -3.11 -B tools/frame_analyzer/veyra_frame_analyzer.py D:/tests/frames.csv --output-dir D:/tests/analysis
```
