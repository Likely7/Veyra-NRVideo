# Runs veyra_chain_hash_probe for every case and compares with a stored baseline.
#   -Record : write the result as the new baseline (S0.3 only)
#   default : compare against the baseline and report every mismatching case
param([string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\app',
      [string]$Clip = 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips\test_av_1080p.mp4',
      [string]$Baseline = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\hash-baseline.json',
      [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\hash',
      [int]$Frames = 24,
      [string[]]$Cases = @('passthrough','nr','nr-style2','nr-residual','nr-protect','nr-temporal','dlss-sr','vsr','sr-nr','nr-sr','vsr-nr','color','color-nr','nr2','nr3','nr4'),
      [switch]$Record)
$env:TEMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'; $env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $Out | Out-Null
$probe = Join-Path $App 'veyra_chain_hash_probe.exe'
$result = [ordered]@{}
foreach ($c in $Cases) {
  $file = Join-Path $Out "$c.json"
  $p = Start-Process -FilePath $probe -ArgumentList @('--input', "`"$Clip`"", '--case', $c, '--frames', $Frames, '--out', "`"$file`"") -WorkingDirectory $App -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Out "$c.stdout.log") -RedirectStandardError (Join-Path $Out "$c.stderr.log")
  $null = $p.Handle
  if (-not $p.WaitForExit(120000)) { $p.Kill(); $result[$c] = 'timeout'; continue }
  $p.WaitForExit()
  if ($p.ExitCode -ne 0) { $result[$c] = "exit $($p.ExitCode)"; continue }
  $result[$c] = (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json).hashes
}
if ($Record) {
  $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Baseline -Encoding utf8
  "recorded $($result.Count) cases -> $Baseline"
  $result.GetEnumerator() | Where-Object { $_.Value -is [string] } | ForEach-Object { "ERROR $($_.Key): $($_.Value)" }
  return
}
$base = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
$bad = 0
foreach ($c in $Cases) {
  $now = $result[$c]; $was = $base.$c
  if ($now -is [string]) { "FAIL $c : $now"; $bad++; continue }
  if ($null -eq $was) { "NEW  $c (no baseline entry)"; continue }
  if ($null -eq $now -or $now.Count -eq 0) { "FAIL $c : no frames produced"; $bad++; continue }
  $diff = @(for ($i = 0; $i -lt [math]::Max($now.Count, $was.Count); $i++) { if ($now[$i] -ne $was[$i]) { $i } })
  if ($diff.Count) { "FAIL $c : $($diff.Count) of $($was.Count) frames differ (first $($diff[0]))"; $bad++ } else { "same $c" }
}
"mismatching cases: $bad"
exit $bad
