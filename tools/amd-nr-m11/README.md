# AMD RDNA3 Native D3D12 Direct ELF Passthrough Worker (`DLSSNR-AMD v0.0.9m11`)

Source-only integration for building an isolated `veyra_amd_nr_worker.exe` powered by `mochizuki0323/DLSSNR-AMD` (`v0.0.9m11`, MIT License) Native D3D12 Direct ELF Passthrough (`amdxc64.dll`, `gfx11_mach=0x941`, `-DNR_INT4=1`).

## Key Benefits Over the Legacy ZLUDA + HIP 7 Worker

- **Zero Vulkan / Zero HIP 7 SDK / Zero ZLUDA**: imports only `d3d12.dll`, `dxgi.dll`, `ADVAPI32.dll`, and `KERNEL32.dll`.
- **1.86x–5.0x Faster on RX 7900 XT (26.x drivers)**:
  - 1080p NR single frame: `30.07 ms -> ~16.15 ms` (M0 Small) / `~13.10 ms` (M3 Small Experimental) / `~6.0 ms` amortized with `NrReuse` Half.
  - Worker cold start: `~4.2s -> ~0.9s`.
  - Supports all 7 models (`CC_SMALL_FP8`, `CC_SMALL_FP8_EXPERIMENTAL`, `CC_SILVER_AARDWOLD`, and 4 `CC_SILVER_AARDWOLD_OPTIMIZED` Quality/Balanced/Performance/Ultra tiers).

## Proprietary Weights Policy (No Redistribution)

NVIDIA's `nvngx_dlssnr.dll` and extracted model weights (`CC_*.bin`) are NVIDIA property and are **never** included in Git or public share archives. End users generate them locally with `setup-models.bat` / `setup-models.ps1` from their own copy of `nvngx_dlssnr.dll` (310.9.16 recommended) using the open-source extractor (`dlssnr_extract_model.exe`), XOR delta structure patches, and `dlssnr-int4-weights.exe`.

## Building

Requires LLVM/Clang (e.g. ROCm clang++), Windows SDK headers (`d3d12.h`, `dxgi1_6.h`), and a checked-out copy of `DLSSNR-AMD` (MIT):

```powershell
set DLSSNR_AMD_SRC=C:\path\to\DLSSNR-AMD
python tools/amd-nr-m11/build_m11.py
```

## Attribution

- `mochizuki0323/DLSSNR-AMD` (MIT License, Copyright (c) 2026 mochizuki0323)
- `mauri870/DLSSNR-RDNA3` (MIT License) & `TheAutomatic` (Windows RDNA3 / D3D12 contributions to `DLSSNR-AMD`)
- `NVIDIA` and `DLSS` are trademarks of NVIDIA Corporation. Independent community integration; not affiliated with or endorsed by NVIDIA or AMD.
