# Veyra 1.4.1 release execution

## Authorization and scope

2026-09-18: user confirmed the latest test build works ("测试好了都可用") and explicitly authorized merging all post-1.4.0 repairs into main and publishing 1.4.1 to Likely7/Veyra-NRVideo. RTX30/40 and sustained FG limitation are the release focus. This user acceptance supersedes the pending target-card status in earlier repair records; historical test logs remain unchanged. It does not imply a complete model/driver test matrix.

## Integration

- Baseline main: `bd7cc0f`; public 1.4.0: `890d200`.
- Preserved main checkpoint: `checkpoint/pre-release-1.4.1-main-20260918`.
- Repair snapshot: `89f4f7f`, also tagged `checkpoint/post140-tested-r4-20260918`; merged `codex/post140-field-repair-20260917` with full history.
- Merged `codex/avermedia-51-switch-20260917` commit `c566dda` for device-tree name, initialization delay and vendor logs. Its separate worktree's uncommitted diagnostic probe is preserved there; it is not a product fix or a release dependency.
- Color, capture-decode and MKV/export repair branches were already ancestors of main. Old source-archive and Smooth Motion experiment branches are not post-1.4.0 repairs and remain separate.
- Runtime identities unchanged. Package script now includes RTX40MFG MIT and HDE BSD notices; archive audit checks all 11 shipped enhancement components, including AMD.

## Verification

Hardware: RTX5070 / driver 616.56. User target-card acceptance is recorded separately above.

- `cmd.exe /c out\build\3060-incremental.cmd`: exit0, 218 build steps; `out/logs/release-1.4.1-build.log`. Uses VS2022 BuildTools vcvars64 and CMake build of `out/build/scheduling-audit-20260918`, parallel 6. Cache retains RemotePlay ON, experimental NR ON, DLSS SDK 310.7.0, NVENC headers n13.0.19.0 and patched FFmpeg/dav1d prefix.
- `scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/scheduling-audit-20260918`: PASS, 74.8993521 seconds, 26 checks. Result: `logs/delivery/ccccf48711ca4544835b226163872bd3/result.json`. Real NR/NVOF, native4K correctness, playback/controls/color, image processing, H.264/HEVC export with audio and cancellation are included.
- EXE FileVersion/ProductVersion: 1.4.1. SHA256: `99EDAF165D742E8D677A71286149B26F2F6C53CFA030440FB06E915F90B43B20`.

- `out/tmp/release-1.4.1-targeted.ps1`: eight cases PASS, logs `out/logs/release-1.4.1-targeted/` and `out/logs/release-1.4.1-targeted.log`. AverMedia switch unit tests, repair/UI contracts, native/Ampere/Ada lifecycle (6,2,5,3,4,6), Ampere Init-failure rollback and `mfg6-exact-rgb-planar-detail` content case. Each child bounded to 45 seconds (content case 60). Real Create/Evaluate succeeded; compatibility tests on RTX5070 do not replace the user's RTX30/40 test.

Prior r4 evidence remains in `RTX3060_FG_FREEZE_2026-09-18.md`; earlier limitations have not been erased.

## Package audit

Assets staged at `C:/veyra-releases/1.4.1/`. Commands:

```powershell
scripts/package-portable.ps1 -Root . -Version 1.4.1 -OutputDirectory C:/veyra-releases/1.4.1 -BuildDirectory out/build/scheduling-audit-20260918
python scripts/package-ffmpeg-source.py --prefix C:/veyra-deps/ffmpeg-ps5-dav1d-installed --vcpkg C:/veyra-deps/vcpkg --source C:/veyra-deps/ffmpeg-ps5-slices-source --dav1d-source C:/veyra-deps/vcpkg/buildtrees/dav1d/src/1.5.4-179377b46e.clean --output C:/veyra-releases/1.4.1/Veyra-1.4.1-FFmpeg-source.zip --version 1.4.1
python scripts/package-remoteplay-source.py --root . --output C:/veyra-releases/1.4.1/Veyra-1.4.1-RemotePlay-source.zip --version 1.4.1
python scripts/acceptance/release-archive-audit.py --directory C:/veyra-releases/1.4.1 --version 1.4.1 --output C:/veyra-releases/1.4.1/archive-audit.json
```

All exit0. Logs: `out/logs/release-1.4.1-{package,ffmpeg-source,remoteplay-source-final,archive-audit}.log`. RemotePlay source was regenerated after the build documentation update; the earlier archive was retained outside the final upload directory. No product test failures occurred in this release run. An initial whitespace audit with `core.autocrlf=false` misclassified Windows CRLFs; rerunning with the repository's actual configuration passed without changing source line endings.

| Asset | Bytes | SHA256 |
| --- | ---: | --- |
| Veyra-1.4.1-win64-portable.zip | 464924466 | `9E9E0D5E349BE6B9A8DEA69A15D9635B4123B40C396725792B68660154679071` |
| Veyra-1.4.1-FFmpeg-source.zip | 25277829 | `15A77217BEDCEB4280FC680D5C0761544170356CA85E871F1558B61C86EE5F02` |
| Veyra-1.4.1-RemotePlay-source.zip | 143753286 | `272E41BC128C9BD86D7A9A93E4F0150DD92DB6B9D00909643336B9E01561F166` |

112 portable files match their manifest. All 11 enhancement runtimes match approved hashes/signatures; two community NR files retain HashMismatch. MIT/BSD compatibility notices are present. FFmpeg binary/build provenance matches the patched source with dav1d. No SDK, source headers, model, PDB/LIB, log, test media or personal settings are included in the portable archive; dependency source archives exclude proprietary binaries. Git introduces no new runtime/SDK/media files.

## Extracted package execution

Expanded the final ZIP to `C:/veyra-releases/1.4.1-verify`. Ran `out/tmp/smoke-post140-package.ps1 -PackageOverride C:/veyra-releases/1.4.1-verify/Veyra-1.4.1-win64-portable -OutputOverride 'C:/Users/123/Desktop/Veyra DLSS Video Player/out/logs/release-1.4.1-portable-smoke' -MediaOverride 'C:/Users/123/Desktop/Veyra DLSS Video Player/out/logs/rtx30-package-motion-fixture.mkv' -RequireNonblank`.

Seven cases PASS: baseline 187 source frames; original/community/Ampere NR + native 6X each 141 source / 565 generated; forced Ampere 2X 78/52, forced Ampere 6X 78/260, forced Ada 6X 108/410. Counts include initialization and smoke-control pause/seek transitions; they are not steady-state FPS measurements. Every case exits normally and has a nonblank/nonconstant screenshot; the Ampere/Ada 6X images were also visually inspected. FFmpeg/CRT/NR/FG DLLs load from the extracted package with system-only PATH and TEMP working directory. EXE hash matches the tested build.

Evidence: `out/logs/release-1.4.1-portable-smoke.log`, per-case logs/JPEGs and `out/logs/release-1.4.1-portable-smoke/result.json`. No final-package test failed. Publication is authorized; GitHub asset verification is recorded after upload.

## Publication verification

- Release commit: `0999a86caf437336753ced3cc1c845da7104b6d4`; annotated tag `v1.4.1` resolves to that commit. `git push --atomic nrvideo main refs/tags/v1.4.1` succeeded without force.
- Created a draft with `gh release create v1.4.1 --repo Likely7/Veyra-NRVideo --verify-tag --draft --title 'Veyra 1.4.1 - RTX 30/40 DLSS 6X 与持续补帧修复' --notes-file docs/RELEASE_BODY_1.4.1.md`, then uploaded the three audited ZIPs with `gh release upload`.
- GitHub release ID: `391180237`. All three remote assets report `uploaded`; names, byte sizes and server SHA256 digests exactly match the table above. No extra assets were uploaded.
- Published with `gh release edit v1.4.1 --repo Likely7/Veyra-NRVideo --draft=false --latest`. API `repos/Likely7/Veyra-NRVideo/releases/latest` confirms tag `v1.4.1`, `draft=false`, `prerelease=false`, and publication at `2026-09-18T02:48:08Z` (10:48:08 Asia/Shanghai).
- Public release: https://github.com/Likely7/Veyra-NRVideo/releases/tag/v1.4.1
- Publication evidence is committed afterward as documentation only; the release tag and uploaded binaries remain at the tested release commit.

## User screenshots and support block

After publication, the user authorized adding three supplied screenshots and required the community/donation QR codes in every future release at their existing 220px width. Updated `RELEASE_BODY_1.4.1.md`, added reusable `RELEASE_SUPPORT.md`, and recorded the requirement in `AGENTS.md`. The three PNGs are separate Release assets; the original three ZIPs and tag are unchanged.

| Screenshot asset | Bytes | SHA256 |
| --- | ---: | --- |
| rtx4070-dlss6x-imax.png | 498701 | `35e879ada2430bf36261e16b8623d9ebf904e713ce9983cea1eaa6ee4f29a433` |
| rtx4070-dlss6x-gpu.png | 360370 | `84c2c42d5269dbf03a65e43b6507e065617f246a7cf95f0f7f19a436926cab74` |
| rtx5070-nr-dlss6x.png | 839615 | `5ef048ae86a933133b3a811c9eb4deb437f7931e042d914759fc77f012dd3b6f` |

GitHub API verifies uploaded image sizes/digests and exact release-body content. Browser inspection confirms all five images loaded (nonzero natural widths), screenshots rendered at 900px and both QR codes at 220px. Asset download endpoints use application/octet-stream in HTTP responses despite image/png asset metadata; the initial MIME-only check was overly strict, and actual browser rendering passed. No product rebuild was needed for this documentation-only change.
