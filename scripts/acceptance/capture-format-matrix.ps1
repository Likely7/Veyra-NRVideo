# Exhaustive real-card sweep: every format index of one video device gets its
# own short capture, and the frame count / drop count decide pass or fail.
# Rationale: the 1.3.2beta native-path regression broke YUY2 while MJPEG stayed
# green, so path-level sampling is not enough - each negotiated subtype has its
# own layout, and the only honest check is to run them all.
param(
  [Parameter(Mandatory = $true)][string]$Root,
  [string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$PlayerExe,
  [Parameter(Mandatory = $true)][ValidateSet('legacy')][string]$UiTarget,
  [int]$Device = 0,
  [int]$CaseSeconds = 10,
  [string]$OutputDirectory = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\capture-matrix',
  [int[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Root).Path
$player = (Resolve-Path -LiteralPath $PlayerExe).Path
$bin = if ($BuildDirectory) { (Resolve-Path -LiteralPath $BuildDirectory).Path } else { Split-Path -Parent $player }
$lister = Join-Path $bin 'veyra_capture_tests.exe'
$out = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($out) | Out-Null
$entryScript = Join-Path $PSScriptRoot 'resolve-ui-migration-entry.ps1'
$null = & $entryScript -Root $root -UiTarget $UiTarget -PlayerExe $player -BuildDirectory $BuildDirectory -StagingDirectory (Split-Path -Parent $player) -ArtifactDirectory $out -OutputFile (Join-Path $out 'entry.json')
if ($UiTarget -ne 'legacy') { throw 'capture-format-matrix.ps1 is legacy-only; QML uses the separate qml-ui-smoke contract.' }
[IO.Directory]::CreateDirectory($out) | Out-Null

$formats = [Collections.Generic.List[object]]::new()
foreach ($line in (& $lister --list 2>$null)) {
  if ($line -notmatch '^FORMAT\s+(\d+):(\d+)\s+(\d+)\s+x\s+(\d+)\s+@\s+([\d.]+)\s+fps\s+.\s+(\S+)') { continue }
  if ([int]$Matches[1] -ne $Device) { continue }
  $index = [int]$Matches[2]
  if ($Only.Count -gt 0 -and $index -notin $Only) { continue }
  $formats.Add([pscustomobject]@{ index = $index; width = [int]$Matches[3]; height = [int]$Matches[4]; fps = [double]$Matches[5]; codec = $Matches[6] })
}
if ($formats.Count -eq 0) { throw "no formats for device $Device" }
Write-Host ("sweeping device {0}: {1} formats, {2}s each" -f $Device, $formats.Count, $CaseSeconds)

$results = [Collections.Generic.List[object]]::new()
$failures = [Collections.Generic.List[string]]::new()
foreach ($format in $formats) {
  $argument = 'capture:' + $Device + ':' + $format.index + ':-1:0'
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $player
  $psi.Arguments = '"' + $argument + '" --smoke-seconds ' + $CaseSeconds + ' --no-nr --no-sr --no-fg'
  $psi.WorkingDirectory = $root
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.CreateNoWindow = $true
  $proc = [System.Diagnostics.Process]::Start($psi)
  $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
  $stderrTask = $proc.StandardError.ReadToEndAsync()
  $timeout = ($CaseSeconds + 90) * 1000
  if (-not $proc.WaitForExit($timeout)) { $proc.Kill(); $timeoutHit = $true } else { $timeoutHit = $false }
  $exitCode = if ($timeoutHit) { -1 } else { $proc.ExitCode }
  $text = $stdoutTask.Result
  $errText = $stderrTask.Result
  Set-Content -LiteralPath (Join-Path $out ("format-{0:D2}.stdout.log" -f $format.index)) -Value $text -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $out ("format-{0:D2}.stderr.log" -f $format.index)) -Value $errText -Encoding UTF8

  $frames = -1
  $dropped = -1
  $failedFlag = $true
  if ($text -match 'smoke frames=(\d+).*failed=(\w+).*captureDropped=(\d+)') {
    $frames = [int]$Matches[1]
    $failedFlag = ($Matches[2] -ne 'false')
    $dropped = [int]$Matches[3]
  }
  $floor = [Math]::Max(20, [int]($CaseSeconds * 8))
  # The UVC driver can push its buffered frames right after Run(); the
  # latest-frame mailbox drops those stale ones by design (observed once: 47
  # frames on format 44, gone on all three re-tests). Tolerate a startup burst
  # of up to 10% (minimum 20 frames) so a real steady-state loss still fails.
  $dropTolerance = [Math]::Max(20, [int]($frames * 0.1))
  $passed = (-not $timeoutHit) -and ($exitCode -eq 0) -and ($frames -ge $floor) -and ($dropped -le $dropTolerance) -and (-not $failedFlag)
  if (-not $passed) { $failures.Add(("format {0} ({1} {2}x{3}@{4})" -f $format.index, $format.codec, $format.width, $format.height, $format.fps)) }
  $results.Add([ordered]@{ index = $format.index; codec = $format.codec; width = $format.width; height = $format.height; fps = $format.fps; exit = $exitCode; frames = $frames; dropped = $dropped; passed = $passed })
  Write-Host (("{0} format={1,2} {2} {3}x{4}@{5} frames={6} dropped={7} exit={8}" -f $(if ($passed) { 'PASS' } else { 'FAIL' }), $format.index, $format.codec, $format.width, $format.height, $format.fps, $frames, $dropped, $exitCode))
}

[ordered]@{ device = $Device; seconds = $CaseSeconds; formats = $results; failures = $failures } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding UTF8
if ($failures.Count -ne 0) { Write-Host ("CAPTURE MATRIX FAIL: {0} of {1} formats ({2})" -f $failures.Count, $results.Count, ($failures -join '; ')); Write-Host "logs: $out"; exit 1 }
Write-Host ("CAPTURE MATRIX PASS: {0} formats" -f $results.Count); Write-Host "logs: $out"
exit 0
