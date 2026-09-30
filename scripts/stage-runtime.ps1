[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root
)

# Stage the local DLSSNR runtime for Phase 0+ probes (Playbook 4.3/4.4).
# - Copies (never moves) the pinned default NR runtime into runtime_local/nvidia/
#   after verifying its identity. Since 2026-09-29 (user decision) the default
#   slot holds the community Lecram build 310.8.3.0 (RankFTW/rhi-repo
#   dlssnr-310.8.Lecram): kernels rebuilt, weights unchanged, output measured
#   byte-identical to NVIDIA 310.8.0.0 on RTX 5070 and ~2% cheaper. It carries
#   NVIDIA's certificate over modified content, so Authenticode is HashMismatch.
# - Writes runtime-manifest.json with the real size/hash and local-experimental
#   mode markers.
# - Creates a persistent runtime_local/config/ngx-local.json identity exactly
#   once; reruns never regenerate the GUID.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedSize = [long]165840496
$expectedSha256 = "F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC"

$sourceDll = Join-Path $Root "third_party_local\nvidia\dlssnr-310.8.Lecram\nvngx_dlssnr.dll"
if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
    Write-Host "stage-runtime.ps1: source DLL missing: $sourceDll"
    exit 2
}

$item = Get-Item -LiteralPath $sourceDll
if ($item.Length -ne $expectedSize) {
    Write-Host ("stage-runtime.ps1: size mismatch actual={0} expected={1}" -f $item.Length, $expectedSize)
    exit 3
}
$hash = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash.ToUpperInvariant()
if ($hash -ne $expectedSha256) {
    Write-Host ("stage-runtime.ps1: sha256 mismatch actual={0}" -f $hash)
    exit 3
}
$signature = Get-AuthenticodeSignature -LiteralPath $sourceDll
if ([string]$signature.Status -ne "HashMismatch") {
    Write-Host ("stage-runtime.ps1: signature status {0}" -f [string]$signature.Status)
    exit 3
}

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
$targetDll = Join-Path $runtimeDir "nvngx_dlssnr.dll"

if (-not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
    Copy-Item -LiteralPath $sourceDll -Destination $targetDll
    Write-Host "stage-runtime.ps1: copied nvngx_dlssnr.dll into runtime_local/nvidia"
}
else {
    $stagedHash = (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($stagedHash -ne $expectedSha256) {
        Write-Host "stage-runtime.ps1: staged DLL identity drifted; replacing from the pinned Lecram source"
        Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
    }
    else {
        Write-Host "stage-runtime.ps1: staged DLL already matches the pinned identity"
    }
}

# Stage the official DLSS SR DLL from the SDK (Playbook section 3.2).
$sdkRel = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0\lib\Windows_x86_64\rel\nvngx_dlss.dll"
$srDll = Join-Path $runtimeDir "nvngx_dlss.dll"
if (Test-Path -LiteralPath $sdkRel -PathType Leaf) {
    if (-not (Test-Path -LiteralPath $srDll -PathType Leaf)) {
        Copy-Item -LiteralPath $sdkRel -Destination $srDll
        Write-Host "stage-runtime.ps1: copied nvngx_dlss.dll from SDK rel/"
    }
    else {
        $sdkHash = (Get-FileHash -LiteralPath $sdkRel -Algorithm SHA256).Hash.ToUpperInvariant()
        $stagedSrHash = (Get-FileHash -LiteralPath $srDll -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($sdkHash -ne $stagedSrHash) {
            Copy-Item -LiteralPath $sdkRel -Destination $srDll -Force
            Write-Host "stage-runtime.ps1: staged nvngx_dlss.dll drifted; replaced from SDK"
        }
    }
}

# Stage the official DLSSG runtime DLL from the SDK (Playbook section 14/3.2).
# 2026-09-30 user decision: the frame-generation provider moves to the official
# NVIDIA DLSS SDK 310.9.1 build so the community 310.9.1 provider patches and
# their optimized kernels can be used. Identity is still pinned and verified:
# 7,460,976 bytes, 310.9.1.0, SHA256
# FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82, Authenticode
# Valid / NVIDIA Corporation. Only this SDK path may be used.
$dlssgExpectedSize = [long]7460976
$dlssgExpectedSha256 = "FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82"
$dlssgRel = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.9.1\lib\Windows_x86_64\rel\nvngx_dlssg.dll"
$dlssgDll = Join-Path $runtimeDir "nvngx_dlssg.dll"
$dlssgPinned = $true
if (Test-Path -LiteralPath $dlssgRel -PathType Leaf) {
    $dlssgItem = Get-Item -LiteralPath $dlssgRel
    $dlssgHash = (Get-FileHash -LiteralPath $dlssgRel -Algorithm SHA256).Hash.ToUpperInvariant()
    $dlssgSig = Get-AuthenticodeSignature -LiteralPath $dlssgRel
    if ($dlssgItem.Length -ne $dlssgExpectedSize -or $dlssgHash -ne $dlssgExpectedSha256 -or [string]$dlssgSig.Status -ne "Valid") {
        Write-Host ("stage-runtime.ps1: nvngx_dlssg.dll identity mismatch size={0} sha256={1} sig={2}; NOT staged" -f $dlssgItem.Length, $dlssgHash, [string]$dlssgSig.Status)
        exit 4
    }
    if (-not (Test-Path -LiteralPath $dlssgDll -PathType Leaf)) {
        Copy-Item -LiteralPath $dlssgRel -Destination $dlssgDll
        Write-Host "stage-runtime.ps1: copied nvngx_dlssg.dll from SDK rel/ (pinned identity verified)"
    }
    else {
        $stagedDlssgHash = (Get-FileHash -LiteralPath $dlssgDll -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($stagedDlssgHash -ne $dlssgExpectedSha256) {
            Copy-Item -LiteralPath $dlssgRel -Destination $dlssgDll -Force
            Write-Host "stage-runtime.ps1: staged nvngx_dlssg.dll drifted; replaced from SDK"
        }
    }
}
else {
    Write-Host "stage-runtime.ps1: SDK nvngx_dlssg.dll not found; skipped (Phase 6 requires it)"
}

# DLSS-G 310.9.1 optimized network kernels (DLSSG-Transfusion 1.4.5.3). They
# are user-replaceable runtime data loaded from disk at session start, never
# compiled into the executable. See THIRD_PARTY_NOTICES.md for provenance.
$kernelSource = Join-Path $Root "third_party_local\nvidia\dlssg-transfusion-kernels-1.4.5.3"
$kernelDir = Join-Path $runtimeDir "dlssg-kernels"
if (Test-Path -LiteralPath $kernelSource -PathType Container) {
    [IO.Directory]::CreateDirectory($kernelDir) | Out-Null
    $kernels = @(Get-ChildItem -LiteralPath $kernelSource -Filter *.ptx | Sort-Object Name)
    foreach ($kernel in $kernels) {
        Copy-Item -LiteralPath $kernel.FullName -Destination (Join-Path $kernelDir $kernel.Name) -Force
    }
    Write-Host ("stage-runtime.ps1: staged {0} DLSS-G optimized kernel file(s) into runtime_local/nvidia/dlssg-kernels" -f $kernels.Count)
}
else {
    Write-Host "stage-runtime.ps1: DLSSG-Transfusion kernel set not found; the provider optimization stays off"
}

$manifestPath = Join-Path $runtimeDir "runtime-manifest.json"
$srManifestEntry = ""
if (Test-Path -LiteralPath $srDll -PathType Leaf) {
    $srItem = Get-Item -LiteralPath $srDll
    $srHash = (Get-FileHash -LiteralPath $srDll -Algorithm SHA256).Hash.ToUpperInvariant()
    $srSig = Get-AuthenticodeSignature -LiteralPath $srDll
    $srVersion = [string]$srItem.VersionInfo.FileVersion
    $srManifestEntry = ",`n    {`n      `"name`": `"nvngx_dlss.dll`",`n      `"size`": $($srItem.Length),`n      `"sha256`": `"$srHash`",`n      `"fileVersion`": `"$srVersion`",`n      `"authenticode`": `"$([string]$srSig.Status)`",`n      `"source`": `"official DLSS SDK 310.7.0 rel`",`n      `"redistributable`": false`n    }"
}
$dlssgManifestEntry = ""
if (Test-Path -LiteralPath $dlssgDll -PathType Leaf) {
    $dlssgVersion = [string](Get-Item -LiteralPath $dlssgDll).VersionInfo.FileVersion
    $dlssgManifestEntry = ",`n    {`n      `"name`": `"nvngx_dlssg.dll`",`n      `"size`": $dlssgExpectedSize,`n      `"sha256`": `"$dlssgExpectedSha256`",`n      `"fileVersion`": `"$dlssgVersion`",`n      `"authenticode`": `"Valid`",`n      `"source`": `"official DLSS SDK 310.9.1 rel (pinned, 2026-09-30 user decision)`",`n      `"redistributable`": false`n    }"
}
$manifestJson = "{`n  `"schema`": 1,`n  `"mode`": `"local-experimental-only`",`n  `"files`": [`n    {`n      `"name`": `"nvngx_dlssnr.dll`",`n      `"size`": 165840496,`n      `"sha256`": `"F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC`",`n      `"fileVersion`": `"310.8.3.0`",`n      `"authenticode`": `"HashMismatch`",`n      `"source`": `"community Lecram 310.8.3 (RankFTW/rhi-repo dlssnr-310.8.Lecram), user-approved 2026-09-29`",`n      `"redistributable`": false`n    }$srManifestEntry$dlssgManifestEntry`n  ]`n}"
Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding utf8
Write-Host "stage-runtime.ps1: runtime-manifest.json written"

$configDir = Join-Path $Root "runtime_local\config"
New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$identityPath = Join-Path $configDir "ngx-local.json"
if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) {
    $id = [guid]::NewGuid().ToString()
    $identityJson = @{
        ngxProjectId = $id
        engineType = 'custom'
        engineVersion = 'Veyra-Experimental-0.1.0'
    } | ConvertTo-Json
    Set-Content -LiteralPath $identityPath -Value $identityJson -Encoding utf8
    Write-Host "stage-runtime.ps1: persistent local NGX identity created"
}
else {
    Write-Host "stage-runtime.ps1: local NGX identity already present (kept)"
}

exit 0
