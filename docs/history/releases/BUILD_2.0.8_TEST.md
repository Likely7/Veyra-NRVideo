# 2.0.8 local test source and build

Use `Veyra-2.0.8-test-source.zip` for the application. The dependency archives are unchanged from 2.0.7: `Veyra-2.0.6-dependency-source.zip` (SHA256 `4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`) and `FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip` (SHA256 `49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`) for the patched FFmpeg/dav1d/zlib.

Follow BUILD_2.0.7_TEST.md, BUILD_2.0.6.md and BUILD_2.0.0.md: MSVC 2022 x64/C++20, CMake/Ninja, Python 3.11 and Qt 6.8.3 (now including the Network module for the update check). Set external SDK and dependency paths explicitly; keep static subtitle support, PS5 slices256, the DeckLink SDK, the RemotePlay/Moonlight/Xbox options and direct NGX. Numeric version 2.0.8, display override `-DVEYRA_DISPLAY_VERSION=2.0.8-test`, shipped PE 2.0.8.0. No SDK, model, runtime, credential or private media is included in the application source.

On this machine the fresh build command is `python -X utf8 -B -I scripts/acceptance/test-2.0.8-build.py --task claude-release-2.0.8-20261015 --label 2.0.8-test`.

**Console code page.** On a Chinese Windows install the compiler prints its `/showIncludes` notes in the console's output code page. The configure step reads that prefix as raw bytes (CMakeLists.txt, "Match the compiler's actual bytes"). Run the build from a console set to UTF-8 (`chcp 65001`). Under code page 936 the prefix comes out as GBK bytes, CMake silently fails to write `CMakeFiles/rules.ninja`, and ninja then stops with `loading 'CMakeFiles\rules.ninja': The system cannot find the file specified` (seen 2026-10-15; the 2.0.7 build had run under 65001).

`test-2.0.8-package.py stage/seal/cold` stages from the audited 2.0.7 r2 NVIDIA/AMD folders, replaces the application, QML and shaders with this build, and adds the identity-checked DLSS-G 310.9.16 performance-mode runtime (`runtime/experimental/dlssg-lite/nvngx_dlssg.dll`, user-approved for the formal package 2026-10-15) to the NVIDIA package only. It audits hashes, vendor separation, licenses and PE imports, produces the corresponding source, checks every ZIP entry and cold-starts each final ZIP with a private profile. Its local `main` merge requirement prevents packaging a divergent source tree. No GitHub publication is performed.

See TEST_2.0.8_REPORT_2026-10-15.md for measured coverage and limits.
