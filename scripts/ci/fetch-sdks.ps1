# CI only: stages the public SDK headers/import libraries that CMakeLists.txt
# expects under third_party_local/ (gitignored), each fetched from the vendor's
# own GitHub repository at an exact commit and verified by commit SHA.
# Nothing proprietary is committed; runtime DLLs are not staged.
param(
    [Parameter(Mandatory = $true)][string]$VeyraRoot,
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [Parameter(Mandatory = $true)][string]$ManifestOut
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$pins = @(
    @{ Name = 'NVIDIA DLSS SDK 310.7.0';      Repo = 'NVIDIA/DLSS';                              Commit = 'a291cc7d2cc642a51566f3dfd5376f635cd1b284' },
    @{ Name = 'Intel XeSS SDK 3.0.2';         Repo = 'intel/xess';                               Commit = '8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0' },
    @{ Name = 'AMD FidelityFX SDK 2.3.0';     Repo = 'GPUOpen-LibrariesAndSDKs/FidelityFX-SDK'; Commit = '60f4ea81909200d8542eca14dccb2628b763a9a3' },
    @{ Name = 'AMD AMF SDK 1.5.3 headers';    Repo = 'GPUOpen-LibrariesAndSDKs/AMF';             Commit = '57ee0cfb0e1a08d9041f77435653794c1b012be0' },
    @{ Name = 'nv-codec-headers n13.0.19.0';  Repo = 'FFmpeg/nv-codec-headers';                  Commit = 'e844e5b26f46bb77479f063029595293aa8f812d' },
    # NVIDIA publishes Optical Flow SDK 5.0.7 behind a developer login; this public
    # mirror's 5.0.7 commit carries the MIT-licensed headers byte for byte.
    @{ Name = 'NVIDIA Optical Flow SDK 5.0.7 headers'; Repo = 'mbucchia/Optical-Flow-SDK';      Commit = 'b9f71e4ca268e99f97e33c605b4a6d6a2256a83b' },
    @{ Name = 'libdovi 3.4.0 (dovi_tool 2.3.3)'; Repo = 'quietvoid/dovi_tool';                  Commit = 'd1abe0e27ff2c7ab3339614d06db9f8a058af6b2' }
)
$nvofSha256 = @{
    'nvOpticalFlowCommon.h' = 'cdbdbf0797fc02d2de1051520d4164bb1e36b7722009f46cb16216675319436a'
    'nvOpticalFlowD3D12.h'  = 'f12bbc6c54278cb5d3ae81723ca0363644f672fc3e72b635a709470ffdd6886f'
}

function Get-Pinned([string]$Repo, [string]$Commit, [string]$Dir) {
    if (Test-Path -LiteralPath $Dir) { Remove-Item -LiteralPath $Dir -Recurse -Force }
    git init -q $Dir
    git -C $Dir remote add origin "https://github.com/$Repo.git"
    git -C $Dir fetch -q --depth 1 origin $Commit
    # Byte-exact checkout: no CRLF conversion, so the pinned file hashes hold on Windows.
    git -C $Dir -c advice.detachedHead=false -c core.autocrlf=false -c core.eol=lf checkout -q FETCH_HEAD
    $head = (git -C $Dir rev-parse HEAD).Trim()
    if ($head -ne $Commit) { throw "$Repo resolved to $head, expected $Commit" }
}
function Copy-Tree([string]$From, [string]$To) {
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    Copy-Item -Path (Join-Path $From '*') -Destination $To -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$src = @{}
foreach ($p in $pins) {
    $dir = Join-Path $WorkRoot (($p.Repo -split '/')[1])
    Write-Host "Fetching $($p.Repo) @ $($p.Commit)"
    Get-Pinned $p.Repo $p.Commit $dir
    $src[$p.Repo] = $dir
}
$tpl = Join-Path $VeyraRoot 'third_party_local'

$dlss = Join-Path $tpl 'nvidia/DLSS_SDK_310.7.0'
Copy-Tree (Join-Path $src['NVIDIA/DLSS'] 'include') (Join-Path $dlss 'include')
Copy-Tree (Join-Path $src['NVIDIA/DLSS'] 'lib') (Join-Path $dlss 'lib')

Copy-Tree (Join-Path $src['intel/xess'] 'inc') (Join-Path $tpl 'intel/xess-3.0.2/inc')

$ffxSrc = Join-Path $src['GPUOpen-LibrariesAndSDKs/FidelityFX-SDK'] 'Kits/FidelityFX'
foreach ($sub in 'api', 'framegeneration', 'upscalers') {
    Copy-Tree (Join-Path $ffxSrc "$sub/include") (Join-Path $tpl "amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/$sub/include")
}

Copy-Tree (Join-Path $src['GPUOpen-LibrariesAndSDKs/AMF'] 'amf/public/include') (Join-Path $tpl 'amd/AMF-SDK-1.5.3/include')
Copy-Tree (Join-Path $src['FFmpeg/nv-codec-headers'] 'include') (Join-Path $tpl 'nvidia/nv-codec-headers-13.0/include')

$nvof = Join-Path $tpl 'nvidia/Optical_Flow_SDK_5.0.7'
Copy-Tree (Join-Path $src['mbucchia/Optical-Flow-SDK'] 'NvOFInterface') (Join-Path $nvof 'NvOFInterface')
Copy-Tree (Join-Path $src['mbucchia/Optical-Flow-SDK'] 'Common/NvOFBase') (Join-Path $nvof 'Common/NvOFBase')
foreach ($h in $nvofSha256.Keys) {
    $got = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $nvof "NvOFInterface/$h")).Hash.ToLowerInvariant()
    if ($got -ne $nvofSha256[$h]) { throw "$h SHA-256 $got does not match $($nvofSha256[$h])" }
}

$manifest = [ordered]@{ sdks = @($pins | ForEach-Object { [ordered]@{ name = $_.Name; repo = $_.Repo; commit = $_.Commit } }) }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ManifestOut -Encoding utf8
"DOVI_SOURCE=$($src['quietvoid/dovi_tool'])" >> $env:GITHUB_ENV
