# Window resize crash repair (2026-09-20)

Scope: user reported a freeze/crash while holding and dragging a window corner
in the 1.4.3 test build. Preserve retained frame-generation repairs and timing.

## Evidence and repair

The old executable crashed during actual bottom-right mouse resizing with
capture active and all effects off. PID 22100 dump reports 0xc000041d at
__chkstk+0x37 with exhausted UI-thread stack. User PID 408 dump has the same
stack exhaustion pattern. Raw stack scans are supporting evidence, not a
complete formal unwind trace.

PE x64 unwind metadata establishes callback stack allocations:

| Callback | Old bytes | Fixed bytes |
| --- | ---: | ---: |
| AppShell proc | 162360 | 280 |
| SettingsWindow proc | 98216 | 2536 |

Win32 modal sizing, layout and painting reenter window callbacks. Previously
each invocation reserved large storage even for small messages. Extract heavy
creation, command, timer and related handlers into noinline helpers, retaining
message bodies, return/break semantics and dispatch order. Do not increase the
thread stack to conceal excessive callback allocations. Rendering, GPU fences,
NR, FG multipliers and latency policy are unchanged.

Changed product files: apps/veyra/ui/AppShell.cpp and
apps/veyra/SettingsWindow.cpp. Regression tools: ui-native-resize.py and
ui-stack-budget.py under scripts/acceptance.

## Verification

- Release 1.4.3 build: passed; UI contract and presentation worker tests passed.
- Stack budget check: fixed build passes; old executable fails as expected.
- Actual mouse bottom-right shrink and top-left drag/snap with NV12 capture:
  video continues, UI responds, clean Alt+F4 shutdown.
- Native Windows modal sizing: four grow/shrink cycles each for capture,
  NR + DLSS 6X and NR + XeSS 4X: passed with continuing presentation.
- Synthetic DPI 96/144/192/reset cycles: passed. Only one physical monitor was
  connected; this does not certify real cross-monitor transitions.
- Baseline synthetic resizing alone did not reproduce the actual mouse crash;
  actual mouse evidence and compiled stack budget are required regressions.
- Initial DLSS test result reader failed on non-UTF8 diagnostic bytes after
  successful app exit. Reader corrected to tolerate those bytes; rerun passed.

Build command: scripts/build-isolated.ps1 -Root . -BuildDirectory
E:/项目/Veyra/build/slider-reset-20260919 -DependencyCache
E:/项目/Veyra/build/slider-reset-20260919/CMakeCache.txt -TempDirectory
E:/项目/Veyra/tmp/fg-cadence-repair-20260920 -Targets
veyra,veyra_presentation_worker_tests,veyra_ui_contract_tests -DisplayVersion 1.4.3.
Linker /MAP enabled in the local cache for stack-budget inspection.

Evidence: E:/项目/Veyra/tests/resize-hang-20260920/ (fix-build.log,
fixed-mouse-capture.log, fixed-native-capture, fixed-native-dlss6-rerun,
fixed-native-xess4, fixed-dpi). OS crash dumps remain in the Windows-managed
C:/Users/123/AppData/Local/CrashDumps directory; no global WER settings changed.

Local package command: scripts/package-portable.ps1 -Root . -Version 1.4.3
-Label '-test' -BuildDirectory E:/项目/Veyra/build/slider-reset-20260919
-OutputDirectory E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix.
Process TEMP/TMP: E:/项目/Veyra/tmp/fg-cadence-repair-20260920.

Coverage is RTX5070 on this machine, not all users/devices. Fixed 6X pacing
gaps remain unresolved. No push or public release.

## Package verification

Portable smoke: all seven cases passed using visible-scene.mp4, CaseSeconds 7,
including real NR/SR/FG and app-local module checks. Logs/result:
E:/项目/Veyra/tests/resize-hang-20260920/package-smoke/.
ZIP contents: all 120 payload entries verified against package manifest sizes
and SHA256, plus the manifest itself; publisher audit forbiddenFiles=0.
Unchanged runtime identities and patched FFmpeg retained.

Packaged executable native modal resize with NR + SR + DLSS 6X passed all four
grow/shrink cycles (1280x800 to 1544x1064 and back), exit 0; evidence in
package-native-resize/result.json. Packaged --smoke-ui --smoke-seconds 12
passed F11/Esc, Alt+Enter/double-click and remaining UI checks, exit 0
(package-ui.log).

Additional --smoke-repair-ui check failed at
NR_off_click_works_with_invalid_draft on both the fixed build and old delivered
package with identical NR-on/SR-off/FG-off input. Logs fixed-repair-ui.log and
baseline-repair-ui.log preserve this existing failure. It is not counted as a
pass and is outside the resize-only repair; the invalid draft condition needs
separate investigation. Actual NR-disable request and graph application are
present in the fixed log.

ZIP: E:/项目/Veyra/test-packages/1.4.3-20260920-resize-fix/Veyra-1.4.3-test-win64-portable.zip

Size: 472349977 bytes.
SHA256: ABA2DA87D1A308629BE1CD347401DF266212D98BC9A5E904E078512AE6FADD19.

## Follow-up regression review

Reviewed at 2b99e89 after the user requested attribution to earlier fixes.
No additional product changes or package replacement in this review.

### Attribution limits

The immediate cause is established by the crash dumps, actual mouse repro,
and compiled stack allocations above. The failing reproduction had all
effects disabled. It therefore does not require active DLSS/XeSS work.
Large settings/snapshot locals and many handlers shared one reentrant window
procedure. Nested synchronous layout/paint messages reserved that large
frame repeatedly. Total GPU load is not an explanation for this stack fault.

History review covered 412a19f (row reset controls), 7761f34 (cross-monitor
handling), 8c9957b (backend switching) and 0050870 (FG overload status).
7761f34 added interactive-move/DPI flags and an exit layout call; the large
callback structure predates it. These changes can affect the message path,
but this is not proof that 7761f34 introduced the first crashing version.
No historical binary bisection or historical callback recompilation was
completed. Do not name a particular FG optimization as the proven origin.

### Additional findings

- SettingsWindow.cpp:860-873,1669: syncProtection writes edit 222 even while
  it is focused, and the timer invokes it outside populate's notification
  guard. Clearing the feather field to enter a replacement can be overwritten
  by the next refresh. Programmatic text changes can also reenter liveField.
  The offending feather synchronization dates to c6f94433 (2026-09-17),
  before the resize extraction. Code-path finding; no dedicated interactive
  reproduction was performed in this follow-up.
- SettingsWindow.cpp:1136-1143: row reset enabled state compares desired
  settings only, ignoring unsubmitted edit text. At backend defaults an
  invalid edit leaves the reset button disabled, so it cannot clear that
  draft. Introduced with row resets in 412a19f. Code-path finding, not a new
  resize-repair regression; dedicated interactive coverage remains needed.
- The existing NR_off_click_works_with_invalid_draft integration assertion
  fails in both old and repaired packages. It combines NR state, checkbox
  state and draft preservation in one assertion. Successful NR-disable graph
  logs do not prove all three predicates. The test also edits an unfocused
  control; isolate those predicates before claiming normal typing is broken.
  This failure remains open and must not be counted as a passed repair gate.

### Broader callback stack inspection

Read the current veyra.map and PE unwind records using the existing
ui-stack-budget.py parser via Python runpy, including other proc/subclass
symbols. Largest observed callback was ScreenCapturePanel proc at 27240
bytes; AppShell interaction 9960, labelPaint 8472, live_status proc 7656,
screen previewProc 6968. Main/inspector remain 280/2536 bytes. No inspected
callback exceeded 32768 bytes. This is a per-function check, not proof of
bounded aggregate call depth or absence of recursion. Historical maps were
not available for equivalent old-version comparisons.

Scope is UI regression review around resize, settings synchronization and
recent window changes, not certification of the entire application. Real
multi-monitor transitions and the separate fixed-6X cadence issue remain
outside this pass. No runtime files were changed or added to Git.

## Follow-up implementation: settings drafts and display transitions

The preceding review findings are historical; this follow-up implements them.
SettingsWindow now tracks numeric drafts per control. Timer updates preserve
unfinished/rejected text; row resets remain enabled for drafts, retain them
when submission fails, and clear only their own field after acceptance.
Feather synchronization guards programmatic notifications. Integration tests
cover NR, feather and colour drafts, timer refreshes and rejected resets.

AppShell queues a coalesced DPI font/layout refresh after SetWindowPos returns.
WM_SIZE skips redundant layout while that refresh is pending; the existing
presentation transition flag remains active until refresh completion. This
hardens nested UI work but is not proof of the user's physical cross-screen
failure cause. Only one monitor (2560x1440) is connected here.

Validation using build/slider-reset-20260919 under E:/项目/Veyra:

- scripts/build-isolated.ps1, targets veyra, veyra_slider_reset_tests,
  veyra_ui_contract_tests, DisplayVersion 1.4.3: passed.
- veyra_slider_reset_tests: passed new draft/reset checks.
- veyra_ui_contract_tests: passed 384 layout cases, persistence and PCM checks.
- ui-stack-budget.py: callbacks remain 280/2536 bytes.
- --smoke-repair-ui with visible-scene.mp4, --nr --no-sr --no-fg,
  --smoke-seconds 25: passed, 288 frames, failed=false. The historical
  NR_off_click_works_with_invalid_draft failure is now resolved in this run.
- ui-display-transition.py with p001.mp4, --nr --sr and DLSS6 / XeSS4:
  passed native-message plus synthetic 144/192/96 DPI cycles and continued
  presentation. physicalCrossMonitorTested=false in both result files.

Evidence: E:/项目/Veyra/tests/resize-hang-20260920/followup-*. Initial DPI
reader incorrectly demanded a one-second present-cost sample between two
adjacent completion logs; corrected to inspect each move cycle after its
final DPI completion, rerun passed. Initial evidence retained. These are
software checks, not optical latency or physical dual-monitor acceptance.

Final follow-up package verification:

- ui-native-resize.py with NR/SR/XeSS4: four native modal sizing cycles
  1280x800 <-> 1544x1064 passed, normal exit (followup-native-xess).
- package-portable.ps1 -Version 1.4.3 -Label '-test' with the build above
  and OutputDirectory E:/项目/Veyra/test-packages/1.4.3-20260920-display-fix:
  passed runtime audit, forbiddenFiles=0.
- portable-smoke.ps1 -CaseSeconds 7, InputFile
  E:/项目/Veyra/tests/1.4.2beta/visible-scene.mp4: seven cases passed;
  result in followup-portable-smoke/result.json beneath the evidence root.
- All 120 ZIP payload sizes/SHA256 match package-manifest.json. Packaged EXE
  equals tested build: F2A1E407D2FE136C32771BCF25068A6F595D51CC6B4F15766964B6A358B80676.

New ZIP: E:/项目/Veyra/test-packages/1.4.3-20260920-display-fix/Veyra-1.4.3-test-win64-portable.zip
Size: 472352589 bytes.
SHA256: C3A40A6A1A6C076243E5DAE38380AC81C13372C7A261EBC40E1DDD9989EAE3BE.
Prior resize-fix package retained as a comparison/rollback artifact.
No push or public release performed.
