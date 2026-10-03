# lmxxf AMD NR origin

- Upstream: https://github.com/lmxxf/dlss5-on-amd-9070xt-porting
- Fixed commit: `78f548749e74824327b8458c57be31a1df78376a`.
- License: MIT, Copyright (c) 2026 Kien; original license in `LICENSE`.
- `LmxxfNrApi.h`: byte-identical copy of upstream `include/LmxxfNrApi.h`.
- Runtime code remains in the external pinned checkout; the Veyra source Git
  contains the public C ABI header, attribution and reproducible build recipe.
- Build adaptation: MSVC `NOMINMAX`, linking `user32.lib` for the upstream hotkey
  reference, and a forced header mapping its sole GNU `noinline` annotation to
  `__declspec(noinline)`. No changes to upstream inference/codec source.
- Veyra-owned host files: `LmxxfNrBackend.h/.cpp`, `NrInstance`/`EnhanceGraph`
  integration, GPU-specific QML choices and local resource preparation scripts.
  They use the existing shared enhancement graph and NR composite/history.

This does **not** license the whole Magpie portable package under MIT. Magpie
has its own GPL license. The NVIDIA-derived `.f16`/`.f32` model weights are
separate binary assets, not MIT source; HIP modules are also kept outside Git.
User-provided 0.39 resources are used only for the recorded local experiment.
No Magpie executable, proxy `dxgi.dll` or ReShade `.addon64` is loaded by Veyra.

See `docs/AMD_NR_INTEGRATION_2026-10-03.md` for asset provenance, ABI limits and
the distinction between host tests and unperformed RX 9000 inference validation.
