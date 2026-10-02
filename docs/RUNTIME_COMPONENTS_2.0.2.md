# Veyra 2.0.2 NR runtime choices

All three choices appear in the same NR selector in List and Node mode. Changing the selection rebuilds every NR layer together. Existing saved selections and defaults are preserved: new RTX 50 configurations use Lecram; RTX 20/30/40 use SF-v2. The NVIDIA original is an explicit RTX 50 choice. Availability is experimental and is not a claim of validation on every model.

| Choice / 选择 | File relative to package | Version | Signature |
|---|---|---|---|
| RTX 50 · Lecram | `runtime/experimental/nvngx_dlssnr.dll` | 310.8.3.0 | HashMismatch; community modified |
| RTX 20–50 · SF-v2 | `runtime/experimental/nr-ampere/nvngx_dlssnr.dll` | 310.8.2.0 | NotSigned; community build |
| RTX 50 · NVIDIA original / 原版 | `runtime/experimental/nr-original/nvngx_dlssnr.dll` | 310.8.0.0 | Valid / NVIDIA |

Original file: 165840496 bytes, SHA256 `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`. It is the previously approved local original, copied unchanged. The signature describes this binary; Veyra's experimental Feature 18 integration is not NVIDIA certification or complete official DLSS 5 support.

All other identities remain those of [2.0.0](RUNTIME_COMPONENTS_2.0.0.md). The complete 43-file publisher lock is `RELEASE_2.0.2_RUNTIME_LOCK.json`; the package includes `release-runtime-manifest.json`. Proprietary runtime/SDK files are not in source Git. Delete an unused variant's DLL to remove it; selecting a missing/incompatible variant fails explicitly and restores the previous working settings. Publisher hashes do not block replacement DLLs at application startup.

列表与节点都提供上述三种 NR，切换时全链同步，原有选择和默认值保留。历史 ID 0 仍是 Lecram，ID 2 是 SF-v2，退役 ID 1 继续迁移为 SF-v2；新增原版用 ID 3，避免旧预设被悄悄改成其他 DLL。原版签名有效不代表 Veyra 获得 NVIDIA 官方认证。用户可自行替换 DLL，兼容性需自行确认。
