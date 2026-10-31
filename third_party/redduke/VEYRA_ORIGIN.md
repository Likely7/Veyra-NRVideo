# RX 7000 NR source and runtime provenance

The independent network layer comes from [RedDukeDev/dlss5-nr-amd-custom](https://github.com/RedDukeDev/dlss5-nr-amd-custom), commit `ab455d179922adc11f65675d399e1d553bf423f0`. Its `runtime/` and NR `kernels/` are MIT, Copyright (c) 2026 RedDukeDev; see LICENSE in this directory. ZLUDA is separately MIT OR Apache-2.0, pinned at `b135e233bb7a6ff7f8690e49701f87f4944adcee`.

Only five source files are fetched to an external dependency directory: `runtime/core/network.cpp`, `network.h`, `runtime/common/cuda_min.h`, `ngx_cuda.h`, and LICENSE. `scripts/amd-nr/prepare-rdna3-source.py` checks their exact Git blob IDs and records original/prepared SHA256. It patches selection of the host's D3D12 adapter LUID, rejects ambiguous multi-GPU mappings, and allows asynchronous enqueue only when our worker uses native shared GPU fences. Upstream synchronization remains the default for initialization.

Veyra's GPLv3 implementation is `Rdna3NrBackend.cpp`, `Rdna3NrWorker.cpp`, their headers/protocol, and `Rdna3NrDecode.hlsl`. One isolated worker owns each layer's network; Veyra owns temporal history, proxy conversion, strength, residual and protection. The network uses zero depth/motion and reset on every frame. This is same-frame inference, not upstream's game wrapper or asynchronous old-frame residual reprojection.

The local runtime subset contains the raw network DLL, eleven NR kernels, translation cache, ZLUDA compatibility shims and notices. No OptiScaler, ReShade, game proxy, SR/RR/FG kernel, driver or HIP SDK is staged. The private `zluda/nvcuda.dll` is an open-source compatibility shim loaded only in the owned worker, never a replacement for the main process's NVIDIA driver.

The NVIDIA DLL/model and translated cache are not relicensed under MIT. The selected original DLL is version 310.9.16.0, 266424944 bytes, valid NVIDIA signature, SHA256 `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f`. Local assets remain outside source Git. This task does not add this subset to the formal Release whitelist or authorize publication. Existing release guards remain in force.

See [execution and validation](../../docs/AMD_RDNA3_NR_PLAN_2026-10-29.md). Local D3D12 contract tests use an explicitly identified copy worker, not AMD inference. Actual RX 7000 inference, quality, speed, HDR, export and long-duration behavior require Radeon hardware validation.
