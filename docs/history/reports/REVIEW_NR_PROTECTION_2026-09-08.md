# Manual NR protection review — 2026-09-08

review_protection: new-context read-only review of cycles64–65. Initial verdict FAIL for P2 stale protection checkbox/count with dirty numeric drafts or unchanged revision while master enhancement off. Fixed dedicated syncProtection; numeric drafts are not repopulated. Final scoped PASS, no remaining introduced P0/P1/P2.

Independent GPU matrix image-aaab094e151b4904b989eb43cdf8242c19.563s: Feature18 Create0x1 non-null SEH0, NR4, changed191323channels, protected/outsideerror0; full-protected2561square nine tiles/nineNR evaluates error0. Preset roundtrip/legacy upgrade/invalid/corrupt tests PASS. No new normal playback/video-export pixel readback found.

Final independent preflight PASS; phase7 logs/delivery/a178db7f0e0e4a4a8555d0b66e764f0a/result.json41.477s,75checksPASS. Finalapp SHA2560654C61F11D9FC50436B964F385E4028E6458C4B9D186B6DCD5B0796C383B4F5. UI logs/optimization-goal-20260908/protection-final-review-a2044089e105466f91c8857a306187a2/app.log exit0,9secondsmoke/25secondwatchdog: actualbuttons/zoom2rectangle/visibleoverlay/sessionassertions PASS; numeric0.42draft survives selection/clear/Esc; master-off same-revision indicators pass; NR4failedfalse.

Initial reviewer setup failures (wrong input path, hidden root) are retained in ignored logs; correct visible launch passed. Final tracked fingerprint9af8dfb05978f33ef832e1c557e0a76e2d9e77a4 unchanged. Untracked overlay initially independently reviewed hash390809D9DEF189952B86B8235C6D7005283AF77BC535DAAD3ED4211C3A321713. GPU/preset code did not change in UI correction and was not redundantly rerun.

Scope: manual NR change suppression only. No visual-design screenshot acceptance; feathered/partial-tile/natural/moving quality, SR/FG protection and privateUI resources still unproved. Q6/Q7 and physical capture remain open. This is not full Q5 or Goal approval.

## Cycle 66 - protection boundary diagnostics (2026-09-08)

Scoped test-only change in tests/integration/ImageDimensionTests.cpp: feather 2/32 pixels, disjoint regions, and partial 2561x2561 mask spanning central horizontal/vertical tile seams. No product code, export integrity or runtime changed.

Commands: scripts/build.ps1 -Root "$PWD" -Preset x64-release; scripts/loop-gate.ps1 -Gate preflight; logs/optimization-goal-20260908/run-image-tests.ps1 (275s watchdog); scripts/loop-gate.ps1 -Gate phase7. Build log build-protection-boundaries-final.log. Initial runs image-eeba5f9f5a934480ab91d3307fbdc74a (17.620s) and image-97deff3da67c4a3e94c731e561377964 (17.654s) failed a new test assertion: unsigned 192-x subtraction misclassified exterior pixels. Diagnostic per-mode evidence isolated the assertion; changed to 192.f-x, no product adjustment.

Maker image-262e170a7cef4014b6287f6c4c022f22 passed 21.828s. Independent review_protection_boundaries PASS with no introduced P0/P1/P2: preflight71; phase7 logs/delivery/d8846bbfe5d54b41b7b14327e8897975/result.json 41.695s/75checks; image-e8c7b9377cd94f68bd50fb189bd8b49c 21.916s. Actual Feature18 Create0x1 non-null SEH0; seven protection evaluations with original/baseline error0. Feather2/32 has 1581/15725 mixed channels, envelope error0. Partial nine-tile/nine-NR interior/exterior error0, envelope error1. Baseline comes from same run, dimensions verified.

Image-test SHA256 052F7DE99BB622DDF05F977D5431C8257F26330119CF7760072126C3DC655481. App remains 0654C61F11D9FC50436B964F385E4028E6458C4B9D186B6DCD5B0796C383B4F5. Reviewer tracked fingerprint f80219622037f7671d65046e14313267f9bb85ce unchanged.

Limits: these assertions prove endpoints and bounded nontrivial blending, not exact smoothstep or perceptual smoothness. Central seams covered, not every seam with a protected region. Moving HUD/temporal behavior, natural quality, SR/FG protection and private resource effectiveness remain unverified. Phase7 and full Goal remain in_progress. Next atomic task: moving-background/static-HUD protection matrix, then isolated optional NR resource contract; Q6 motion confidence/FG and Q7 candidate ROI remain pending.
