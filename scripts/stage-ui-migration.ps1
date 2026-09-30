[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][ValidateSet('qml')][string]$UiTarget,
  [Parameter(Mandatory = $true)][string]$Build,
  [Parameter(Mandatory = $true)][string]$App,
  [Parameter(Mandatory = $true)][string]$Release,
  # Optional default-slot NR runtime (2026-09-29 user decision: Lecram 310.8.3).
  # When given, runtime/ is copied instead of linked so the release stays untouched.
  [string]$NrDefaultRuntime = '',
  # Shared RTX20-50 compatible slot (legacy parameter/path name retained).
  [string]$NrAmpereRuntime = ''
)

# Stage one runnable UI contract per directory. The resolver later checks the
# same cache switch and executable hash before any smoke test starts.
$ErrorActionPreference = 'Stop'
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra'
$rootPath = (Resolve-Path (Join-Path $PSScriptRoot '..') -ErrorAction Stop).Path
$buildPath = (Resolve-Path -LiteralPath $Build -ErrorAction Stop).Path
$releasePath = (Resolve-Path -LiteralPath $Release -ErrorAction Stop).Path
$appPath = [IO.Path]::GetFullPath($App)
$artifactRootFull = [IO.Path]::GetFullPath($artifactRoot).TrimEnd('\') + '\'
foreach ($path in @($buildPath, $releasePath, $appPath)) {
  if (-not $path.StartsWith($artifactRootFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build, release and staging paths must be under ${artifactRoot}: $path"
  }
}
if (Test-Path -LiteralPath $appPath) {
  if (-not (Test-Path -LiteralPath $appPath -PathType Container)) { throw "Staging path is not a directory: $appPath" }
  $existing = @(Get-ChildItem -LiteralPath $appPath -Force)
  if ($existing.Count -gt 0) { throw "Staging directory must be new and empty; refusing to reuse: $appPath" }
} else {
  [IO.Directory]::CreateDirectory($appPath) | Out-Null
}
$cache = Join-Path $buildPath 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { throw "missing CMakeCache.txt: $cache" }
$switch = (Select-String -LiteralPath $cache -Pattern '^VEYRA_BUILD_QML_UI:BOOL=(ON|OFF)$' | Select-Object -First 1).Matches[0].Groups[1].Value
$expectedSwitch = if ($UiTarget -eq 'qml') { 'ON' } else { 'OFF' }
if ($switch -ne $expectedSwitch) { throw "Build cache target mismatch: requested=$UiTarget expected=$expectedSwitch found=$switch" }

$expectedExe = 'veyra_qml_ui.exe'
$requiredExecutables = @(
  'veyra_qml_ui.exe',
  'veyra_qml_data_tests.exe',
  'veyra_qml_easing_tests.exe',
  'veyra_qml_quick_tests.exe'
)
$buildExecutables = @{}
foreach ($name in $requiredExecutables) {
  $found = @(Get-ChildItem -LiteralPath $buildPath -Filter $name -File -Recurse -Force)
  if ($found.Count -ne 1) { throw "expected exactly one $name in $buildPath, found $($found.Count)" }
  $buildExecutables[$name] = $found[0].FullName
}
$opposite = 'veyra.exe'
$existingOpposite = @(Get-ChildItem -LiteralPath $appPath -Filter $opposite -File -Recurse -ErrorAction SilentlyContinue)
if ($existingOpposite.Count -gt 0) { throw "staging directory contains the legacy UI executable: $($existingOpposite[0].FullName)" }

$qtRoot = Join-Path $artifactRoot 'deps\qt\6.8.3\msvc2022_64'
$windeployqt = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) { throw "missing Qt deployment tool: $windeployqt" }
$qmlSource = Join-Path $buildPath 'qml'
if (-not (Test-Path -LiteralPath (Join-Path $qmlSource 'Veyra\qmldir') -PathType Leaf)) {
  throw "QML build output is missing qml/Veyra/qmldir: $qmlSource"
}
$qmlTestSource = Join-Path $rootPath 'tests\qml\quick'
if (-not (Test-Path -LiteralPath (Join-Path $qmlTestSource 'tst_components.qml') -PathType Leaf)) {
  throw "QML Quick Test source is missing tests/qml/quick/tst_components.qml: $qmlTestSource"
}

$runtimeTargets = @{
  runtime = Join-Path $releasePath 'runtime'
  'runtime_local\amd' = Join-Path $releasePath 'runtime_local\amd'
  'runtime_local\intel' = Join-Path $releasePath 'runtime_local\intel'
}
foreach ($entry in $runtimeTargets.GetEnumerator()) {
  if (-not (Test-Path -LiteralPath $entry.Value -PathType Container)) {
    throw "release runtime directory is missing: $($entry.Value)"
  }
}

# Always copy the NR runtime tree: retiring the old40 DLL must never follow a
# junction into the protected beta. Audit both approved files in every staging.
if (-not $NrDefaultRuntime) { $NrDefaultRuntime = Join-Path $runtimeTargets['runtime'] 'experimental\nvngx_dlssnr.dll' }
if (-not $NrAmpereRuntime) { $NrAmpereRuntime = Join-Path $runtimeTargets['runtime'] 'experimental\nr-ampere\nvngx_dlssnr.dll' }
# Approved slot replacements, pinned by identity for publisher staging only.
$nrSlots = @(
  @{ Given = $NrDefaultRuntime; Folder = 'experimental'; Path = 'runtime/experimental/nvngx_dlssnr.dll'
     Sha = 'F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC'; Size = 165840496; Sig = 'HashMismatch'
     Class = 'user-approved-community-Lecram-310.8.3-RTX50-runtime'; Source = 'community Lecram 310.8.3 (RankFTW/rhi-repo dlssnr-310.8.Lecram)' },
  @{ Given = $NrAmpereRuntime; Folder = 'experimental\nr-ampere'; Path = 'runtime/experimental/nr-ampere/nvngx_dlssnr.dll'
     Sha = '6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927'; Size = 165830144; Sig = 'NotSigned'
     Class = 'user-approved-community-SF-v2-310.8.2-RTX20-RTX50-compatible-runtime'; Source = 'community SF-v2 310.8.2 (RankFTW/rhi-repo dlssnr-310.8.SF-v2)' }
) | Where-Object { $_.Given }
if ($nrSlots) {
  # A copied runtime keeps the release untouched while one slot is replaced.
  $runtimeCopy = Join-Path $appPath 'runtime'
  & robocopy $runtimeTargets['runtime'] $runtimeCopy /E /NJH /NJS /NP /NFL /NDL | Out-Null
  if ($LASTEXITCODE -gt 7) { throw "robocopy failed ($LASTEXITCODE): runtime" }
  $manifestFile = Join-Path $runtimeCopy 'experimental\release-runtime-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
  foreach ($slot in $nrSlots) {
    $nrSource = (Resolve-Path -LiteralPath $slot.Given -ErrorAction Stop).Path
    $nrItem = Get-Item -LiteralPath $nrSource
    $nrHash = (Get-FileHash -LiteralPath $nrSource -Algorithm SHA256).Hash
    $nrSig = [string](Get-AuthenticodeSignature -LiteralPath $nrSource).Status
    if ($nrHash -ne $slot.Sha -or $nrItem.Length -ne $slot.Size -or $nrSig -ne $slot.Sig) {
      throw "NR runtime identity rejected for $($slot.Path): sha=$nrHash size=$($nrItem.Length) sig=$nrSig"
    }
    Copy-Item -LiteralPath $nrSource -Destination (Join-Path (Join-Path $runtimeCopy $slot.Folder) 'nvngx_dlssnr.dll') -Force
    $entry = @($manifest.files | Where-Object { $_.path -eq $slot.Path })
    if ($entry.Count -ne 1) { throw "manifest has $($entry.Count) entries for $($slot.Path)" }
    # Numeric parts, as package-portable.ps1 writes them (these DLLs' strings vary).
    $nrVersion = $nrItem.VersionInfo
    $entry[0].sha256 = $nrHash
    $entry[0].size = $nrItem.Length
    $entry[0].fileVersion = "$($nrVersion.FileMajorPart).$($nrVersion.FileMinorPart).$($nrVersion.FileBuildPart).$($nrVersion.FilePrivatePart)"
    $entry[0].authenticode = $nrSig
    $entry[0].classification = $slot.Class
    $entry[0].source = $slot.Source
    $entry[0].experimental = $true
    Write-Host "NR slot $($slot.Path) staged from $nrSource (sha $nrHash)"
  }
  $retired = [IO.Path]::GetFullPath((Join-Path $runtimeCopy 'experimental\nr-community\nvngx_dlssnr.dll'))
  if (-not $retired.StartsWith(([IO.Path]::GetFullPath($appPath).TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Retired runtime escaped fresh staging' }
  foreach ($parent in @($runtimeCopy, (Split-Path -Parent $retired))) {
    if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing to delete through runtime junction: $parent" }
  }
  if (Test-Path -LiteralPath $retired) { Remove-Item -LiteralPath $retired -Force }
  $manifest.files = @($manifest.files | Where-Object { $_.path -ne 'runtime/experimental/nr-community/nvngx_dlssnr.dll' })
  [IO.File]::WriteAllText($manifestFile, ($manifest | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
} else {
  foreach ($name in 'runtime') {
    $link = Join-Path $appPath $name
    if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $runtimeTargets[$name] | Out-Null }
  }
}
$local = Join-Path $appPath 'runtime_local'
[IO.Directory]::CreateDirectory($local) | Out-Null
foreach ($name in 'amd', 'intel') {
  $link = Join-Path $local $name
  $target = $runtimeTargets[(Join-Path 'runtime_local' $name)]
  if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target $target | Out-Null }
}

# Application binaries, QML-only test executables, FFmpeg DLLs and shaders come
# from the selected build. Backend tests and probes never enter this staging.
Get-ChildItem -LiteralPath $buildPath -File | Where-Object {
  $_.Extension -eq '.dll' -or $_.Name -eq $expectedExe -or $_.Name -in @('veyra_qml_data_tests.exe', 'veyra_qml_easing_tests.exe', 'veyra_qml_quick_tests.exe')
} | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $appPath -Force
}
foreach ($name in $requiredExecutables) {
  Copy-Item -LiteralPath $buildExecutables[$name] -Destination (Join-Path $appPath $name) -Force
}
foreach ($dll in 'avcodec-63.dll','avformat-63.dll','avutil-61.dll','dav1d.dll','swresample-7.dll','swscale-10.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') {
  $target = Join-Path $appPath $dll
  $source = Join-Path $releasePath $dll
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "release dependency is missing: $source" }
  if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $source -Destination $target }
}
function Invoke-Robocopy([string]$Source, [string]$Destination) {
  # The destination is a fresh staging tree. /E copies the selected build
  # without deleting anything from a directory that might belong to another run.
  & robocopy $Source $Destination /E /NJH /NJS /NP /NFL /NDL | Out-Null
  if ($LASTEXITCODE -gt 7) { throw "robocopy failed ($LASTEXITCODE): $Source -> $Destination" }
}
if (Test-Path -LiteralPath (Join-Path $buildPath 'shaders')) {
  Invoke-Robocopy (Join-Path $buildPath 'shaders') (Join-Path $appPath 'shaders')
}
Invoke-Robocopy $qmlSource (Join-Path $appPath 'qml')
Invoke-Robocopy $qmlTestSource (Join-Path $appPath 'qml-tests')

# Deploy Qt beside the same executable and test binaries that will be run. The
# QML UI alone does not pull in Qt Quick Test, so deploying only from it would
# leave veyra_qml_quick_tests un-runnable in a fresh staging directory.
 $stageTemp = Join-Path $artifactRoot ('tmp\ui-migration-stage\' + [IO.Path]::GetFileName($appPath))
 New-Item -ItemType Directory -Path $stageTemp -Force | Out-Null
 $previousTemp = $env:TEMP; $previousTmp = $env:TMP
 try {
 $env:TEMP = $stageTemp; $env:TMP = $stageTemp
 foreach ($name in @('veyra_qml_ui.exe', 'veyra_qml_easing_tests.exe', 'veyra_qml_quick_tests.exe')) {
  # QtTest is imported by the test sources, not by the application QML.
  $scanSource = if ($name -eq 'veyra_qml_quick_tests.exe') { $qmlTestSource } else { $qmlSource }
  & $windeployqt '--release' '--no-translations' '--qmldir' $scanSource '--qmlimport' $qmlSource '--dir' $appPath (Join-Path $appPath $name) | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for $name with exit $LASTEXITCODE" }
}
 } finally { $env:TEMP = $previousTemp; $env:TMP = $previousTmp }

# windeployqt deploys qwindows by default, but both QML tests run offscreen.
$offscreenSource = Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll'
if (-not (Test-Path -LiteralPath $offscreenSource -PathType Leaf)) { throw "missing Qt offscreen platform plugin: $offscreenSource" }
Copy-Item -LiteralPath $offscreenSource -Destination (Join-Path $appPath 'platforms\qoffscreen.dll')
if (-not (Test-Path -LiteralPath (Join-Path $appPath 'qml\QtTest\qmldir') -PathType Leaf)) {
  throw 'Qt deployment is incomplete; missing qml/QtTest/qmldir'
}

$requiredQtDlls = @(
  'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6QmlModels.dll',
  'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickTemplates2.dll',
  'Qt6Widgets.dll', 'Qt6QuickTest.dll'
)
foreach ($name in $requiredQtDlls) {
  $path = Join-Path $appPath $name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Qt deployment is incomplete; missing $path" }
}
$platformPlugin = Join-Path $appPath 'platforms\qwindows.dll'
if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf)) { throw "Qt deployment is incomplete; missing $platformPlugin" }
if (-not (Test-Path -LiteralPath (Join-Path $appPath 'qml\Veyra\qmldir') -PathType Leaf)) {
  throw "staging is missing qml/Veyra/qmldir after Qt deployment"
}
if (-not (Test-Path -LiteralPath (Join-Path $appPath 'qml-tests\tst_components.qml') -PathType Leaf)) {
  throw "staging is missing qml-tests/tst_components.qml"
}
$stagedUi = @(Get-ChildItem -LiteralPath $appPath -File -Force | Where-Object { $_.Name -in @('veyra.exe', 'veyra_qml_ui.exe') })
if ($stagedUi.Count -ne 1 -or -not $stagedUi[0].Name.Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
  $names = ($stagedUi | ForEach-Object Name) -join ', '
  throw "staging must contain exactly $expectedExe and no other UI executable; found: $names"
}
$stagedExecutables = @(Get-ChildItem -LiteralPath $appPath -File -Filter '*.exe' -Force | ForEach-Object Name | Sort-Object)
$expectedStagedExecutables = @($requiredExecutables | Sort-Object)
if (@(Compare-Object -ReferenceObject $expectedStagedExecutables -DifferenceObject $stagedExecutables).Count -gt 0) {
  throw "staging executable set mismatch; expected=$($expectedStagedExecutables -join ',') found=$($stagedExecutables -join ',')"
}
"staged $UiTarget $appPath"
