# G2.5: does the fullscreen control window cost the video anything? Runs the QML app in
# fullscreen with the control window held shown, then held hidden (--full-bar), and
# averages the engine's own [player-timing] P95 fields over the steady part of each
# run. PresentMon is not on this machine; this compares the engine's present timing.
param(
  [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app',
  [string]$Clip = 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips\test_av_1080p.mp4',
  [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\goal\g2.5\compare',
  [int]$Seconds = 20,
  [int]$Rounds = 2
)
New-Item -ItemType Directory -Force $Out | Out-Null
$fields = 'graphSubmitP95Ms','gpuReadyP95Ms','presentP95Ms','returnAbsP95Ms'
$rows = @()
for ($round = 1; $round -le $Rounds; $round++) {
  foreach ($mode in 'shown','hidden') {
    $log = Join-Path $Out "$mode-$round.log"
    $env:VEYRA_LOG_FILE = $log
    $p = Start-Process -FilePath (Join-Path $App 'veyra_qml_ui.exe') -ArgumentList @('--page','min','--full-bar',$mode,('"' + $Clip + '"')) -PassThru
    Start-Sleep -Seconds $Seconds
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 800
    $all = Get-Content -LiteralPath $log -Encoding utf8
    # Steady state: timing lines after fullscreen was entered, first two skipped.
    $at = ($all | Select-String -Pattern 'ui-fullscreen\] enabled=true' | Select-Object -First 1).LineNumber
    $lines = @($all | Select-Object -Skip ([int]$at) | Where-Object { $_ -match '\[player-timing\]' } | Select-Object -Skip 2)
    $row = [ordered]@{ mode = $mode; round = $round; samples = $lines.Count
                       barVisible = (@($all | Select-String 'control window visible=true').Count -gt 0) }
    foreach ($f in $fields) {
      $v = @($lines | ForEach-Object { if ($_ -match "$f=([0-9.]+)") { [double]$Matches[1] } })
      $row[$f] = if ($v.Count) { [math]::Round(($v | Measure-Object -Average).Average, 3) } else { $null }
    }
    $d = @($lines | ForEach-Object { if ($_ -match 'displaySubmits=([0-9]+)') { [double]$Matches[1] } })
    $row['displaySubmitsPerLine'] = if ($d.Count -gt 1) { [math]::Round(($d[-1] - $d[0]) / ($d.Count - 1), 1) } else { $null }
    $rows += [pscustomobject]$row
  }
}
Remove-Item Env:VEYRA_LOG_FILE
$rows | Format-Table -AutoSize | Out-String -Width 200
$rows | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Out 'summary.json') -Encoding utf8
