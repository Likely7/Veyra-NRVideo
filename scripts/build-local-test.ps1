#Requires -Version 5.1
<#
.SYNOPSIS
  Builds a local test package of this branch on Windows.

.DESCRIPTION
  Compiles veyra_qml_ui.exe from this checkout and drops it, with the QML
  interface files, into a copy of the official Veyra 2.1.5 portable package.
  Every runtime component (Qt, FFmpeg, NVIDIA/AMD/Intel runtimes, models,
  shaders) is the official package's own, so only the program changes.

  It downloads what it can: Qt 6.8.3 (aqtinstall), the public DLSS 310.7,
  nv-codec-headers 13.0, XeSS 3.0.2 and FidelityFX 2.3.0 headers, FFmpeg n9.0.1
  headers, and the official package. FFmpeg import libraries are generated from
  the official package's own DLLs. The NVIDIA Optical Flow SDK needs an NVIDIA
  developer login, so it is passed in with -NvofSdk.

  Everything goes under -WorkDir; the checkout itself is not modified. Remote
  Play, PC (Moonlight) and Xbox streaming, ASS effect subtitles, Magewell and
  DeckLink capture and AMD optical flow are not built; see
  docs/BUILD_LOCAL_TEST.md.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build-local-test.ps1 -NvofSdk C:\Downloads\Optical_Flow_SDK_5.0.7.zip
#>
[CmdletBinding()]
param(
    # Optical Flow SDK zip or extracted folder (developer.nvidia.com/opticalflow-sdk).
    [Parameter(Mandatory = $true)][string]$NvofSdk,
    # Which official package to start from. The program is the same for both.
    [ValidateSet('NVIDIA', 'AMD')][string]$Gpu = 'NVIDIA',
    # ASCII path without spaces: all downloads, dependencies, build and output.
    [string]$WorkDir = 'C:\veyra-build',
    # The official Veyra-2.1.5-<Gpu>-win64-portable.zip if already downloaded.
    [string]$PackageZip,
    # Folder holding cuda.h (enables NVIDIA VFG frame generation). Defaults to the
    # installed CUDA Toolkit, then to NVIDIA's public CUDA runtime headers.
    [string]$CudaInclude,
    # Original VBCABLE_Driver_Pack46.zip, embedded for the offline driver install.
    [string]$VbCableZip,
    # Also write a .zip of the finished package.
    [switch]$Zip,
    # Delete the previous build tree first.
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$Version = '2.1.5'
$QtVersion = '6.8.3'
$Repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Step([string]$Text) { Write-Host ''; Write-Host ('==> ' + $Text) -ForegroundColor Cyan }
function Fail([string]$Text) { Write-Host ''; Write-Host ('build-local-test: ' + $Text) -ForegroundColor Red; exit 1 }
function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { Fail ('{0} {1} exited with {2}' -f $File, ($Arguments -join ' '), $LASTEXITCODE) }
}
function Test-PlainPath([string]$Path) { return $Path -match '^[A-Za-z]:\\[\x21-\x7E\\]*$' }
function CMakePath([string]$Path) { return $Path.Replace('\', '/') }
function Get-File([string]$Url, [string]$Target) {
    if (Test-Path -LiteralPath $Target -PathType Leaf) { return }
    Write-Host ('    download ' + $Url)
    $partial = $Target + '.partial'
    & curl.exe -L --fail --retry 3 -C - -o $partial $Url
    if ($LASTEXITCODE -ne 0) { Fail ('download failed: ' + $Url) }
    Move-Item -LiteralPath $partial -Destination $Target
}
function Expand-Zip([string]$Archive, [string]$Target) {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
    & tar.exe -xf $Archive -C $Target
    if ($LASTEXITCODE -ne 0) {
        Write-Host '    tar could not unpack it; using Expand-Archive'
        Expand-Archive -LiteralPath $Archive -DestinationPath $Target -Force
    }
}
# A shallow (and optionally sparse) clone of one tag; Probe is a file that exists
# once the checkout is complete, so an interrupted clone is redone.
function Get-GitTree([string]$Url, [string]$Tag, [string]$Dir, [string]$Probe, [string[]]$Paths) {
    if (Test-Path -LiteralPath (Join-Path $Dir $Probe) -PathType Leaf) { return }
    if (Test-Path -LiteralPath $Dir) { Remove-Item -LiteralPath $Dir -Recurse -Force }
    if ($Paths) {
        Invoke-Checked 'git' @('clone', '--quiet', '--depth', '1', '--branch', $Tag, '--filter=blob:none', '--sparse', $Url, $Dir)
        Invoke-Checked 'git' (@('-C', $Dir, 'sparse-checkout', 'set') + $Paths)
    } else {
        Invoke-Checked 'git' @('clone', '--quiet', '--depth', '1', '--branch', $Tag, $Url, $Dir)
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Dir $Probe) -PathType Leaf)) { Fail ('{0} {1} has no {2}' -f $Url, $Tag, $Probe) }
}

# --- Paths and tools ---------------------------------------------------------
Step 'Checking paths and tools'
if (-not (Test-PlainPath $WorkDir)) { Fail ('-WorkDir must be an absolute ASCII path without spaces, e.g. C:\veyra-build (got {0})' -f $WorkDir) }
# link.exe reads its response files in the ANSI code page, so non-ASCII source
# paths break the link (see CMakeLists.txt, veyra_qml_files).
if (-not (Test-PlainPath $Repo)) { Fail ('Clone the repository to an ASCII path without spaces, e.g. C:\src\Veyra-NRVideo (this checkout is at {0})' -f $Repo) }
$Downloads = Join-Path $WorkDir 'downloads'
$Deps = Join-Path $WorkDir 'deps'
$Build = Join-Path $WorkDir 'build'
foreach ($dir in @($WorkDir, $Downloads, $Deps)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

foreach ($tool in @('git', 'curl.exe', 'tar.exe', 'robocopy.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail ($tool + ' not found. Install Git for Windows (git-scm.com); curl/tar/robocopy come with Windows 10/11.') }
}
$pyExe = $null; $pyArgs = @()
foreach ($candidate in @(@('py', '-3'), @('python'))) {
    if (-not (Get-Command $candidate[0] -ErrorAction SilentlyContinue)) { continue }
    $rest = @($candidate | Select-Object -Skip 1)
    # A missing interpreter (or the Microsoft Store alias) writes to stderr, which
    # Windows PowerShell turns into an error under 'Stop'.
    $ErrorActionPreference = 'Continue'
    $answer = & $candidate[0] @rest -c 'import sys; print(1 if sys.version_info >= (3, 9) else 0)' 2>$null
    $ErrorActionPreference = 'Stop'
    if (("$answer").Trim() -eq '1') { $pyExe = $candidate[0]; $pyArgs = $rest; break }
}
if (-not $pyExe) { Fail 'Python 3.9+ not found. Install it from python.org (tick "Add python.exe to PATH").' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { Fail 'Visual Studio 2022 not found. Install it (Community is fine) with "Desktop development with C++".' }
$vs = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) | Select-Object -First 1
if (-not $vs) { Fail 'Visual Studio has no x64 C++ tools. Add the "Desktop development with C++" workload in the Visual Studio Installer.' }
$devShell = Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path -LiteralPath $devShell)) { Fail ('Visual Studio developer shell module missing: ' + $devShell) }
Import-Module $devShell
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
foreach ($tool in @('cl.exe', 'dumpbin.exe', 'lib.exe', 'cmake.exe', 'ninja.exe')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Fail ($tool + ' not found in the Visual Studio environment. In the Visual Studio Installer add "C++ CMake tools for Windows" and a Windows 11 SDK.')
    }
}
$cmake = (Get-Command cmake.exe).Source
$ninja = (Get-Command ninja.exe).Source
$ErrorActionPreference = 'Continue'
$commit = "$(& git -C $Repo rev-parse --short HEAD 2>$null)".Trim()
$branch = "$(& git -C $Repo rev-parse --abbrev-ref HEAD 2>$null)".Trim()
$ErrorActionPreference = 'Stop'
if (-not $commit) { $commit = 'unknown'; $branch = 'not a git checkout' }
Write-Host ('    repository {0} @ {1}' -f $Repo, $commit)
Write-Host ('    Visual Studio {0}' -f $vs)

# --- Official package ----------------------------------------------------------
Step ('Official Veyra {0} {1} package' -f $Version, $Gpu)
$packageName = 'Veyra-{0}-{1}-win64-portable' -f $Version, $Gpu
if (-not $PackageZip) {
    $PackageZip = Join-Path $Downloads ($packageName + '.zip')
    Get-File ('https://github.com/Likely7/Veyra-NRVideo/releases/download/v{0}/{1}.zip' -f $Version, $packageName) $PackageZip
}
if (-not (Test-Path -LiteralPath $PackageZip -PathType Leaf)) { Fail ('package not found: ' + $PackageZip) }
$official = Join-Path $WorkDir ('official-' + $Gpu)
if (-not (Get-ChildItem -LiteralPath $official -Recurse -Filter 'veyra_qml_ui.exe' -ErrorAction SilentlyContinue)) { Expand-Zip $PackageZip $official }
$officialExe = Get-ChildItem -LiteralPath $official -Recurse -Filter 'veyra_qml_ui.exe' | Select-Object -First 1
if (-not $officialExe) { Fail ('veyra_qml_ui.exe not found in ' + $PackageZip) }
$AppRoot = $officialExe.Directory.FullName
Write-Host ('    {0}' -f $AppRoot)

# --- Qt ------------------------------------------------------------------------
Step ('Qt {0} (msvc2022_64)' -f $QtVersion)
$qtBase = Join-Path $Deps 'qt'
$Qt = Join-Path $qtBase ($QtVersion + '\msvc2022_64')
if (-not (Test-Path -LiteralPath (Join-Path $Qt 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    Invoke-Checked $pyExe ($pyArgs + @('-m', 'pip', 'install', '--user', '--upgrade', '--quiet', 'aqtinstall'))
    Invoke-Checked $pyExe ($pyArgs + @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $QtVersion, 'win64_msvc2022_64', '--outputdir', $qtBase))
}

# --- Public SDK headers ----------------------------------------------------------
Step 'Public SDK headers (DLSS 310.7, nv-codec-headers 13.0, XeSS 3.0.2, FidelityFX 2.3.0)'
$Dlss = Join-Path $Deps 'dlss-310.7.0'
Get-GitTree 'https://github.com/NVIDIA/DLSS' 'v310.7.0' $Dlss 'lib\Windows_x86_64\x64\nvsdk_ngx_s.lib' @('include', 'lib/Windows_x86_64/x64')
$Nvenc = Join-Path $Deps 'nv-codec-headers-13.0'
Get-GitTree 'https://github.com/FFmpeg/nv-codec-headers' 'n13.0.19.0' $Nvenc 'include\ffnvcodec\nvEncodeAPI.h' @()
$Xess = Join-Path $Deps 'xess-3.0.2'
Get-GitTree 'https://github.com/intel/xess' 'v3.0.2' $Xess 'inc\xess_fg\xefg_swapchain_d3d12.h' @('inc')
$Ffx2Clone = Join-Path $Deps 'fidelityfx-2.3.0'
Get-GitTree 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK' 'v2.3.0' $Ffx2Clone 'Kits\FidelityFX\framegeneration\include\dx12\ffx_api_framegeneration_dx12.h' @('Kits/FidelityFX/api/include', 'Kits/FidelityFX/framegeneration/include', 'Kits/FidelityFX/upscalers/include')
$Ffx2 = Join-Path $Ffx2Clone 'Kits\FidelityFX'

# --- Optical Flow SDK ------------------------------------------------------------
Step 'NVIDIA Optical Flow SDK'
$nvofSearch = $NvofSdk
if (Test-Path -LiteralPath $NvofSdk -PathType Leaf) {
    $nvofSearch = Join-Path $Deps 'optical-flow-sdk'
    if (-not (Get-ChildItem -LiteralPath $nvofSearch -Recurse -Filter 'nvOpticalFlowD3D12.h' -ErrorAction SilentlyContinue)) { Expand-Zip $NvofSdk $nvofSearch }
} elseif (-not (Test-Path -LiteralPath $NvofSdk -PathType Container)) {
    Fail ('-NvofSdk not found: ' + $NvofSdk)
}
$nvofHeader = Get-ChildItem -LiteralPath $nvofSearch -Recurse -Filter 'nvOpticalFlowD3D12.h' | Select-Object -First 1
if (-not $nvofHeader) { Fail 'nvOpticalFlowD3D12.h not found: -NvofSdk must be Optical Flow SDK 3.0 or later (5.0.7 is what the release used).' }
$Nvof = $nvofHeader.Directory.Parent.FullName
if (-not (Test-Path -LiteralPath (Join-Path $Nvof 'NvOFInterface\nvOpticalFlowCommon.h'))) { Fail ('unexpected Optical Flow SDK layout under ' + $Nvof) }
Write-Host ('    {0}' -f $Nvof)

# --- CUDA driver header (optional) -------------------------------------------------
Step 'CUDA driver header (optional, for NVIDIA VFG)'
$Cuda = $null
if ($CudaInclude) {
    if (-not (Test-Path -LiteralPath (Join-Path $CudaInclude 'cuda.h'))) { Fail ('no cuda.h in -CudaInclude ' + $CudaInclude) }
    $Cuda = $CudaInclude
} elseif ($env:CUDA_PATH -and (Test-Path -LiteralPath (Join-Path $env:CUDA_PATH 'include\cuda.h'))) {
    $Cuda = Join-Path $env:CUDA_PATH 'include'
} else {
    try {
        $redist = 'https://developer.download.nvidia.com/compute/cuda/redist/'
        $meta = Invoke-RestMethod ($redist + 'redistrib_12.6.3.json')
        $relative = $meta.cuda_cudart.'windows-x86_64'.relative_path
        $archive = Join-Path $Downloads (Split-Path $relative -Leaf)
        Get-File ($redist + $relative) $archive
        $cudaTree = Join-Path $Deps 'cuda-cudart'
        if (-not (Get-ChildItem -LiteralPath $cudaTree -Recurse -Filter 'cuda.h' -ErrorAction SilentlyContinue)) { Expand-Zip $archive $cudaTree }
        $header = Get-ChildItem -LiteralPath $cudaTree -Recurse -Filter 'cuda.h' | Select-Object -First 1
        if ($header) { $Cuda = $header.Directory.FullName }
    } catch {
        Write-Host ('    could not fetch CUDA headers: ' + $_.Exception.Message)
    }
}
if ($Cuda) { Write-Host ('    {0}' -f $Cuda) } else { Write-Warning 'No cuda.h: NVIDIA VFG frame generation will be unavailable in this build. Install the CUDA Toolkit or pass -CudaInclude to enable it.' }

# --- FFmpeg headers and import libraries -----------------------------------------
# Headers from the same FFmpeg n9.0.1 release; import libraries generated from the
# official package's own DLLs, so the program links exactly against what it ships with.
Step 'FFmpeg n9.0.1 headers and import libraries from the official DLLs'
$ffZip = Join-Path $Downloads 'FFmpeg-n9.0.1.zip'
Get-File 'https://codeload.github.com/FFmpeg/FFmpeg/zip/refs/tags/n9.0.1' $ffZip
$ffTree = Join-Path $Deps 'ffmpeg-src'
$ffSource = Join-Path $ffTree 'FFmpeg-n9.0.1'
if (-not (Test-Path -LiteralPath (Join-Path $ffSource 'libavcodec\avcodec.h'))) { Expand-Zip $ffZip $ffTree }
if (-not (Test-Path -LiteralPath (Join-Path $ffSource 'libavcodec\avcodec.h'))) { Fail ('unexpected FFmpeg source layout in ' + $ffZip) }
$Ffmpeg = Join-Path $Deps 'ffmpeg-dev'
if (Test-Path -LiteralPath $Ffmpeg) { Remove-Item -LiteralPath $Ffmpeg -Recurse -Force }
$ffInclude = Join-Path $Ffmpeg 'include'; $ffLib = Join-Path $Ffmpeg 'lib'; $ffShare = Join-Path $Ffmpeg 'share\ffmpeg'
foreach ($dir in @($ffInclude, $ffLib, $ffShare)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$libraries = @()
foreach ($name in @('avformat', 'avcodec', 'avfilter', 'swresample', 'swscale', 'avutil')) {
    $dll = Get-ChildItem -LiteralPath $AppRoot -Filter ('{0}-*.dll' -f $name) -File | Select-Object -First 1
    if (-not $dll) { $dll = Get-ChildItem -LiteralPath $AppRoot -Recurse -Filter ('{0}-*.dll' -f $name) -File | Select-Object -First 1 }
    if (-not $dll) {
        if ($name -eq 'avfilter') { continue }
        Fail ('{0}-*.dll not found in the official package' -f $name)
    }
    $headers = Join-Path $ffInclude ('lib' + $name)
    New-Item -ItemType Directory -Force -Path $headers | Out-Null
    Copy-Item -Path (Join-Path $ffSource ('lib{0}\*.h' -f $name)) -Destination $headers
    $exports = @()
    foreach ($line in (& dumpbin.exe /nologo /exports $dll.FullName)) {
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)\s*$') { $exports += $Matches[1] }
    }
    if ($exports.Count -lt 10) { Fail ('could not read the exports of ' + $dll.FullName) }
    $def = Join-Path $ffLib ($name + '.def')
    Set-Content -LiteralPath $def -Encoding ASCII -Value (@(('LIBRARY "{0}"' -f $dll.Name), 'EXPORTS') + ($exports | ForEach-Object { '    ' + $_ }))
    Invoke-Checked 'lib.exe' @('/nologo', '/machine:x64', ('/def:' + $def), ('/out:' + (Join-Path $ffLib ($name + '.lib'))))
    $libraries += $name
    Write-Host ('    {0}: {1} exports' -f $dll.Name, $exports.Count)
}
# Generated by FFmpeg's configure; the public headers include them.
Set-Content -LiteralPath (Join-Path $ffInclude 'libavutil\avconfig.h') -Encoding ASCII -Value @(
    '#ifndef AVUTIL_AVCONFIG_H', '#define AVUTIL_AVCONFIG_H', '#define AV_HAVE_BIGENDIAN 0', '#define AV_HAVE_FAST_UNALIGNED 1', '#endif')
Set-Content -LiteralPath (Join-Path $ffInclude 'libavutil\ffversion.h') -Encoding ASCII -Value @(
    '#ifndef AVUTIL_FFVERSION_H', '#define AVUTIL_FFVERSION_H', '#define FFMPEG_VERSION "n9.0.1"', '#endif')
# CMakeLists.txt calls find_package(FFMPEG) with <root>/share/ffmpeg on the module path.
$find = @(
    '# Generated by scripts/build-local-test.ps1: headers + import libraries for the official DLLs.',
    'get_filename_component(_veyra_ffmpeg_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)',
    'set(FFMPEG_INCLUDE_DIRS "${_veyra_ffmpeg_root}/include")',
    'set(FFMPEG_LIBRARY_DIRS "${_veyra_ffmpeg_root}/lib")',
    'set(FFMPEG_LIBRARIES "")')
foreach ($name in $libraries) { $find += ('list(APPEND FFMPEG_LIBRARIES "${{_veyra_ffmpeg_root}}/lib/{0}.lib")' -f $name) }
$find += 'set(FFMPEG_FOUND TRUE)'
Set-Content -LiteralPath (Join-Path $ffShare 'FindFFMPEG.cmake') -Encoding ASCII -Value $find
$deinterlace = if ($libraries -contains 'avfilter') { 'ON' } else { 'OFF' }

# --- Configure and build ------------------------------------------------------------
Step 'Configuring'
if ($Clean -and (Test-Path -LiteralPath $Build)) { Remove-Item -LiteralPath $Build -Recurse -Force }
$configure = @(
    '-S', $Repo, '-B', $Build, '-G', 'Ninja',
    ('-DCMAKE_MAKE_PROGRAM=' + (CMakePath $ninja)),
    '-DCMAKE_BUILD_TYPE=Release',
    ('-DCMAKE_PREFIX_PATH=' + (CMakePath $Qt)),
    ('-DVEYRA_DISPLAY_VERSION={0}-vdesk-test' -f $Version),
    '-DVEYRA_BUILD_QML_UI=ON',
    '-DVEYRA_ENABLE_D3D12_DEBUG=OFF',
    '-DVEYRA_ENABLE_REMOTEPLAY=OFF', '-DVEYRA_ENABLE_MOONLIGHT=OFF', '-DVEYRA_ENABLE_XBOX=OFF',
    ('-DVEYRA_FFMPEG_ROOT=' + (CMakePath $Ffmpeg)),
    ('-DVEYRA_ENABLE_DEINTERLACE=' + $deinterlace),
    '-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON',
    ('-DVEYRA_DLSS_SDK_ROOT=' + (CMakePath $Dlss)),
    ('-DVEYRA_NVOF_SDK_ROOT=' + (CMakePath $Nvof)),
    ('-DVEYRA_NVENC_HEADERS_ROOT=' + (CMakePath $Nvenc)),
    ('-DVEYRA_XESS_ROOT=' + (CMakePath $Xess)),
    ('-DVEYRA_FIDELITYFX2_ROOT=' + (CMakePath $Ffx2)))
if ($Cuda) { $configure += ('-DVEYRA_CUDA_DRIVER_INCLUDE_DIR=' + (CMakePath $Cuda)) }
if ($VbCableZip) { $configure += ('-DVEYRA_VB_CABLE_PACKAGE=' + (CMakePath (Resolve-Path -LiteralPath $VbCableZip).Path)) }
Invoke-Checked $cmake $configure
# Qt is looked up QUIETly; without it the program target silently does not exist.
if (-not (Select-String -LiteralPath (Join-Path $Build 'build.ninja') -SimpleMatch 'veyra_qml_ui.exe' -Quiet)) {
    Fail ('the veyra_qml_ui target was not configured; check that Qt was found under ' + $Qt)
}

Step 'Building veyra_qml_ui (this takes a while)'
Invoke-Checked $cmake @('--build', $Build, '--target', 'veyra_qml_ui')
$builtExe = Join-Path $Build 'veyra_qml_ui.exe'
if (-not (Test-Path -LiteralPath $builtExe)) { Fail ('build finished without ' + $builtExe) }

# --- Package --------------------------------------------------------------------------
Step 'Assembling the test package'
$outName = 'Veyra-{0}-vdesk-test-{1}' -f $Version, $Gpu
$Output = Join-Path $WorkDir ('package\' + $outName)
if (Test-Path -LiteralPath $Output) { Remove-Item -LiteralPath $Output -Recurse -Force }
& robocopy.exe $AppRoot $Output /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail ('copying the official package failed (robocopy {0})' -f $LASTEXITCODE) }
Copy-Item -LiteralPath $builtExe -Destination (Join-Path $Output 'veyra_qml_ui.exe') -Force
& robocopy.exe (Join-Path $Repo 'qml') (Join-Path $Output 'qml') /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail ('copying the QML files failed (robocopy {0})' -f $LASTEXITCODE) }
$notes = @(
    ('Veyra {0} virtual desktop capture test build' -f $Version),
    ('Source: {0} (commit {1})' -f $branch, $commit),
    ('Built: {0}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm')),
    ('Base package: {0}' -f (Split-Path $PackageZip -Leaf)),
    'Replaced: veyra_qml_ui.exe and qml\ (everything else is the official package).',
    'Not in this build: PS5 Remote Play, PC (Moonlight) and Xbox streaming, ASS effect subtitles, Magewell and DeckLink capture, AMD optical flow.',
    ('NVIDIA VFG: {0}; deinterlacing: {1}; offline VB-CABLE install: {2}' -f $(if ($Cuda) { 'built' } else { 'not built (no cuda.h)' }), $deinterlace, $(if ($VbCableZip) { 'built' } else { 'not built' })),
    'Settings are kept in runtime_local\ inside this folder, separate from other Veyra copies.')
Set-Content -LiteralPath (Join-Path $Output 'TEST-BUILD.txt') -Encoding UTF8 -Value $notes
if ($Zip) {
    $zipPath = $Output + '.zip'
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Invoke-Checked 'tar.exe' @('-a', '-c', '-f', $zipPath, '-C', (Split-Path $Output -Parent), $outName)
}

Step 'Done'
Write-Host ('    Test package: {0}' -f $Output) -ForegroundColor Green
Write-Host ('    Start it with {0}' -f (Join-Path $Output 'veyra_qml_ui.exe')) -ForegroundColor Green
if ($Zip) { Write-Host ('    Zip: {0}' -f ($Output + '.zip')) -ForegroundColor Green }
