# Performance / latency baseline for the shared legacy engine path. The old
# command-line switches below are not implemented by the QML executable.
# Callers must name the executable and UI contract explicitly; a staging folder
# containing both front ends is otherwise ambiguous.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][ValidateSet('legacy')][string]$UiTarget,
  [Parameter(Mandatory = $true)][string]$PlayerExe,
  [string]$App = '',
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$StagingDirectory,
  [string]$Fixtures = 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\perf',
  [string]$Save = '',
  [string]$Compare = '',
  [int]$Seconds = 8
)
$ErrorActionPreference = 'Stop'
$env:TEMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'
$env:TMP = $env:TEMP
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$outPath = [IO.Path]::GetFullPath($Out)
$entryScript = Join-Path $PSScriptRoot 'acceptance\resolve-ui-migration-entry.ps1'
$entryArgs = @(
  '-Root', $root, '-UiTarget', $UiTarget, '-PlayerExe', $PlayerExe,
  '-ArtifactDirectory', $outPath,
  '-OutputFile', (Join-Path $outPath 'entry.json')
)
$entryArgs += @('-BuildDirectory', $BuildDirectory, '-StagingDirectory', $StagingDirectory)
$null = & $entryScript @entryArgs
New-Item -ItemType Directory -Force $outPath | Out-Null
$exe = (Resolve-Path -LiteralPath $PlayerExe).Path
$workingDirectory = if ($App) { (Resolve-Path -LiteralPath $App).Path } else { Split-Path -Parent $exe }
$fixturePaths = @(
  (Join-Path $Fixtures 'test_av_1080p.mp4'),
  (Join-Path $Fixtures 'test_av_4k.mp4')
)
foreach ($fixture in $fixturePaths) {
  if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) { throw "Missing performance fixture: $fixture" }
}
$cases = [ordered]@{
  'nr-1080'       = @((Join-Path $Fixtures 'test_av_1080p.mp4'), '--nr', '--realtime')
  'nr-fg-1080'    = @((Join-Path $Fixtures 'test_av_1080p.mp4'), '--nr', '--realtime', '--fg')
  'nr-fg-4k'      = @((Join-Path $Fixtures 'test_av_4k.mp4'), '--nr', '--realtime', '--fg')
  'plain-1080'    = @((Join-Path $Fixtures 'test_av_1080p.mp4'))
}
$fields = 'decodeP95Ms','graphSubmitP95Ms','gpuReadyP95Ms','presentP95Ms','gpuColorP95Ms','gpuSrP95Ms','gpuFlowP95Ms','gpuNrP95Ms','gpuResidualP95Ms','gpuFgBatchP95Ms','gpuBlitP95Ms'
$summary = [ordered]@{}
$failures = [Collections.Generic.List[string]]::new()
foreach ($name in $cases.Keys) {
  $log = Join-Path $outPath "$name.log"
  $argv = @($cases[$name]) + @('--smoke-seconds', $Seconds)
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $exe
  $psi.WorkingDirectory = $workingDirectory
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  if ($psi.PSObject.Properties.Name -contains 'ArgumentList') {
    foreach ($argument in $argv) { [void]$psi.ArgumentList.Add([string]$argument) }
  } else {
    $quote = { param([string]$value) '"' + $value.Replace('"', '\"') + '"' }
    $psi.Arguments = (($argv | ForEach-Object { & $quote ([string]$_) }) -join ' ')
  }
  $p = [Diagnostics.Process]::Start($psi)
  $stdoutTask = $p.StandardOutput.ReadToEndAsync()
  $stderrTask = $p.StandardError.ReadToEndAsync()
  $finished = $p.WaitForExit(($Seconds + 40) * 1000)
  if (-not $finished) {
    try { $p.Kill() } catch { }
    $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $log -Encoding UTF8
    $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath "$log.err" -Encoding UTF8
    $summary[$name] = 'timeout'
    $failures.Add($name)
    continue
  }
  $p.WaitForExit()
  $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $log -Encoding UTF8
  $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath "$log.err" -Encoding UTF8
  $lines = @(Get-Content -LiteralPath $log | Where-Object { $_ -match '\[player-timing\]' } | Select-Object -Skip 1)
  $row = [ordered]@{ exit = $p.ExitCode; samples = @($lines).Count }
  if ($p.ExitCode -ne 0 -or $row.samples -eq 0) { $failures.Add($name) }
  foreach ($f in $fields) {
    $vals = @($lines | ForEach-Object { if ($_ -match "$f=([0-9.]+)") { [double]$Matches[1] } })
    $row[$f] = if ($vals.Count) { [math]::Round(($vals | Measure-Object -Average).Average, 3) } else { $null }
  }
  $flow = Get-Content -LiteralPath $log | Where-Object { $_ -match '\[frame-flow\] state=closed' } | Select-Object -Last 1
  foreach ($k in 'presentSubmitFps','generatedFps','realPresented','generatedPresented','sourceSkippedBeforeGraph','slotWaitCount','cancelledBeforePresent') {
    if ($flow -match "$k=([0-9.]+)") { $row[$k] = [double]$Matches[1] }
  }
  $summary[$name] = $row
}
$json = $summary | ConvertTo-Json -Depth 4
$resultsPath = Join-Path $outPath 'results.json'
$json | Set-Content -LiteralPath $resultsPath -Encoding utf8
if ($Save) { $json | Set-Content -LiteralPath $Save -Encoding utf8; "saved $Save" }
if ($Compare) {
  $base = Get-Content -LiteralPath $Compare -Raw | ConvertFrom-Json
  foreach ($name in $summary.Keys) {
    $now = $summary[$name]; $was = $base.$name
    if ($now -is [string] -or -not $was) { "$name : $now (no comparison)"; continue }
    $parts = foreach ($f in 'graphSubmitP95Ms','gpuReadyP95Ms','presentP95Ms','gpuNrP95Ms','presentSubmitFps','sourceSkippedBeforeGraph') {
      if ($null -ne $now[$f] -and $null -ne $was.$f) { '{0} {1}->{2}' -f $f, $was.$f, $now[$f] }
    }
    "$name : " + ($parts -join ' | ')
  }
  & (Join-Path $PSScriptRoot 'compare-ui-migration-perf.ps1') -Results $resultsPath -Baseline $Compare -Report (Join-Path $outPath 'gate.json')
  if ($failures.Count -gt 0) { exit 1 }
  exit $LASTEXITCODE
} else { $json }
if ($failures.Count -gt 0) { Write-Error ("Performance run failed: " + ($failures -join ', ')); exit 1 }
