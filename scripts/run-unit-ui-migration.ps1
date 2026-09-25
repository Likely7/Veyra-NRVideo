# Runs the unit/contract test executables of the staged app and writes a JSON summary.
# Tests that need hardware or media (capture, remote play, endpoint audio) are skipped:
# they are listed in $skip with the reason, never reported as passed.
param([string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\app',
      [string]$Out = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\unit',
      [int]$TimeoutSeconds = 120)
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $Out | Out-Null
$env:TEMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'; $env:TMP = $env:TEMP
# Several contract tests open a fixture through a path relative to the CWD
# (loop/local/fixed_clips/corpus/...). They run from the staged app directory,
# so stage the fixture tree beside the executables first; otherwise they report
# "corpus missing" and look like a regression they are not.
$fixtureRoot = Join-Path $App 'loop'
if (-not (Test-Path -LiteralPath (Join-Path $fixtureRoot 'localixed_clips\corpus'))) {
  $sourceRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'loop'
  if (Test-Path -LiteralPath $sourceRoot) { Copy-Item -LiteralPath $sourceRoot -Destination $fixtureRoot -Recurse -Force }
}
# name -> reason. Anything not listed and matching *_tests.exe runs with no arguments.
$skip = @{
  'veyra_capture_tests.exe' = 'needs a capture device'
  'veyra_wasapi_input_tests.exe' = 'needs an explicit recording endpoint'
  'veyra_capture_latency_tests.exe' = 'needs a capture device'
}
$results = @()
Get-ChildItem -LiteralPath $App -Filter 'veyra_*_tests.exe' | Sort-Object Name | ForEach-Object {
  $name = $_.Name
  if ($skip.ContainsKey($name)) { $results += [pscustomobject]@{ name = $name; status = 'skipped'; exit = $null; seconds = 0; reason = $skip[$name] }; return }
  $log = Join-Path $Out ($_.BaseName + '.log')
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $_.FullName -WorkingDirectory $App -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError ($log + '.err')
  $null = $p.Handle  # cache the handle so ExitCode is readable after exit
  if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { $p.Kill(); $status = 'timeout'; $code = $null }
  else { $p.WaitForExit(); $code = $p.ExitCode; $status = if ($code -eq 0) { 'pass' } else { 'fail' } }
  $results += [pscustomobject]@{ name = $name; status = $status; exit = $code; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1); reason = '' }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Out 'summary.json') -Encoding utf8
$results | Group-Object status | ForEach-Object { "{0}={1}" -f $_.Name, $_.Count }
$results | Where-Object { $_.status -in 'fail', 'timeout' } | ForEach-Object { "{0} {1} exit={2}" -f $_.status, $_.name, $_.exit }
