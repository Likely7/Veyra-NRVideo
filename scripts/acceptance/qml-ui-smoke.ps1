[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][ValidateSet('qml')][string]$UiTarget,
  [Parameter(Mandatory = $true)][string]$PlayerExe,
  [Parameter(Mandatory = $true)][string]$BuildDirectory,
  [Parameter(Mandatory = $true)][string]$StagingDirectory,
  [string]$OutputDirectory = '',
  [string]$TempDirectory = '',
  [string]$FixtureRoot = '',
  [ValidateRange(1, 270000)][int]$ExitAfterMs = 1800,
  [switch]$AllowMissingPlaybackFixture
)

# QML has a different command-line contract from the Win32 shell. This smoke
# test deliberately uses only options implemented by apps/veyra-qml/main.cpp.
$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = [IO.Path]::GetFullPath((Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra')).TrimEnd('\') + '\'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $artifactRoot 'logs\ui-qml-migration-20260927\qml-smoke'
}
if ([string]::IsNullOrWhiteSpace($TempDirectory)) {
  $TempDirectory = Join-Path $artifactRoot 'tmp\ui-qml-migration-20260927\qml-smoke'
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$tempRoot = [IO.Path]::GetFullPath($TempDirectory)
foreach ($path in @($outputRoot, $tempRoot)) {
  if (-not $path.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Smoke output and TEMP must be under E:\项目\Veyra: $path"
  }
}
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$outPath = Join-Path $outputRoot ('run-' + $runId)
$tempPath = Join-Path $tempRoot ('run-' + $runId)
foreach ($path in @($outPath, $tempPath)) {
  if (Test-Path -LiteralPath $path) {
    throw "Smoke run directory already exists; refusing to reuse: $path"
  }
  [IO.Directory]::CreateDirectory($path) | Out-Null
}
$env:TEMP = $tempPath
$env:TMP = $tempPath
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QML_DISK_CACHE_PATH = Join-Path $tempPath 'qmlcache'
$entryScript = Join-Path $PSScriptRoot 'resolve-ui-migration-entry.ps1'
$entryArgs = @{
  Root = $rootPath; UiTarget = $UiTarget; PlayerExe = $PlayerExe
  BuildDirectory = $BuildDirectory; StagingDirectory = $StagingDirectory
  ArtifactDirectory = $outPath; OutputFile = (Join-Path $outPath 'entry.json')
}
$null = & $entryScript @entryArgs
if ($UiTarget -ne 'qml') {
  throw 'qml-ui-smoke.ps1 requires -UiTarget qml and veyra_qml_ui.exe.'
}

$exePath = (Resolve-Path -LiteralPath $PlayerExe).Path
$workingDirectory = Split-Path -Parent $exePath
$dataPath = Join-Path $outPath 'data'
[IO.Directory]::CreateDirectory($dataPath) | Out-Null
$cases = @(
  [pscustomobject]@{ name = 'home'; args = @('--page', 'home') },
  [pscustomobject]@{ name = 'minimal'; args = @('--page', 'minimal') },
  [pscustomobject]@{ name = 'professional'; args = @('--page', 'pro', '--tab', 'quality') },
  [pscustomobject]@{ name = 'node'; args = @('--page', 'node') },
  [pscustomobject]@{ name = 'export'; args = @('--page', 'export') },
  [pscustomobject]@{ name = 'settings'; args = @('--page', 'settings') },
  [pscustomobject]@{ name = 'dialog-capture'; args = @('--page', 'home', '--dialog', 'capture') }
)
$clip = if ([string]::IsNullOrWhiteSpace($FixtureRoot)) { $null } else { Join-Path ([IO.Path]::GetFullPath($FixtureRoot)) 'test_av_1080p.mp4' }
if ($clip -and (Test-Path -LiteralPath $clip -PathType Leaf)) {
  $cases += [pscustomobject]@{ name = 'playback-professional'; args = @('--page', 'pro', '--tab', 'quality', $clip) }
} elseif (-not $AllowMissingPlaybackFixture) {
  $fixtureDescription = if ($clip) { $clip } else { 'FixtureRoot was not provided' }
  throw "Missing required QML playback fixture: $fixtureDescription. Pass -AllowMissingPlaybackFixture only for an explicit UI-only run that records the playback case as skipped."
} else {
  $fixtureDescription = if ($clip) { $clip } else { 'FixtureRoot was not provided' }
  $cases += [pscustomobject]@{ name = 'playback-professional'; args = @(); skipped = $true; skipReason = "missing fixture: $fixtureDescription" }
}

$results = [Collections.Generic.List[object]]::new()
foreach ($case in $cases) {
  if ($case.PSObject.Properties['skipped'] -and $case.skipped) {
    # A skipped playback case is explicit evidence that U4 is incomplete. It
    # must never be serialized as passed or let unattended mode advance.
    $results.Add([ordered]@{ name = $case.name; status = 'skipped'; passed = $false; detail = [string]$case.skipReason; exit = $null })
    continue
  }
  $stdout = Join-Path $outPath ($case.name + '.stdout.log')
  $stderr = Join-Path $outPath ($case.name + '.stderr.log')
  $qmlLog = Join-Path $outPath ($case.name + '.qml.log')
  $savedLog = $env:VEYRA_LOG_FILE
  try {
    $env:VEYRA_LOG_FILE = $qmlLog
    $arguments = @($case.args | ForEach-Object { $_ }) + @('--data-dir', $dataPath, '--reduced-motion', '--exit-after', $ExitAfterMs)
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exePath
    $psi.WorkingDirectory = $workingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($psi.PSObject.Properties.Name -contains 'ArgumentList') {
      foreach ($argument in $arguments) { [void]$psi.ArgumentList.Add([string]$argument) }
    } else {
      $quote = { param([string]$value) '"' + $value.Replace('"', '\"') + '"' }
      $psi.Arguments = (($arguments | ForEach-Object { & $quote ([string]$_) }) -join ' ')
    }
    $p = [Diagnostics.Process]::Start($psi)
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()
    $finished = $p.WaitForExit(($ExitAfterMs + 30000))
    if (-not $finished) {
      try { $p.Kill() } catch { }
      $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stdout -Encoding UTF8
      $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stderr -Encoding UTF8
      $results.Add([ordered]@{ name = $case.name; status = 'fail'; passed = $false; detail = 'timeout'; exit = $null })
      continue
    }
    $p.WaitForExit()
    $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stdout -Encoding UTF8
    $stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath $stderr -Encoding UTF8
    $text = if (Test-Path -LiteralPath $qmlLog) { Get-Content -LiteralPath $qmlLog -Raw } else { '' }
    $ok = ($p.ExitCode -eq 0) -and ($text -match 'Veyra QML session started') -and
          ($text -notmatch 'no root object|objectCreationFailed|QML Error|invalid video probe')
    $detail = "exit=$($p.ExitCode) log=$qmlLog"
    if (-not $ok -and [string]::IsNullOrWhiteSpace($text)) { $detail += '; qml log is empty' }
    $results.Add([ordered]@{ name = $case.name; status = if ($ok) { 'pass' } else { 'fail' }; passed = $ok; detail = $detail; exit = $p.ExitCode })
  } finally {
    if ($null -eq $savedLog) { Remove-Item Env:VEYRA_LOG_FILE -ErrorAction SilentlyContinue }
    else { $env:VEYRA_LOG_FILE = $savedLog }
  }
}

$record = [ordered]@{
  schema = 'veyra.qml-ui-smoke.v2'
  runDirectory = $outPath
  tempDirectory = $tempPath
  uiTarget = $UiTarget
  playbackFixture = $clip
  allowMissingPlaybackFixture = [bool]$AllowMissingPlaybackFixture
  cases = $results
  failures = @($results | Where-Object { $_.status -eq 'fail' }).Count
  skipped = @($results | Where-Object { $_.status -eq 'skipped' }).Count
  requiredFailures = @($results | Where-Object { $_.status -in @('fail', 'skipped') }).Count
  passed = (@($results | Where-Object { $_.status -in @('fail', 'skipped') }).Count -eq 0)
}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outPath 'result.json') -Encoding UTF8
if ($record.requiredFailures -gt 0) {
  Write-Host "QML UI SMOKE INCOMPLETE: $outPath (failures=$($record.failures), skipped=$($record.skipped))"
  exit 1
}
Write-Host "QML UI SMOKE PASS: $outPath"
exit 0
