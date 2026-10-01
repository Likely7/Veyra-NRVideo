[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet("x64-debug", "x64-release")][string]$Preset,
    [string]$BuildDirectory,
    [switch]$RemotePlay,
    [string]$ChiakiCheckout,
    [string]$ChiakiStage,
    [string]$RemotePlayPrefixPath,
    [string]$ProtocPath,
    [string]$PkgConfigPath,
    [string]$FfmpegRoot,
    [switch]$Clean
)

# Veyra build wrapper: resolves the MSVC environment on this machine and runs
# the tracked CMake presets. Keeps machine-specific paths out of the presets.
# Writes only under the gitignored out/ directory.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath (Join-Path $Root "CMakePresets.json") -PathType Leaf)) {
    Write-Host "build.ps1: CMakePresets.json not found under $Root"
    exit 2
}

# --- Resolve Visual Studio installation (vswhere first, known path fallback)
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = ""
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vsRoot = (& $vswhere -latest -version "[17,18)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ""
}
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    if (Test-Path -LiteralPath $fallback -PathType Container) { $vsRoot = $fallback }
}
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
    Write-Host "build.ps1: Visual Studio 2022 VC tools not found"
    exit 3
}

# Prefer the standalone system cmake over the VS-bundled cmake — the bundled
# cmake in VS 18 Preview has an incomplete share/cmake-X.Y modules directory.
$cmakeExe = ""
$found = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -ne $found) { $cmakeExe = $found.Source }
if ([string]::IsNullOrWhiteSpace($cmakeExe)) {
    $vsCmake = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $vsCmake -PathType Leaf) { $cmakeExe = $vsCmake }
}
if ([string]::IsNullOrWhiteSpace($cmakeExe)) {
    Write-Host "build.ps1: cmake not found"
    exit 3
}

$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    Write-Host "build.ps1: vcvars64.bat not found under $vsRoot"
    exit 3
}

$buildDir = Join-Path $Root ("out\build\" + $Preset)
if ($BuildDirectory) { $buildDir = [IO.Path]::GetFullPath($BuildDirectory) }
if ($Clean -and (Test-Path -LiteralPath $buildDir -PathType Container)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

# Local experimental DLSSNR support: enable when the staged official SDK tree
# exists; the CMake configure itself fail-closes on missing paths.
$configureExtra = ""
$sdkRoot = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0"
if (Test-Path -LiteralPath (Join-Path $sdkRoot "include\nvsdk_ngx.h") -PathType Leaf) {
    $configureExtra = ' -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT="{0}"' -f $sdkRoot
}
else {
    $configureExtra = " -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=OFF"
}

# FFmpeg dependency roots (C:\veyra-deps: the project path contains spaces,
# which FFmpeg's build refuses; see loop/JOURNAL.md).
$ffmpegRoot = if ($FfmpegRoot) { [IO.Path]::GetFullPath($FfmpegRoot) } else { "C:\veyra-deps\installed\x64-windows" }
if (Test-Path -LiteralPath (Join-Path $ffmpegRoot "include\libavformat\avformat.h") -PathType Leaf) {
    $configureExtra = $configureExtra + (' -DVEYRA_FFMPEG_ROOT="{0}"' -f $ffmpegRoot)
}
$clipToolsRoot = "C:\veyra-deps\tools-installed\x64-windows"
if (Test-Path -LiteralPath (Join-Path $clipToolsRoot "include\libavcodec\avcodec.h") -PathType Leaf) {
    $configureExtra = $configureExtra + (' -DVEYRA_CLIP_TOOLS_ROOT="{0}"' -f $clipToolsRoot)
}
$configureExtra += ' -DVEYRA_NVOF_SDK_ROOT="C:/tools/Optical_Flow_SDK_5.0.7"'
$configureExtra += ' -DVEYRA_XESS_ROOT="C:/tools/XeSS_302"'
$configureExtra += ' -DVEYRA_FIDELITYFX_ROOT="C:/veyra-deps/FFX_SDK-114-MT/sdk"'

# Remote Play is explicit: no dangling source/backend combination and no
# silently substituted prebuilt Chiaki library. Shared CMake verifies the pin
# and complete reviewed patches before compiling the dependency from source.
$configureExtra += " -DVEYRA_ENABLE_REMOTEPLAY=" + $(if($RemotePlay){"ON"}else{"OFF"})
if ($RemotePlay) {
    foreach ($path in @($ChiakiCheckout,$ChiakiStage,$RemotePlayPrefixPath,$ProtocPath,$PkgConfigPath)) {
        if (-not $path -or -not (Test-Path -LiteralPath $path)) { throw "Remote Play requires existing ChiakiCheckout, ChiakiStage, RemotePlayPrefixPath, ProtocPath and PkgConfigPath." }
    }
    $configureExtra += ' -DVEYRA_RP_CHIAKI_VERIFY_DIR="{0}" -DVEYRA_RP_CHIAKI_SOURCE_DIR="{1}" -DCMAKE_PREFIX_PATH="{2}" -DPROTOC="{3}" -DPKG_CONFIG_EXECUTABLE="{4}"' -f $ChiakiCheckout,$ChiakiStage,$RemotePlayPrefixPath,$ProtocPath,$PkgConfigPath
}

# --- Configure + build inside one vcvars environment, from a temp batch file
$batchDir = Join-Path $Root "out\build"
New-Item -ItemType Directory -Force -Path $batchDir | Out-Null
$batchFile = Join-Path $batchDir ("veyra-build-" + $Preset + ".cmd")

$batchLines = @(
    '@echo off',
    ('call "{0}" >nul 2>&1' -f $vcvars),
    ('if errorlevel 1 exit /b 4' -f $null),
    'chcp 65001 >nul',
    ('cd /d "{0}"' -f $Root),
    ('"{0}" --preset {1} -B "{2}"{3}' -f $cmakeExe, $Preset, $buildDir, $configureExtra),
    'if errorlevel 1 exit /b 5',
    ('"{0}" --build "{1}"' -f $cmakeExe, $buildDir),
    'if errorlevel 1 exit /b 6',
    'exit /b 0'
)
Set-Content -LiteralPath $batchFile -Value $batchLines -Encoding ASCII

& cmd.exe /c ('"{0}"' -f $batchFile)
$exitCode = $LASTEXITCODE
if ($exitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $buildDir 'veyra.exe'))) {
    foreach ($name in @('avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')) {
        Copy-Item -LiteralPath (Join-Path $ffmpegRoot "bin/$name") -Destination $buildDir
    }
    # AV1 software decoding is optional at the FFmpeg build level.  When the
    # selected prefix was built with libdav1d, keep its app-local dependency
    # beside avcodec; never silently copy a system or unrelated dav1d DLL.
    $dav1d = Join-Path $ffmpegRoot 'bin/dav1d.dll'
    $stagedDav1d = Join-Path $buildDir 'dav1d.dll'
    if (Test-Path -LiteralPath $dav1d -PathType Leaf) {
        Copy-Item -LiteralPath $dav1d -Destination $stagedDav1d -Force
    } elseif (Test-Path -LiteralPath $stagedDav1d -PathType Leaf) {
        Remove-Item -LiteralPath $stagedDav1d -Force
    }
}
Write-Host ("build.ps1: preset {0} exitCode={1}" -f $Preset, $exitCode)
exit $exitCode
