[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][ValidateSet('qml')][string]$UiTarget,
  [ValidateSet('veyra_qml_ui', 'veyra_qml_data_tests', 'veyra_qml_easing_tests', 'veyra_qml_quick_tests')][string[]]$Targets = @(),
  [string]$Out = '',
  [string]$Log = '',
  [string]$Temp = ''
)

# A migration build is explicitly one UI contract. Reusing a cache configured
# for the other contract can produce a valid executable with the wrong entry.
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra'
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + ([Guid]::NewGuid().ToString('N').Substring(0, 8))
if ([string]::IsNullOrWhiteSpace($Out)) {
  $Out = Join-Path $artifactRoot ("build\ui-qml-migration-20260927\" + $runId + "\" + $UiTarget)
}
if ([string]::IsNullOrWhiteSpace($Log)) {
  $Log = Join-Path $artifactRoot ("logs\ui-qml-migration-20260927\" + $runId + "\build-" + $UiTarget + '.log')
}
if ([string]::IsNullOrWhiteSpace($Temp)) {
  $Temp = Join-Path $artifactRoot ("tmp\ui-qml-migration-20260927\" + $runId + "\" + $UiTarget)
}
$outFull = [IO.Path]::GetFullPath($Out)
$logFull = [IO.Path]::GetFullPath($Log)
$tempFull = [IO.Path]::GetFullPath($Temp)
$rootPrefix = [IO.Path]::GetFullPath($artifactRoot).TrimEnd('\') + '\'
foreach ($path in @($outFull, $logFull, $tempFull)) {
  if (-not $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build output, logs and TEMP must be under ${artifactRoot}: $path"
  }
}

function Assert-NewDirectory([string]$Path, [string]$Label) {
  if (Test-Path -LiteralPath $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "$Label is not a directory: $Path" }
    $existing = @(Get-ChildItem -LiteralPath $Path -Force)
    if ($existing.Count -gt 0) { throw "$Label must be a new empty directory; refusing to reuse: $Path" }
  } else {
    [IO.Directory]::CreateDirectory($Path) | Out-Null
  }
}

Assert-NewDirectory $outFull 'Build output'
[IO.Directory]::CreateDirectory((Split-Path -Parent $logFull)) | Out-Null
if (Test-Path -LiteralPath $logFull) { throw "Build log already exists; refusing to overwrite: $logFull" }
[IO.Directory]::CreateDirectory((Split-Path -Parent $tempFull)) | Out-Null
Assert-NewDirectory $tempFull 'Build TEMP'
[IO.Directory]::CreateDirectory((Split-Path -Parent $logFull)) | Out-Null
$env:VEYRA_OUT = $outFull
$env:VEYRA_TMP = $tempFull
$env:TEMP = $tempFull
$env:TMP = $tempFull
$env:VEYRA_UI_TARGET = $UiTarget
$env:VEYRA_QT_DIR = Join-Path $artifactRoot 'deps\qt\6.8.3\msvc2022_64'
$cmd = Join-Path $PSScriptRoot 'build-ui-migration.cmd'
$targetArgs = if ($Targets.Count -gt 0) { ' ' + ($Targets -join ' ') } else { '' }
& cmd.exe /d /c "`"$cmd`"$targetArgs > `"$logFull`" 2>&1"
$code = $LASTEXITCODE
if (Test-Path -LiteralPath $logFull) { Get-Content -LiteralPath $logFull -Tail 25 -Encoding utf8 }
"exit=$code"
exit $code
