# Performance / latency baseline for the UI migration (S0.4 and every engine step).
# Runs the real player headless on fixed clips and averages the [player-timing]
# P95 fields plus the final [frame-flow] counters. Compare two runs with -Compare.
param([string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\app',
      [string]$Fixtures = 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips',
      [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\perf',
      [string]$Save = '',
      [string]$Compare = '',
      [int]$Seconds = 8)
$env:TEMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'; $env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $Out | Out-Null
$exe = Join-Path $App 'veyra.exe'
$cases = [ordered]@{
  'nr-1080'       = @((Join-Path $Fixtures 'test_av_1080p.mp4'), '--nr', '--realtime')
  'nr-fg-1080'    = @((Join-Path $Fixtures 'test_av_1080p.mp4'), '--nr', '--realtime', '--fg')
  'nr-fg-4k'      = @((Join-Path $Fixtures 'test_av_4k.mp4'), '--nr', '--realtime', '--fg')
  'plain-1080'    = @((Join-Path $Fixtures 'test_av_1080p.mp4'))
}
$fields = 'decodeP95Ms','graphSubmitP95Ms','gpuReadyP95Ms','presentP95Ms','gpuColorP95Ms','gpuSrP95Ms','gpuFlowP95Ms','gpuNrP95Ms','gpuResidualP95Ms','gpuFgBatchP95Ms','gpuBlitP95Ms'
$summary = [ordered]@{}
foreach ($name in $cases.Keys) {
  $log = Join-Path $Out "$name.log"
  $argv = @($cases[$name] | ForEach-Object { if ($_ -like '*\*') { "`"$_`"" } else { $_ } }) + @('--smoke-seconds', $Seconds)
  $p = Start-Process -FilePath $exe -ArgumentList $argv -WorkingDirectory $App -PassThru -WindowStyle Minimized -RedirectStandardOutput $log -RedirectStandardError "$log.err"
  $null = $p.Handle
  if (-not $p.WaitForExit(($Seconds + 40) * 1000)) { $p.Kill(); $summary[$name] = 'timeout'; continue }
  $lines = Get-Content -LiteralPath $log | Where-Object { $_ -match '\[player-timing\]' } | Select-Object -Skip 1
  $row = [ordered]@{ exit = $p.ExitCode; samples = @($lines).Count }
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
} else { $json }
