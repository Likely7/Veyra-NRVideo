[CmdletBinding()]
param(
  [string]$Root = 'C:\Users\123\Desktop\Veyra DLSS Video Player',
  [Parameter(Mandatory = $true)][string]$QmlBuild,
  [string]$OutputDirectory = ''
)

# Read-only contract test over copied executable names. It intentionally makes
# no source/build changes; each case receives a fresh staging directory and
# only the resolver decides whether that combination is valid.
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
$qmlBuildPath = (Resolve-Path -LiteralPath $QmlBuild -ErrorAction Stop).Path
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = [IO.Path]::GetFullPath((Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra')).TrimEnd('\') + '\'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $artifactRoot 'tests\ui-migration-entry-contract-20260927'
}
[string]$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $qmlBuildPath.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "QmlBuild must be under E:\项目\Veyra: $qmlBuildPath"
}
if ($qmlBuildPath -match '\\qt-probe(?:-|\\|$)') {
  throw "QmlBuild cannot use a legacy qt-probe directory: $qmlBuildPath"
}
if (-not $outputRoot.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
  throw "OutputDirectory must be under E:\项目\Veyra: $outputRoot"
}
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$outRoot = Join-Path $outputRoot ('run-' + $runId)
[IO.Directory]::CreateDirectory($outRoot) | Out-Null
$resolver = Join-Path $rootPath 'scripts\acceptance\resolve-ui-migration-entry.ps1'
$qtRoot = Join-Path $artifactRoot 'deps\qt\6.8.3\msvc2022_64'
$requiredExecutables = @(
  'veyra_qml_ui.exe',
  'veyra_qml_data_tests.exe',
  'veyra_qml_easing_tests.exe',
  'veyra_qml_quick_tests.exe'
)
$requiredQtDlls = @(
  'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6QmlModels.dll',
  'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickTemplates2.dll',
  'Qt6Widgets.dll', 'Qt6QuickTest.dll'
)
foreach ($name in $requiredExecutables) {
  if (-not (Test-Path -LiteralPath (Join-Path $qmlBuildPath $name) -PathType Leaf)) { throw "missing QML fixture: $(Join-Path $qmlBuildPath $name)" }
}
foreach ($name in $requiredQtDlls) {
  if (-not (Test-Path -LiteralPath (Join-Path $qtRoot "bin\$name") -PathType Leaf)) { throw "missing Qt fixture: $(Join-Path $qtRoot "bin\$name")" }
}
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'plugins\platforms\qwindows.dll') -PathType Leaf)) {
  throw "missing Qt fixture platform plugin: $(Join-Path $qtRoot 'plugins\platforms\qwindows.dll')"
}
if (-not (Test-Path -LiteralPath (Join-Path $rootPath 'tests\qml\quick\tst_components.qml') -PathType Leaf)) {
  throw "missing QML Quick Test source fixture"
}

function Copy-Tree([string]$Source, [string]$Destination) {
  if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "fixture source directory is missing: $Source" }
  [IO.Directory]::CreateDirectory($Destination) | Out-Null
  Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name) -Recurse -Force
  }
}

function New-Stage([string]$Name, [string]$SourceExe, [string]$TargetExe, [bool]$IncludeQml) {
  $stage = Join-Path $outRoot ("stage-" + $Name)
  [IO.Directory]::CreateDirectory($stage) | Out-Null
  Copy-Item -LiteralPath $SourceExe -Destination (Join-Path $stage $TargetExe) -Force
  foreach ($name in $requiredExecutables | Where-Object { $_ -ne 'veyra_qml_ui.exe' }) {
    Copy-Item -LiteralPath (Join-Path $qmlBuildPath $name) -Destination (Join-Path $stage $name) -Force
  }
  if ($IncludeQml) {
    Copy-Tree (Join-Path $qmlBuildPath 'qml') (Join-Path $stage 'qml')
  }
  Copy-Tree (Join-Path $qtRoot 'qml\QtTest') (Join-Path $stage 'qml\QtTest')
  Copy-Tree (Join-Path $rootPath 'tests\qml\quick') (Join-Path $stage 'qml-tests')
  foreach ($name in $requiredQtDlls) {
    Copy-Item -LiteralPath (Join-Path $qtRoot "bin\$name") -Destination (Join-Path $stage $name) -Force
  }
  [IO.Directory]::CreateDirectory((Join-Path $stage 'platforms')) | Out-Null
  Copy-Item -LiteralPath (Join-Path $qtRoot 'plugins\platforms\qwindows.dll') -Destination (Join-Path $stage 'platforms\qwindows.dll') -Force
  Copy-Item -LiteralPath (Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll') -Destination (Join-Path $stage 'platforms\qoffscreen.dll') -Force
  foreach ($relative in @('runtime', 'runtime_local\amd', 'runtime_local\intel')) {
    [IO.Directory]::CreateDirectory((Join-Path $stage $relative)) | Out-Null
  }
  return $stage
}

$qmlSource = Join-Path $qmlBuildPath 'veyra_qml_ui.exe'

$qmlStage = New-Stage 'qml-valid' $qmlSource 'veyra_qml_ui.exe' $true
$qmlNamedLegacyStage = New-Stage 'qml-with-legacy-entry' $qmlSource 'veyra.exe' $true
$hashMismatchStage = New-Stage 'qml-hash-mismatch' $qmlSource 'veyra_qml_ui.exe' $true
$missingModuleStage = New-Stage 'qml-missing-module' $qmlSource 'veyra_qml_ui.exe' $false
$missingQtStage = New-Stage 'qml-missing-qt-runtime' $qmlSource 'veyra_qml_ui.exe' $true
$missingOffscreenStage = New-Stage 'qml-missing-offscreen' $qmlSource 'veyra_qml_ui.exe' $true
$missingQtTestStage = New-Stage 'qml-missing-qttest' $qmlSource 'veyra_qml_ui.exe' $true
[IO.File]::AppendAllText((Join-Path $hashMismatchStage 'veyra_qml_ui.exe'), 'entry-contract-hash-mismatch')
Remove-Item -LiteralPath (Join-Path $missingQtStage 'Qt6Quick.dll') -Force
Remove-Item -LiteralPath (Join-Path $missingOffscreenStage 'platforms\qoffscreen.dll') -Force
Remove-Item -LiteralPath (Join-Path $missingQtTestStage 'qml\QtTest\qmldir') -Force

$cases = @(
  [ordered]@{ name = 'qml-valid'; target = 'qml'; build = $qmlBuildPath; stage = $qmlStage; exe = (Join-Path $qmlStage 'veyra_qml_ui.exe'); expected = $true },
  [ordered]@{ name = 'qml-rejects-legacy-entry'; target = 'qml'; build = $qmlBuildPath; stage = $qmlNamedLegacyStage; exe = (Join-Path $qmlNamedLegacyStage 'veyra.exe'); expected = $false },
  [ordered]@{ name = 'qml-rejects-hash-mismatch'; target = 'qml'; build = $qmlBuildPath; stage = $hashMismatchStage; exe = (Join-Path $hashMismatchStage 'veyra_qml_ui.exe'); expected = $false },
  [ordered]@{ name = 'qml-rejects-missing-module'; target = 'qml'; build = $qmlBuildPath; stage = $missingModuleStage; exe = (Join-Path $missingModuleStage 'veyra_qml_ui.exe'); expected = $false },
  [ordered]@{ name = 'qml-rejects-missing-qt-runtime'; target = 'qml'; build = $qmlBuildPath; stage = $missingQtStage; exe = (Join-Path $missingQtStage 'veyra_qml_ui.exe'); expected = $false },
  [ordered]@{ name = 'qml-rejects-missing-offscreen'; target = 'qml'; build = $qmlBuildPath; stage = $missingOffscreenStage; exe = (Join-Path $missingOffscreenStage 'veyra_qml_ui.exe'); expected = $false },
  [ordered]@{ name = 'qml-rejects-missing-qttest'; target = 'qml'; build = $qmlBuildPath; stage = $missingQtTestStage; exe = (Join-Path $missingQtTestStage 'veyra_qml_ui.exe'); expected = $false }
)

$results = [Collections.Generic.List[object]]::new()
foreach ($case in $cases) {
  $artifact = Join-Path $outRoot ("evidence-" + $case.name)
  $output = Join-Path $artifact 'entry.json'
  $actual = $false
  $detail = ''
  try {
    [IO.Directory]::CreateDirectory($artifact) | Out-Null
    $null = & $resolver -Root $rootPath -UiTarget $case.target -PlayerExe $case.exe -BuildDirectory $case.build -StagingDirectory $case.stage -ArtifactDirectory $artifact -OutputFile $output
    $actual = $true
  } catch {
    $detail = $_.Exception.Message
  }
  $passed = ($actual -eq [bool]$case.expected)
  $results.Add([ordered]@{ name = $case.name; expectedPass = [bool]$case.expected; actualPass = $actual; passed = $passed; detail = $detail })
}
$record = [ordered]@{
  schema = 'veyra.ui-migration-entry-contract.v3'
  timestampUtc = [DateTime]::UtcNow.ToString('o')
  outputRoot = $outputRoot
  runDirectory = $outRoot
  cases = $results
  failures = @($results | Where-Object { -not $_.passed }).Count
}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outRoot 'result.json') -Encoding UTF8
if ($record.failures -gt 0) { Write-Host "ENTRY CONTRACT FAIL: $outRoot"; exit 1 }
Write-Host "ENTRY CONTRACT PASS: $outRoot"; exit 0
