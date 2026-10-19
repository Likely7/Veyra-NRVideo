# lmxxf AMD NR origin

- Upstream: https://github.com/lmxxf/dlss5-on-amd-9070xt-porting
- Current fixed commit: `4764e4f1a24f5234b8e29952dc41f493b2d77c90` (0.44 F9 hotfix).
- Previous 0.39 integration: `78f548749e74824327b8458c57be31a1df78376a`.
- License: MIT, Copyright (c) 2026 Kien; original license in `LICENSE`.
- `LmxxfNrApi.h`: byte-identical copy of upstream `include/LmxxfNrApi.h`.
- Runtime code remains in the external pinned checkout; the Veyra source Git
  contains the public C ABI header, attribution and reproducible build recipe.
- Build adaptation: MSVC `NOMINMAX`, linking `user32.lib` for the upstream hotkey
  reference, and a forced header mapping its sole GNU `noinline` annotation to
  `__declspec(noinline)`, `/utf-8`, and undefining Windows' `small` macro.
- `veyra-model-isolation.patch` changes `src/native_model_mode.h` and
  `src/native_hot_flags.h` in isolated build copies: fixed model per DLL,
  no external model hotkey/config polling. Small and Original coexist across
  NR layers without per-frame environment mutation. Network/codec math is unchanged.
- `scripts/amd-nr/veyra-runtime-model.cpp` exposes the actual compiled selector
  and upstream Small asset preflight; the host rejects wrong modules and missing
  Small weights instead of silently using Original. The public C ABI remains unchanged.
- Veyra-owned host files: `LmxxfNrBackend.h/.cpp`, `NrInstance`/`EnhanceGraph`
  integration, GPU-specific QML choices and local resource preparation scripts.
  They use the existing shared enhancement graph and NR composite/history.

This does **not** license the whole Magpie portable package under MIT. Magpie
has its own GPL license. The NVIDIA-derived `.f16`/`.f32` model weights are
separate binary assets, not MIT source; HIP modules are also kept outside Git.
User-provided 0.44 resources are used only for this isolated local integration;
`stage-044-runtime.py` audits all 875 package checksums before extracting assets.
The old `prepare-local-runtime.py` is retained solely for historical 0.39 builds.
No Magpie executable, proxy `dxgi.dll` or ReShade `.addon64` is loaded by Veyra.

See `docs/AMD_NR_INTEGRATION_2026-10-03.md` for asset provenance, ABI limits and
the distinction between host tests and unperformed RX 9000 inference validation.
See `docs/AMD_NR_044_PLAN_2026-10-18.md` for the current scope and validation.
