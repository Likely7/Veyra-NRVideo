# Independent bidirectional candidate review

Fresh-context read-only `review_bidirectional_candidate`, scoped diff vs3fc3e22. Final **PASS**, no remaining introduced P0/P1/P2. Default FORWARD, product graph/UI/export integrity unchanged. No natural NR/FG quality or full Goal claim.

Initial review: independent preflight71/phase7 75 exit0, delivery `logs/delivery/d63406d71034438c9b461502da3ffb73/result.json`; test evidence `logs/optimization-goal-20260908/review-bidir-53ef67d5a404489c84050b408ea57879/`. Actual1080p/4K four combinations each pass/debug0. Both direction EPE .0441942px. Novel wrong acceptance6330→12 and25992→44; correct background101392/432992 unchanged. Occlusion warmed fixed-pair throughput .829→1.506ms and2.973→5.475ms, not latency/P95. Tracked fingerprint unchanged `d2e87a8fd1e12f1179e766014df1d439edf29e29`.

Initial verdict FAIL: P2 in diagnostic readback offsets for640×129 (gridheight33). Per-footprint offsets25344/59136 were256mod512. Maker corrected independent512-byte alignment and final allocation extent; this did not alter standard-size layouts or product behavior.

Final independent640×129: all four cases exit0/debug0, EPE .0441942px. Novel wrong acceptance191→73, correct background1411 unchanged. Evidence `logs/optimization-goal-20260908/review-bidir-fix-e6fbc793d4824054b7465f6361075361/`. Final phase7 75 exit0: `logs/delivery/aea3d88148bf4e6b9c793cd99f95f7fb/result.json`. Tracked fingerprint unchanged `2d3745d722cd9931e12d9ec6200d14d4d66f9924`. Original P2 closed.

Normal register/unregister→resource-release→destroy ran on GPU. Driver registration-failure rollback was inspected, not fault-injected. CPU consistency diagnostic does not prove production GPU consistency or NR/FG benefits on changing/natural pairs. Same fixed-pair batch mean is not latency/P95. No additional source-frame lookahead introduced. Physical capture, long stability and distribution remain outside scope.
