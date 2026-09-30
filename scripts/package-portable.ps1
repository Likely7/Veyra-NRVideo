param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
  [Parameter(Mandatory = $true)][string]$OutputDirectory,
  [string]$BuildDirectory,
  [string]$DependencyRoot,
  [switch]$LocalVideoHdr,
  # Suffix used only for the staging/archive name (e.g. "beta" -> Veyra-1.3.1beta-win64-portable).
  # The numeric $Version still names the docs and must match the EXE's numeric parts.
  [string]$Label = ''
)
$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$resolvedDependencies = $resolvedRoot
if ($DependencyRoot) { $resolvedDependencies = (Resolve-Path -LiteralPath $DependencyRoot).Path }
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$bin = Join-Path $resolvedRoot 'out/build/x64-release'
if ($BuildDirectory) { $bin = (Resolve-Path -LiteralPath $BuildDirectory).Path }
$stage = Join-Path $resolvedOutput "Veyra-$Version$Label-win64-portable"
$archive = "$stage.zip"
# Preserve existing candidates and their verification evidence.
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) { throw 'Output exists; choose a new staging directory.' }
$ffmpegRoot = 'C:\veyra-deps\installed\x64-windows'
$cachePath = Join-Path $bin 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath -PathType Leaf) {
  $ffmpegMatch = [regex]::Match((Get-Content -LiteralPath $cachePath -Raw), '(?m)^VEYRA_FFMPEG_ROOT:[^=]*=(.+)$')
  if ($ffmpegMatch.Success -and -not [string]::IsNullOrWhiteSpace($ffmpegMatch.Groups[1].Value)) {
    $ffmpegRoot = $ffmpegMatch.Groups[1].Value.Trim()
  }
}
$ffmpegBin = Join-Path $ffmpegRoot 'bin'
$applicationFiles = @('veyra.exe','avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')
if (Test-Path -LiteralPath (Join-Path $ffmpegBin 'dav1d.dll') -PathType Leaf) {
  $applicationFiles += 'dav1d.dll'
}
if ([version]$Version -ge [version]'0.0.5') {
  $cache = Get-Content -LiteralPath (Join-Path $bin 'CMakeCache.txt') -Raw
  if ($cache -notmatch 'VEYRA_ENABLE_REMOTEPLAY:BOOL=ON') { throw '0.0.5 package requires the real PS5 backend enabled' }
}
$appVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $bin 'veyra.exe'))
# The display string may carry a label ("1.3.1beta"); the numeric parts still have
# to match the version the package is built as.
if (-not $appVersion.ProductVersion.StartsWith($Version)) { throw "EXE version $($appVersion.ProductVersion) does not match $Version" }
# Publisher audit only. The application permits users to replace these DLLs.
$runtimeFiles = @(
  @{ Name='nvngx_dlss.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlss.dll'; Hash='BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E'; Category='official-dlss-sdk-310.7.0-rel'; Experimental=$false },
  @{ Name='nvngx_dlssg.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlssg.dll'; Hash='FF6E90EB78B827927DFF5B4ECC6B1C870C2E9BCA29ED9F48C7D348CC9E170B82'; Signature='Valid'; Size=7460976; Version='310.9.1.0'; Category='official-dlss-sdk-310.9.1-rel'; Experimental=$true },
  @{ Name='nvngx_dlssnr.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlssnr.dll'; Hash='F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC'; Signature='HashMismatch'; Size=165840496; Version='310.8.3.0'; Category='user-approved-community-Lecram-310.8.3-RTX50-runtime'; Experimental=$true },
  @{ Name='nvngx_dlssnr.dll'; Folder='runtime/experimental/nr-community'; Source='runtime_local/nvidia/nr-community/nvngx_dlssnr.dll'; Hash='984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014'; Signature='HashMismatch'; Size=165840496; Version='310.8.0.0'; Category='user-provided-community-modified-RTX40-RTX50-runtime'; Experimental=$true },
  @{ Name='nvngx_vsr.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_vsr.dll'; Hash='C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED'; Category='official-rtx-video-sdk-1.1.0'; Experimental=$false },
  @{ Name='libxell.dll'; Folder='runtime_local/intel/experimental'; Source='runtime_local/intel/experimental/libxell.dll'; Hash='D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7'; Category='official-intel-xess-sdk-3.0.2'; Experimental=$true },
  @{ Name='libxess_fg.dll'; Folder='runtime_local/intel/experimental'; Source='runtime_local/intel/experimental/libxess_fg.dll'; Hash='EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27'; Category='official-intel-xess-sdk-3.0.2'; Experimental=$true }
)
if ([version]$Version -ge [version]'1.3.1') {
  if ([version]$Version -ge [version]'2.0.0') {
    # New candidates ship exactly two NR versions. Historical 1.x manifests
    # keep their original identities; the retired40 binary is not recopied.
    $runtimeFiles = @($runtimeFiles | Where-Object { $_.Folder -ne 'runtime/experimental/nr-community' })
  }
  # AMD FidelityFX SDK 2.3.0 runtime (MIT licensed, AMD-signed). Needed for the
  # FSR frame-generation and FSR upscaling paths; loaded from runtime_local/amd/fidelityfx.
  $runtimeFiles += @(
    @{ Name='amd_fidelityfx_loader_dx12.dll'; Folder='runtime_local/amd/fidelityfx'; Source='runtime_local/amd/fidelityfx/amd_fidelityfx_loader_dx12.dll'; Hash='E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA'; Category='official-amd-fidelityfx-sdk-2.3.0-loader'; Experimental=$true },
    @{ Name='amd_fidelityfx_framegeneration_dx12.dll'; Folder='runtime_local/amd/fidelityfx'; Source='runtime_local/amd/fidelityfx/amd_fidelityfx_framegeneration_dx12.dll'; Hash='02297BEEDD285E822D3A64F314CF00FAF378DCEC0EDC47FF0C4DD71B3A8C2F18'; Category='official-amd-fidelityfx-sdk-2.3.0-framegeneration'; Experimental=$true },
    @{ Name='amd_fidelityfx_upscaler_dx12.dll'; Folder='runtime_local/amd/fidelityfx'; Source='runtime_local/amd/fidelityfx/amd_fidelityfx_upscaler_dx12.dll'; Hash='D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46'; Category='official-amd-fidelityfx-sdk-2.3.0-upscaler'; Experimental=$true }
  )
}
if ([version]$Version -ge [version]'1.1.1') {
  if ([version]$Version -ge [version]'2.0.0') {
    # SF-v2 is the RTX20-50 compatible choice; the disk slot name is historical.
    $runtimeFiles += @{ Name='nvngx_dlssnr.dll'; Folder='runtime/experimental/nr-ampere'; Source='runtime_local/nvidia/nr-ampere/nvngx_dlssnr.dll'; Hash='6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927'; Signature='NotSigned'; Size=165830144; Version='310.8.2.0'; Category='user-approved-community-SF-v2-310.8.2-RTX20-RTX50-compatible-runtime'; Experimental=$true }
  } else {
    $runtimeFiles += @{ Name='nvngx_dlssnr.dll'; Folder='runtime/experimental/nr-ampere'; Source='runtime_local/nvidia/nr-ampere/nvngx_dlssnr.dll'; Hash='DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F'; Signature='HashMismatch'; Size=165840496; Version='310.8.0.0'; Category='user-provided-NeuralScreen-1.8.2-modified-RTX30-experimental-runtime'; Experimental=$true }
  }
}
if ($LocalVideoHdr -or [version]$Version -ge [version]'1.4.2') {
  if ($LocalVideoHdr -and -not $Label) { throw 'Local HDR package requires an explicit test label.' }
  $runtimeFiles += @{ Name='nvngx_truehdr.dll'; Folder='runtime/experimental'; Source='third_party_local/nvidia/RTX_Video_SDK_1.1.0/bin/Windows/x64/rel/nvngx_truehdr.dll'; Hash='9A80575F247190C05FE80EAC0C4BAA1D0D4D932348F26808310B5EC4BF9EEB4B'; Size=3955752; Version='1.1.0.0'; Category='official-rtx-video-sdk-1.1.0'; Experimental=$true }
}
function Runtime-Source($Item) {
  if ([IO.Path]::IsPathRooted($Item.Source)) { return $Item.Source }
  return Join-Path $resolvedDependencies $Item.Source
}
if ([version]$Version -ge [version]'2.0.0') {
  # DLSS-G 310.9.1 optimized network kernels (DLSSG-Transfusion 1.4.5.3, see
  # THIRD_PARTY_NOTICES.md). Runtime data loaded from disk, not linked in.
  $kernelSource = Join-Path $resolvedDependencies 'third_party_local/nvidia/dlssg-transfusion-kernels-1.4.5.3'
  if (-not (Test-Path -LiteralPath $kernelSource -PathType Container)) { throw "DLSS-G optimized kernel set missing: $kernelSource" }
  foreach ($kernel in (Get-ChildItem -LiteralPath $kernelSource -Filter *.ptx | Sort-Object Name)) {
    $runtimeFiles += @{
      Name = $kernel.Name; Folder = 'runtime/experimental/dlssg-kernels'
      Source = "third_party_local/nvidia/dlssg-transfusion-kernels-1.4.5.3/$($kernel.Name)"
      Hash = (Get-FileHash -LiteralPath $kernel.FullName -Algorithm SHA256).Hash
      Signature = 'NotSigned'
      Category = 'community-DLSSG-Transfusion-1.4.5.3-sm86-optimized-kernels'
      Experimental = $true
    }
  }
}
$records = foreach ($item in $runtimeFiles) {
  $source = Runtime-Source $item
  $file = Get-Item -LiteralPath $source
  $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $sig = Get-AuthenticodeSignature -LiteralPath $source
  $expectedSignature = if ($item.ContainsKey('Signature')) { $item.Signature } else { 'Valid' }
  if ($hash -ne $item.Hash -or $sig.Status.ToString() -ne $expectedSignature) { throw "Publisher runtime identity rejected: $($item.Source)" }
  $v = $file.VersionInfo
  $versionText = "$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)"
  if (($item.ContainsKey('Size') -and $file.Length -ne $item.Size) -or ($item.ContainsKey('Version') -and $versionText -ne $item.Version)) { throw "Publisher runtime metadata rejected: $($item.Source)" }
  [pscustomobject][ordered]@{name=$item.Name;path="$($item.Folder)/$($item.Name)";size=$file.Length;sha256=$hash;fileVersion="$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)";authenticode=$sig.Status.ToString();signer=$sig.SignerCertificate.Subject;classification=$item.Category;experimental=$item.Experimental;removable=$true;source=$item.Category}
}
$allowed = [Collections.Generic.List[string]]::new()
function Copy-Payload([string]$Source,[string]$Relative) {
  $destination = Join-Path $stage $Relative
  [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $destination
  $allowed.Add($Relative.Replace('\','/'))
}
foreach ($name in $applicationFiles) {
  $destination = if ($name -eq 'veyra.exe') {'Veyra.exe'} else {$name}
  Copy-Payload (Join-Path $bin $name) $destination
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ''
$crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^\d+\.' | Sort-Object Name -Descending | Select-Object -First 1
foreach ($name in @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll')) {
  Copy-Payload (Join-Path $crt.FullName "x64/Microsoft.VC143.CRT/$name") $name
}
foreach ($name in @('LICENSE','README.md','README_EN.md','THIRD_PARTY_NOTICES.md')) { Copy-Payload (Join-Path $resolvedRoot $name) $name }
foreach ($image in Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'docs/images') -Recurse -File) {
  $relative = $image.FullName.Substring((Join-Path $resolvedRoot 'docs/images').Length+1).Replace('\','/')
  Copy-Payload $image.FullName "docs/images/$relative"
}
foreach ($name in @('BUILD.md',"RUNTIME_COMPONENTS_$Version.md","RELEASE_NOTES_$Version.md")) { Copy-Payload (Join-Path $resolvedRoot "docs/$name") "docs/$name" }
Copy-Payload (Join-Path $resolvedRoot "docs/RELEASE_NOTES_$Version.md") 'RELEASE_NOTES.md'
# Remote Play is statically linked. Retain all dependency notices and source instructions.
foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'licenses/remoteplay') -Recurse -File) {
  $relative = $notice.FullName.Substring((Join-Path $resolvedRoot 'licenses').Length+1).Replace('\','/')
  Copy-Payload $notice.FullName "licenses/$relative"
}
$remoteBuildDoc = if ([version]$Version -ge [version]'1.0.0') {"REMOTEPLAY_BUILD_$Version.md"} else {'REMOTEPLAY_BUILD_0.0.5.md'}
Copy-Payload (Join-Path $resolvedRoot "docs/$remoteBuildDoc") "docs/$remoteBuildDoc"
foreach ($shader in Get-ChildItem -LiteralPath (Join-Path $bin 'shaders') -Recurse -File -Filter '*.dxil') {
  $relative=$shader.FullName.Substring((Join-Path $bin 'shaders').Length+1).Replace('\','/')
  Copy-Payload $shader.FullName "shaders/$relative"
}
foreach ($name in @('LICENSE','NOTICE','PROVENANCE.md','VEYRA_INTEGRATION.md')) {
  Copy-Payload (Join-Path $resolvedRoot "third_party/gpu-dis/$name") "licenses/gpu-dis/$name"
}
foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $resolvedRoot 'third_party/gpu-dis/licenses') -File) {
  Copy-Payload $notice.FullName "licenses/gpu-dis/licenses/$($notice.Name)"
}
Copy-Payload (Join-Path $resolvedDependencies 'runtime_local/config/ngx-local.json') 'runtime/config/ngx-local.json'
Copy-Payload (Join-Path $ffmpegRoot 'share/ffmpeg/copyright') 'licenses/FFMPEG-COPYRIGHT.txt'
Copy-Payload (Join-Path $ffmpegRoot 'share/ffmpeg/vcpkg.spdx.json') 'licenses/FFMPEG-SPDX.json'
if ($applicationFiles -contains 'dav1d.dll') {
  Copy-Payload (Join-Path $ffmpegRoot 'share/dav1d/copyright') 'licenses/DAV1D-COPYRIGHT.txt'
  Copy-Payload (Join-Path $ffmpegRoot 'share/dav1d/vcpkg.spdx.json') 'licenses/DAV1D-SPDX.json'
}
$dav1dCopyright = Join-Path $ffmpegRoot 'share/dav1d/copyright'
$dav1dSpdx = Join-Path $ffmpegRoot 'share/dav1d/vcpkg.spdx.json'
if (Test-Path -LiteralPath $dav1dCopyright -PathType Leaf) { Copy-Payload $dav1dCopyright 'licenses/DAV1D-COPYRIGHT.txt' }
if (Test-Path -LiteralPath $dav1dSpdx -PathType Leaf) { Copy-Payload $dav1dSpdx 'licenses/DAV1D-SPDX.json' }
$ffmpegLocalBuild = Join-Path $ffmpegRoot 'share/ffmpeg/veyra-local-build.json'
if (Test-Path -LiteralPath $ffmpegLocalBuild) {
  $ffmpegBuild = Get-Content -LiteralPath $ffmpegLocalBuild -Raw | ConvertFrom-Json
  foreach ($file in $ffmpegBuild.files) {
    if ($file.name -notin $applicationFiles) { throw 'Unexpected FFmpeg build manifest entry' }
    if ((Get-FileHash -LiteralPath (Join-Path $bin $file.name) -Algorithm SHA256).Hash -ne $file.sha256) { throw "FFmpeg publisher provenance mismatch: $($file.name)" }
  }
  Copy-Payload $ffmpegLocalBuild 'licenses/FFMPEG-VEYRA-BUILD.json'
}
Copy-Payload (Join-Path $resolvedRoot 'assets/icons/lucide/LICENSE') 'licenses/LUCIDE-LICENSE.txt'
Copy-Payload (Join-Path $resolvedRoot 'licenses/WIN32_CAPTURE_SAMPLE_MIT.txt') 'licenses/WIN32_CAPTURE_SAMPLE_MIT.txt'
Copy-Payload (Join-Path $resolvedRoot 'licenses/capture/ELGATO_NITLINK_MIT.txt') 'licenses/capture/ELGATO_NITLINK_MIT.txt'
foreach ($name in @('RTX40MFG_LICENSE.txt','HDE_LICENSE.txt')) {
  Copy-Payload (Join-Path $resolvedRoot "src/ngx/compat/$name") "licenses/$name"
}
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/nvidia/DLSS_repo/LICENSE.txt') 'licenses/NVIDIA_RTX_SDK_LICENSE.txt'
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/nvidia/RTX_Video_SDK_1.1.0/NVIDIA_RTX_Video_SDK_License.pdf') 'licenses/NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf'
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/nvidia/Optical_Flow_SDK_5.0.7/LicenseAgreement.pdf') 'licenses/NVIDIA_OPTICAL_FLOW_SDK_LICENSE.pdf'
if ([version]$Version -ge [version]'1.3.1') {
  # MIT licence of the FidelityFX SDK that ships the AMD runtime above.
  $amdLicense = Join-Path $resolvedDependencies 'third_party_local/amd/FidelityFX-SDK-2.3.0/docs/license.md'
  if (-not (Test-Path -LiteralPath $amdLicense -PathType Leaf)) {
    $amdLicense = Join-Path $resolvedDependencies 'third_party_local/amd/FidelityFX-SDK-2.3.0/Kits/FidelityFX/docs/license.md'
  }
  if (Test-Path -LiteralPath $amdLicense -PathType Leaf) {
    Copy-Payload $amdLicense 'licenses/AMD-FIDELITYFX-LICENSE.txt'
  } else {
    throw 'AMD FidelityFX licence text not found in the vendored SDK'
  }
}
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/intel/xess-3.0.2/LICENSE.txt') 'licenses/INTEL_XESS_LICENSE.txt'
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/intel/xess-3.0.2/third-party-programs.txt') 'licenses/INTEL_THIRD_PARTY_PROGRAMS.txt'
Copy-Payload (Join-Path $resolvedDependencies 'third_party_local/amd/FidelityFX-SDK/LICENSE.txt') 'licenses/AMD_FIDELITYFX_LICENSE.txt'
foreach ($item in $runtimeFiles) { Copy-Payload (Runtime-Source $item) "$($item.Folder)/$($item.Name)" }
if ($LocalVideoHdr) {
  Copy-Payload (Join-Path $resolvedRoot "docs/RELEASE_NOTES_$Version.md") 'LOCAL_TEST.md'
}
$manifestFolders = @('runtime/experimental','runtime_local/intel/experimental')
if ([version]$Version -ge [version]'1.3.1') { $manifestFolders += 'runtime_local/amd/fidelityfx' }
foreach ($folder in $manifestFolders) {
  $relative = "$folder/release-runtime-manifest.json"
  [ordered]@{schema=1;package="Veyra $Version$Label";mode=$(if($LocalVideoHdr){'local-evaluation-only'}else{'user-authorized-runtime-pack'});enforcedAtRuntime=$false;warning='Community experimental integration; not vendor certification or endorsement.';files=@($records | Where-Object {$_.path.StartsWith("$folder/")})} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage $relative) -Encoding UTF8
  $allowed.Add($relative)
}
$payload = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
$forbidden = '\.(pdb|lib|obj|h|hpp|cpp|c|zip|pth|onnx|log|mp4|partial|addon64)$'
foreach ($file in $payload) {
  $relative = $file.FullName.Substring($stage.Length+1).Replace('\','/')
  if ($relative -notin $allowed -or $relative -match $forbidden -or $relative -match '(^|/)(third_party_local|logs|captures|\.git)/' -or $relative -match '(^|/)(last-applied\.v1|ui-preferences\.v1|presets\.v1|veyra\.ini)$') { throw "Unexpected payload: $relative" }
}
$fileManifest = @($payload | ForEach-Object {[ordered]@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
[ordered]@{schema=1;version=$Version;files=$fileManifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Encoding UTF8
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
"$archiveHash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
[ordered]@{archive=$archive;sha256=$archiveHash;files=$payload.Count+1;runtime=$records;forbiddenFiles=0} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resolvedOutput 'package-audit.json') -Encoding UTF8
Get-Item -LiteralPath $archive | Select-Object FullName,Length
Write-Output "SHA256 $archiveHash"
