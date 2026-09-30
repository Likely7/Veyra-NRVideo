[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][ValidateSet('qml')][string]$UiTarget,
  [Parameter(Mandatory = $true)][string]$PlayerExe,
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$StagingDirectory,
  [string]$App = '',
  [string]$Out = '',
  [string]$TempDirectory = '',
  [ValidateRange(1, 300)][int]$TimeoutSeconds = 120
)

# Unit/contract results are real process results. Hardware-dependent tests are
# explicitly skipped and never counted as passes; any executable failure makes
# this script fail so unattended mode cannot advance on a partial run.
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = [IO.Path]::GetFullPath((Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra')).TrimEnd('\') + '\'
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 8))
if ([string]::IsNullOrWhiteSpace($Out)) {
  $Out = Join-Path $artifactRoot ("logs\ui-qml-migration-20260927\" + $runId + '\unit')
}
if ([string]::IsNullOrWhiteSpace($TempDirectory)) {
  $TempDirectory = Join-Path $artifactRoot ("tmp\ui-qml-migration-20260927\" + $runId + '\unit')
}
$outPath = [IO.Path]::GetFullPath($Out)
foreach ($path in @($outPath, [IO.Path]::GetFullPath($TempDirectory))) {
  if (-not $path.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unit output and TEMP must be under E:\项目\Veyra: $path"
  }
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $outPath)) | Out-Null
if (Test-Path -LiteralPath $outPath) {
  if (-not (Test-Path -LiteralPath $outPath -PathType Container)) { throw "Unit output is not a directory: $outPath" }
  if (@(Get-ChildItem -LiteralPath $outPath -Force).Count -gt 0) { throw "Unit output must be a new empty directory; refusing to reuse: $outPath" }
} else {
  [IO.Directory]::CreateDirectory($outPath) | Out-Null
}
[IO.Directory]::CreateDirectory($outPath) | Out-Null
$tempPath = [IO.Path]::GetFullPath($TempDirectory)
if (Test-Path -LiteralPath $tempPath) {
  if (-not (Test-Path -LiteralPath $tempPath -PathType Container)) { throw "Unit TEMP is not a directory: $tempPath" }
  if (@(Get-ChildItem -LiteralPath $tempPath -Force).Count -gt 0) { throw "Unit TEMP must be a new empty directory; refusing to reuse: $tempPath" }
} else {
  [IO.Directory]::CreateDirectory($tempPath) | Out-Null
}
$env:TEMP = $tempPath
$env:TMP = $env:TEMP
# Unit tests run unattended and must not depend on an interactive desktop. The
# QML test binaries still use the staged Qt platform/plugin and fail normally
# if that deployment is incomplete.
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QML_DISK_CACHE_PATH = Join-Path $tempPath 'qmlcache'

$entryScript = Join-Path $rootPath 'scripts\acceptance\resolve-ui-migration-entry.ps1'
$entryArgs = @{
  Root = $rootPath; UiTarget = $UiTarget; PlayerExe = $PlayerExe
  BuildDirectory = $BuildDirectory; StagingDirectory = $StagingDirectory
  ArtifactDirectory = $outPath; OutputFile = (Join-Path $outPath 'entry.json')
}
$null = & $entryScript @entryArgs
$stagingPath = (Resolve-Path -LiteralPath $StagingDirectory -ErrorAction Stop).Path
$appPath = if ([string]::IsNullOrWhiteSpace($App)) { $stagingPath } else { (Resolve-Path -LiteralPath $App -ErrorAction Stop).Path }
if (-not (Test-Path -LiteralPath $appPath -PathType Container)) { throw "App is not a directory: $appPath" }
if (-not $appPath.Equals($stagingPath, [StringComparison]::OrdinalIgnoreCase)) {
  throw "QML unit tests must run from the QML staging directory passed to -StagingDirectory: staging=$stagingPath app=$appPath"
}
if (Test-Path -LiteralPath (Join-Path $appPath 'veyra.exe') -PathType Leaf) {
  throw "QML staging contains the legacy entry veyra.exe: $appPath"
}

$requiredTests = @('veyra_qml_data_tests.exe', 'veyra_qml_easing_tests.exe', 'veyra_qml_quick_tests.exe')
$testExecutables = foreach ($name in $requiredTests) {
  $candidate = Join-Path $appPath $name
  if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw "Missing QML-only test executable: $candidate" }
  Get-Item -LiteralPath $candidate
}
$results = [Collections.Generic.List[object]]::new()

function Quote-Argument([string]$Value) { return '"' + $Value.Replace('"', '\"') + '"' }
foreach ($test in $testExecutables) {
  $name = $test.Name
  $log = Join-Path $outPath ($test.BaseName + '.log')
  $errLog = $log + '.err'
  $arguments = @()
  if ($name -eq 'veyra_qml_data_tests.exe') {
    $scratch = Join-Path $outPath 'qml-data-scratch'
    # The test creates this directory itself and rejects an existing one.
    $arguments = @($scratch)
  }
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $test.FullName
  $psi.WorkingDirectory = $appPath
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  if ($psi.PSObject.Properties.Name -contains 'ArgumentList') {
    foreach ($argument in $arguments) { [void]$psi.ArgumentList.Add([string]$argument) }
  } elseif ($arguments.Count -gt 0) {
    $psi.Arguments = ($arguments | ForEach-Object { Quote-Argument ([string]$_) }) -join ' '
  }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $process = [Diagnostics.Process]::Start($psi)
  $stdoutTask = $process.StandardOutput.ReadToEndAsync()
  $stderrTask = $process.StandardError.ReadToEndAsync()
  $finished = $process.WaitForExit($TimeoutSeconds * 1000)
  if (-not $finished) {
    try { $process.Kill() } catch { }
    $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $log -Encoding UTF8
    $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $errLog -Encoding UTF8
    $results.Add([ordered]@{ name = $name; status = 'timeout'; exit = $null; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); reason = 'timeout' })
    continue
  }
  $process.WaitForExit()
  $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $log -Encoding UTF8
  $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $errLog -Encoding UTF8
  $code = $process.ExitCode
  $results.Add([ordered]@{ name = $name; status = if ($code -eq 0) { 'pass' } else { 'fail' }; exit = $code; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); reason = '' })
}

$summaryPath = Join-Path $outPath 'summary.json'
$summary = [ordered]@{
  schema = 'veyra.qml-only-unit-run.v1'
  uiTarget = $UiTarget
  stagingDirectory = $appPath
  results = $results
  failures = @($results | Where-Object { $_.status -in @('fail', 'timeout') }).Count
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$results | Group-Object status | ForEach-Object { "{0}={1}" -f $_.Name, $_.Count }
$failed = @($results | Where-Object { $_.status -in @('fail', 'timeout') })
$failed | ForEach-Object { "{0} {1} exit={2}" -f $_.status, $_.name, $_.exit }
if ($failed.Count -gt 0) { exit 1 }
exit 0
