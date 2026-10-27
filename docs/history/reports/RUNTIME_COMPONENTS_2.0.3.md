# Veyra 2.0.3 GPU packages

Choose the package for the GPU Veyra uses (the high-performance DirectX 12 adapter on a hybrid PC). Both contain the same player executable/QML/shaders, Qt, patched FFmpeg, capture/Magewell, PS5, Xbox, Moonlight, image/video export and common notices. Extract into a new writable folder with 7-Zip and run `veyra_qml_ui.exe`. GPU/capture drivers are installed separately.

| Component | NVIDIA package | AMD package |
| --- | --- | --- |
| Lecram / SF-v2 / NVIDIA original NR | Included; gray when the GPU does not qualify | Gray; no NR NVIDIA DLLs |
| AMD lmxxf NR 0.39 assets + host runtime | Gray; no weights/HIP/runtime | Included; RX9000 + driver HIP 7 required |
| DLSS SR / RTX Video SR / RTX Video HDR / DLSS FG | Included; hardware and initialization limits apply | Gray; no NGX runtime |
| NVIDIA VFG 2–8X, Low/Medium/High | Five original native DLLs; RTX40/50 required | Gray; no VFG/CUDA runtime |
| FSR 3.1 SR/FG | Existing SDK 2.3.0 compatibility provider | Included in SDK 2.3.0's compatibility path |
| FSR4 ML SR/FG | Gray; shared FidelityFX components retained by explicit user choice | Included; RX9000 required; fallback SR follows existing provider compatibility |
| XeSS FG + XeLL / AMD compute optical flow | Common cross-vendor paths retained | Common cross-vendor paths retained |

The NR version list has four entries in List and Node mode, including RX9000 · lmxxf. Unsupported entries remain visible with a disabled reason. AMD NR's NVIDIA-only parameters and unsupported internal resolutions remain visible but gray. NR 0.39 retains the exact complete weight/kernel set; height ≤1080, pixels ≤1920×1080 and width ≤2560 are its API budget. Preview may downsample to that budget; image/video NR export requires a native size inside the budget. HDR AMD NR and native 1440p/4K AMD NR export are unavailable. No model is quantized or rewritten for packaging.

NVIDIA VFG uses official `nvidia-vfx` 0.2.0.0 / Video Effects SDK 1.3.0. The original wheel SHA256 is `5aaf6a42bc6b6dbbf52fcb714194c994a6893cbbf7ada38bc2165a1f83e4a6fc`. Its VFG-only dependency set is `NVVideoEffects.dll`, `NVCVImage.dll`, `nvVFXVideoFrameGeneration.dll`, `nvngxruntime.dll` and `cudart64_12.dll`: 210788400 bytes, without the nine unrelated NPP DLLs (287106032 bytes). No Python, OpenCV, SDK samples or system `nvcuda.dll` is needed in the portable runtime. Native calls still use the installed NVIDIA driver. VFG High 8X can be expensive; preview may fall behind or shed enhancement opportunities, while offline export processes every source frame. Submitted FPS is not measured physical display FPS or end-to-end latency.

Both packages retain the existing signed SDK 2.3.0 loader, FSR4.1.1 SR provider and FSR4.0.1 FG provider, including FSR3.1 compatibility. The user explicitly requested keeping FSR3.1/4 together instead of splitting the runtime. NVIDIA exposes FSR3.1 and keeps FSR4 ML disabled; AMD exposes the ML paths only for qualifying RX9000 hardware. Each DLL keeps its existing approved identity. No old SDK replacement, DLL patch or new ABI compatibility branch is shipped.

AMD NR source is lmxxf commit `78f548749e74824327b8458c57be31a1df78376a`; its MIT host DLL is built from the actual published source with the documented MSVC shim (1082368 bytes, SHA256 `2693cdb760db564ec6f34b39b8a7e554d35cbab5df2366bff030e1611756e3d8`, NotSigned). User-supplied `Magpie-DLSS5-AMD-0.39.zip` is 340434944 bytes, SHA256 `9ea84c665d270cd45e24184729b8272c152485df462a1528539ed778d41849f5`. All 739 supplied checksum entries matched; 186 weights, two maps and 62 gfx1200/gfx1201 HIP modules remain intact. Model weights are not MIT. No Magpie executable, ReShade/RenoDX add-on or AMD driver DLL is distributed. No RX9000 inference has been verified on this RTX5070 machine.

Each package has its own `package-manifest.json`, top-level `release-runtime-manifest.json`, matching component-page manifest and appropriate VFG/AMD NR manifest, licenses and corresponding-source links. The source publisher lock retains existing runtime identities and separately audits vendor additions. All proprietary SDK/runtime/model assets are excluded from source Git. Runtime loading still uses absolute paths, API/initialization checks and error logging; hashes are publisher audit data and do not lock user replacements. Experimental distribution is user-authorized for 2.0.3, not vendor partnership/certification or a general redistribution grant.

## 中文

分包只减少运行资源，基础功能完整，两包程序相同。NVIDIA 包不带 AMD NR；两包保留原有 FSR3.1/4 共用组件，NVIDIA 的 FSR4 ML 仍禁用；AMD 包不带 NVIDIA NR/DLSS/VFG/CUDA。FSR3.1、XeSS 等实际跨显卡能力保留。换包/恢复旧配置时关闭不可用效果并保留参数，界面入口显示灰色原因。AMD NR 的真实推理、画质与长期稳定，Xbox 真机有声/长期稳定，RTX40 和用户驱动 616.92 仍待实机验收。HDR/Dolby Vision PR #13/#14 本轮未适配或合并。
