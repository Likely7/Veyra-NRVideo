[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][ValidateSet('qml')][string]$UiTarget,
  [Parameter(Mandatory = $true)][string]$PlayerExe,
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$StagingDirectory,
  [string]$ArtifactDirectory = '',
  [string]$OutputFile = ''
)

$ErrorActionPreference = 'Stop'
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = [IO.Path]::GetFullPath((Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra')).TrimEnd('\')
if ([string]::IsNullOrWhiteSpace($ArtifactDirectory)) {
  $ArtifactDirectory = Join-Path $artifactRoot 'logs\ui-qml-migration-20260927\entry'
}

function FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path)
}

function Resolve-Directory([string]$Path, [string]$Name) {
  $full = FullPath $Path
  if (-not (Test-Path -LiteralPath $full -PathType Container)) { throw "$Name is not a directory: $full" }
  return (Resolve-Path -LiteralPath $full -ErrorAction Stop).Path
}

function Resolve-File([string]$Path, [string]$Name) {
  $full = FullPath $Path
  if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "$Name is not a file: $full" }
  return (Resolve-Path -LiteralPath $full -ErrorAction Stop).Path
}

function Assert-Under([string]$Path, [string]$Directory, [string]$Name) {
  $fullPath = FullPath $Path
  $fullDirectory = (FullPath $Directory).TrimEnd('\', '/')
  $prefix = $fullDirectory + '\'
  if (-not $fullPath.Equals($fullDirectory, [StringComparison]::OrdinalIgnoreCase) -and
      -not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Name must be inside ${fullDirectory}: $fullPath"
  }
}

function Assert-ArtifactPath([string]$Path, [string]$Name) {
  $full = FullPath $Path
  Assert-Under $full $artifactRoot $Name
  return $full
}

$rootPath = Resolve-Directory $Root 'Root'
$playerPath = Resolve-File $PlayerExe 'PlayerExe'
$buildPath = Resolve-Directory $BuildDirectory 'BuildDirectory'
$stagingPath = Resolve-Directory $StagingDirectory 'StagingDirectory'
Assert-Under $buildPath $artifactRoot 'BuildDirectory'
Assert-Under $stagingPath $artifactRoot 'StagingDirectory'
$actualName = [IO.Path]::GetFileName($playerPath)
$expectedName = 'veyra_qml_ui.exe'
if (-not $actualName.Equals($expectedName, [StringComparison]::OrdinalIgnoreCase)) {
  throw "UI entry mismatch: target=$UiTarget requires $expectedName but PlayerExe=$playerPath"
}
Assert-Under $playerPath $stagingPath 'PlayerExe'

# A staging directory is one QML product plus its three QML-only test tools.
# The legacy entry or an unlisted executable makes later smoke/unit results
# ambiguous even when PlayerExe is explicit.
$requiredExecutables = @(
  'veyra_qml_ui.exe',
  'veyra_qml_data_tests.exe',
  'veyra_qml_easing_tests.exe',
  'veyra_qml_quick_tests.exe'
)
$allStagedExecutables = @(Get-ChildItem -LiteralPath $stagingPath -File -Filter '*.exe' -Recurse -Force |
  ForEach-Object { $_.Name } | Sort-Object)
$expectedExecutableNames = @($requiredExecutables | Sort-Object)
$exeDifferences = @(Compare-Object -ReferenceObject $expectedExecutableNames -DifferenceObject $allStagedExecutables)
if ($exeDifferences.Count -gt 0) {
  throw "StagingDirectory executable set mismatch; expected=$($expectedExecutableNames -join ', ') found=$($allStagedExecutables -join ', ')"
}
$legacyExecutables = @(Get-ChildItem -LiteralPath $stagingPath -Filter 'veyra.exe' -File -Recurse -Force)
if ($legacyExecutables.Count -gt 0) { throw "StagingDirectory contains the legacy UI executable: $($legacyExecutables[0].FullName)" }

$cachePath = Resolve-File (Join-Path $buildPath 'CMakeCache.txt') 'CMakeCache.txt'
$line = Select-String -LiteralPath $cachePath -Pattern '^VEYRA_BUILD_QML_UI:BOOL=(ON|OFF)$' | Select-Object -First 1
if (-not $line) { throw "CMakeCache.txt has no VEYRA_BUILD_QML_UI entry: $cachePath" }
$cmakeQml = $line.Matches[0].Groups[1].Value
$expectedSwitch = if ($UiTarget -eq 'qml') { 'ON' } else { 'OFF' }
if ($cmakeQml -ne $expectedSwitch) {
  throw "UI target $UiTarget requires VEYRA_BUILD_QML_UI=$expectedSwitch in $cachePath (found $cmakeQml)"
}

$buildMatches = @(Get-ChildItem -LiteralPath $buildPath -Filter $expectedName -File -Recurse -Force | Sort-Object FullName)
if ($buildMatches.Count -ne 1) {
  throw "BuildDirectory must contain exactly one $expectedName; found $($buildMatches.Count): $buildPath"
}
$buildExePath = $buildMatches[0].FullName
$buildHash = (Get-FileHash -LiteralPath $buildExePath -Algorithm SHA256).Hash
$playerHash = (Get-FileHash -LiteralPath $playerPath -Algorithm SHA256).Hash
if (-not $buildHash.Equals($playerHash, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Build/staging executable mismatch: build=$buildExePath ($buildHash), staging=$playerPath ($playerHash)"
}

$testExecutables = [Collections.Generic.List[object]]::new()
foreach ($name in @('veyra_qml_data_tests.exe', 'veyra_qml_easing_tests.exe', 'veyra_qml_quick_tests.exe')) {
  $buildTestMatches = @(Get-ChildItem -LiteralPath $buildPath -Filter $name -File -Recurse -Force | Sort-Object FullName)
  if ($buildTestMatches.Count -ne 1) {
    throw "BuildDirectory must contain exactly one $name; found $($buildTestMatches.Count): $buildPath"
  }
  $stagedTestPath = Join-Path $stagingPath $name
  if (-not (Test-Path -LiteralPath $stagedTestPath -PathType Leaf)) { throw "staging is missing QML test executable: $stagedTestPath" }
  $testHash = (Get-FileHash -LiteralPath $stagedTestPath -Algorithm SHA256).Hash
  $buildTestHash = (Get-FileHash -LiteralPath $buildTestMatches[0].FullName -Algorithm SHA256).Hash
  if (-not $buildTestHash.Equals($testHash, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build/staging executable mismatch: build=$($buildTestMatches[0].FullName) ($buildTestHash), staging=$stagedTestPath ($testHash)"
  }
  $testExecutables.Add([ordered]@{
    name = $name
    stagingPath = $stagedTestPath
    stagingSha256 = $testHash
    buildPath = $buildTestMatches[0].FullName
    buildSha256 = $buildTestHash
  })
}

if ($UiTarget -eq 'qml') {
  $qmldir = Join-Path $stagingPath 'qml\Veyra\qmldir'
  if (-not (Test-Path -LiteralPath $qmldir -PathType Leaf)) {
    throw "QML staging is incomplete; missing $qmldir"
  }
}
$qmlTestSource = Join-Path $stagingPath 'qml-tests\tst_components.qml'
if (-not (Test-Path -LiteralPath $qmlTestSource -PathType Leaf)) {
  throw "QML staging is incomplete; missing $qmlTestSource"
}
$requiredQtDlls = @(
  'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6QmlModels.dll',
  'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickTemplates2.dll',
  'Qt6Widgets.dll', 'Qt6QuickTest.dll'
)
$qtRuntimeFiles = [Collections.Generic.List[object]]::new()
foreach ($name in $requiredQtDlls) {
  $path = Join-Path $stagingPath $name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "QML staging is missing Qt runtime DLL: $path" }
  $qtRuntimeFiles.Add([ordered]@{ name = $name; path = $path; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash })
}
$platformPlugin = Join-Path $stagingPath 'platforms\qwindows.dll'
if (-not (Test-Path -LiteralPath $platformPlugin -PathType Leaf)) { throw "QML staging is missing Qt platform plugin: $platformPlugin" }
$offscreenPlugin = Join-Path $stagingPath 'platforms\qoffscreen.dll'
if (-not (Test-Path -LiteralPath $offscreenPlugin -PathType Leaf)) { throw "QML staging is missing Qt offscreen plugin: $offscreenPlugin" }
$qtTestModule = Join-Path $stagingPath 'qml\QtTest\qmldir'
if (-not (Test-Path -LiteralPath $qtTestModule -PathType Leaf)) { throw "QML staging is missing QtTest module: $qtTestModule" }
$runtimeDirectories = @('runtime', 'runtime_local\amd', 'runtime_local\intel')
foreach ($relative in $runtimeDirectories) {
  $path = Join-Path $stagingPath $relative
  if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "QML staging is missing runtime directory: $path" }
}

$artifactPath = Assert-ArtifactPath $ArtifactDirectory 'ArtifactDirectory'
[IO.Directory]::CreateDirectory($artifactPath) | Out-Null
$outputPath = $null
if (-not [string]::IsNullOrWhiteSpace($OutputFile)) {
  $outputPath = Assert-ArtifactPath $OutputFile 'OutputFile'
  Assert-Under $outputPath $artifactPath 'OutputFile'
  [IO.Directory]::CreateDirectory((Split-Path -Parent $outputPath)) | Out-Null
}

$branch = (& git -C $rootPath rev-parse --abbrev-ref HEAD 2>$null)
$commit = (& git -C $rootPath rev-parse HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) { throw "Could not resolve Git identity for $rootPath" }

$record = [ordered]@{
  schema = 'veyra.ui-migration-entry.v3'
  timestampUtc = [DateTime]::UtcNow.ToString('o')
  root = $rootPath
  branch = [string]$branch
  commit = [string]$commit
  uiTarget = $UiTarget
  entryContract = 'qml-smoke'
  playerExe = $playerPath
  playerExeName = $actualName
  playerExeSha256 = $playerHash
  buildDirectory = $buildPath
  buildExe = $buildExePath
  buildExeSha256 = $buildHash
  buildStagingHashMatch = $true
  testExecutables = @($testExecutables)
  cmakeCache = $cachePath
  cmakeQmlSwitch = $cmakeQml
  stagingDirectory = $stagingPath
  qmlModule = Join-Path $stagingPath 'qml\Veyra\qmldir'
  qmlQuickTestSource = $qmlTestSource
  qtRuntime = [ordered]@{
    dlls = @($qtRuntimeFiles)
    platformPlugin = [ordered]@{
      path = $platformPlugin
      sha256 = (Get-FileHash -LiteralPath $platformPlugin -Algorithm SHA256).Hash
    }
    offscreenPlugin = [ordered]@{
      path = $offscreenPlugin
      sha256 = (Get-FileHash -LiteralPath $offscreenPlugin -Algorithm SHA256).Hash
    }
    qtTestModule = $qtTestModule
  }
  runtimeDirectories = @($runtimeDirectories | ForEach-Object { Join-Path $stagingPath $_ })
  artifactDirectory = $artifactPath
}
$json = $record | ConvertTo-Json -Depth 10
if ($outputPath) { $json | Set-Content -LiteralPath $outputPath -Encoding UTF8 }
Write-Output $json
