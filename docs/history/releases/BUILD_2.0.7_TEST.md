# 2.0.7 local test source and build

Use `Veyra-2.0.7-test-source.zip` for the application, unchanged `Veyra-2.0.6-dependency-source.zip` (SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`), and `FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip` (SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`) for the current patched FFmpeg/dav1d/zlib. The old dependency ZIP alone is insufficient for the current subtitle FFmpeg.

Follow BUILD_2.0.6.md and BUILD_2.0.0.md with MSVC 2022 x64/C++20, CMake/Ninja, Python3.11 and Qt6.8.3. Set external SDK/dependency paths explicitly, preserve static subtitle support, PS5 slices256, DeckLink SDK, RemotePlay/Moonlight/Xbox options and direct NGX. Numeric version2.0.7, display override `-DVEYRA_DISPLAY_VERSION=2.0.7-test`, shipped PE2.0.7.0. No SDK, model, runtime, credential or private media is included in application source.

`scripts/acceptance/test-2.0.7-build.py` records this machine's complete CMake configuration and targets. On another machine supply its own dependency paths rather than copying the E-drive values. This build generates MSVC import libraries from the matching FFmpeg DEFs when the installed dependency tree contains only DLL/DEF files. Application runtime components remain byte-identical to approved vendor donors; publisher identity records do not lock user replacements.

`test-2.0.7-package.py stage/seal/cold` stages approved vendor payloads, replaces application/QML/shaders with this source build, audits hashes/vendor separation/licenses/PE imports, produces corresponding source, checks every ZIP entry and cold-starts each final ZIP with a private profile. Its local `main` merge requirement prevents packaging a divergent source tree. No GitHub publication is performed.

See TEST_2.0.7_REPORT_2026-10-12.md for measured coverage and limits. Prior reports describe historical source builds, not additional current validation. NVIDIA long-run VRAM growth and AMD/Blackmagic field checks remain open.

## Cadence refresh (2.0.7-test-r2)

The rebuilt application source is `Veyra-2.0.7-test-r2-source.zip`; the dependency source archives above are unchanged. Use display override `-DVEYRA_DISPLAY_VERSION=2.0.7-test-r2` (PE version remains 2.0.7.0). On this machine the separate fresh build command is `python -X utf8 -B -I scripts/acceptance/test-2.0.7-build.py --task test-2.0.7-refresh-20261012 --label 2.0.7-test-r2`.

Packaging uses the same explicit task/label, `--validation-record docs/TEST_2.0.7_REFRESH_2026-10-12.md`, and the audited original 2.0.7 NVIDIA/AMD folders as `--nvidia-donor` / `--amd-donor`. Existing user profiles and logs outside the donor manifest are never copied. Startup checks keep any existing user session running, record that condition, and do not measure GPU performance. Current results and the extended synthetic half-rate test limitation are in [the refresh report](../reports/TEST_2.0.7_REFRESH_2026-10-12.md).
