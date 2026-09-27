[CmdletBinding()]
param(
  [string]$Evidence = '',
  [switch]$Initialize
)

# This is a path-and-history guard, not a test gate. It prevents the UI
# migration from silently growing into another engine/pipeline migration.
# Existing out-of-scope work is recorded and frozen; it is never erased here.
$ErrorActionPreference = 'Stop'

# These are deliberately constants. An unattended run must not be able to
# select a different checkout or baseline and call that result evidence for
# this migration.
$canonicalRoot = 'C:\Users\123\Desktop\Veyra DLSS Video Player'
$canonicalBranch = 'codex/ui-qml-migration-20260925'
$canonicalMainCommit = 'df41580f7fa0d2b26718f355640470e8cb94b324'
$canonicalBaselineRelative = 'docs/UI_MIGRATION_SCOPE_BASELINE_2026-09-27.json'
$canonicalGuardRelative = 'scripts/acceptance/ui-migration-scope-guard.ps1'

function Normalize-RepoPath([string]$Path) {
  if ($null -eq $Path) { return '' }
  $value = $Path.Trim().Replace('\', '/')
  if ($value.StartsWith('./')) { $value = $value.Substring(2) }
  return $value
}

function Is-ControlPath([string]$RepoPath) {
  $normalized = Normalize-RepoPath $RepoPath
  return $normalized -eq $canonicalBaselineRelative -or $normalized -eq $canonicalGuardRelative
}

function Full([string]$Path) {
  return [IO.Path]::GetFullPath($Path)
}

function Invoke-Git([string[]]$Arguments) {
  $output = @(& git -C $script:rootPath @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed: $($output -join ' ')"
  }
  return $output
}

function Get-BranchName {
  return ([string](Invoke-Git @('rev-parse', '--abbrev-ref', 'HEAD'))).Trim()
}

function Get-Commit([string]$Ref) {
  return ([string](Invoke-Git @('rev-parse', '--verify', $Ref))).Trim()
}

function Get-HeadBlob([string]$Commit, [string]$RepoPath) {
  $spec = $Commit + ':' + $RepoPath
  # PowerShell 5.1 promotes native stderr to a terminating error when the
  # process is run under ErrorActionPreference=Stop. Missing paths are
  # expected for untracked UI files, so lower the preference only around this
  # probe and restore it immediately after collecting the exit code.
  $previousErrorAction = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    $output = @(& git -C $script:rootPath rev-parse --verify $spec 2>$null)
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }
  if ($exitCode -ne 0) { return $null }
  return ([string]$output[0]).Trim()
}

function Get-WorktreeHash([string]$RepoPath) {
  $full = Join-Path $script:rootPath ($RepoPath.Replace('/', '\'))
  if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { return $null }
  return (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Unquote-GitPath([string]$Value) {
  $text = $Value.Trim()
  if ($text.Length -ge 2 -and $text.StartsWith('"') -and $text.EndsWith('"')) {
    $text = $text.Substring(1, $text.Length - 2)
    $text = $text -replace '\\"', '"'
    $text = $text -replace '\\\\', '\'
  }
  return $text
}

function Get-StatusEntries {
  $entries = [Collections.Generic.List[object]]::new()
  foreach ($lineObject in @(Invoke-Git @('status', '--porcelain=v1', '--untracked-files=all'))) {
    $line = [string]$lineObject
    if ($line.Length -lt 4) { continue }
    $status = $line.Substring(0, 2)
    $rawPath = Unquote-GitPath $line.Substring(3)
    # A rename line contains both names. Keep both so a rename cannot hide a
    # forbidden path from the guard.
    $paths = if ($rawPath -match '^(.*) -> (.*)$') { @($Matches[1], $Matches[2]) } else { @($rawPath) }
    foreach ($path in $paths) {
      $normalized = Normalize-RepoPath $path
      if (-not [string]::IsNullOrWhiteSpace($normalized)) {
        $entries.Add([ordered]@{ path = $normalized; status = $status })
      }
    }
  }
  return @($entries)
}

function Get-ChangedPathMap([string]$MainCommit, [string]$HeadCommit) {
  $map = @{}
  foreach ($lineObject in @(Invoke-Git @('diff', '--name-only', '--diff-filter=ACDMRTUXB', $MainCommit, $HeadCommit))) {
    $path = Normalize-RepoPath ([string]$lineObject)
    if (-not [string]::IsNullOrWhiteSpace($path) -and -not (Is-ControlPath $path)) { $map[$path] = 'committed' }
  }
  foreach ($entry in @(Get-StatusEntries)) {
    if (-not (Is-ControlPath ([string]$entry.path))) {
      $map[$entry.path] = [string]$entry.status
    }
  }
  return $map
}

function Get-UiScopeReason([string]$RepoPath) {
  $path = Normalize-RepoPath $RepoPath
  switch -Regex ($path) {
    '^qml/' { return 'qml-source' }
    '^apps/veyra-qml/' { return 'qml-entry' }
    '^include/veyra/ui/Qml[^/]*\.(h|hpp)$' { return 'qml-bridge-header' }
    '^include/veyra/ui/ThumbnailProvider\.(h|hpp)$' { return 'qml-thumbnail-header' }
    '^src/ui/Qml[^/]*\.(cpp|h|hpp)$' { return 'qml-bridge-source' }
    '^src/ui/ThumbnailProvider\.(cpp|h|hpp)$' { return 'qml-thumbnail-source' }
    '^tests/qml/' { return 'qml-tests' }
    '^tests/unit/Qml[^/]*\.(cpp|h|hpp)$' { return 'qml-unit-test' }
    '^tools/qt_probe/' { return 'qt-probe' }
    '^prototypes/ui-redesign-2026-09-25/' { return 'approved-design-prototype' }
    '^scripts/build-ui-migration\.(cmd|ps1)$' { return 'qml-build-script' }
    '^scripts/build-qt-probe\.(cmd|ps1)$' { return 'qml-build-script' }
    '^scripts/stage-ui-migration\.ps1$' { return 'qml-stage-script' }
    '^scripts/run-unit-ui-migration\.ps1$' { return 'qml-test-script' }
    # Chain-hash and performance scripts exercise the frozen legacy/backend
    # path. They remain historical evidence and must stay frozen during UI
    # migration, even though their filenames contain "ui-migration".
    '^scripts/acceptance/(qml-ui-smoke|resolve-ui-migration-entry|test-ui-migration-entry-contract)\.ps1$' { return 'qml-acceptance-script' }
    '^docs/UI[^/]*\.md$' { return 'qml-documentation' }
    '^docs/UI_MIGRATION_SCOPE_(BASELINE|AUDIT)_2026-09-27\.(json|md)$' { return 'qml-audit-documentation' }
    '^docs/WORKLOG\.md$' { return 'worklog' }
    default { return $null }
  }
}

$script:rootPath = (Resolve-Path -LiteralPath $canonicalRoot -ErrorAction Stop).Path
$canonicalRootPath = Full $canonicalRoot
$canonicalGuardPath = Full (Join-Path $canonicalRoot $canonicalGuardRelative)
$baselinePath = Full (Join-Path $canonicalRoot $canonicalBaselineRelative)
$invokedGuardPath = Full $PSCommandPath
if (-not $script:rootPath.Equals($canonicalRootPath, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Canonical repository root mismatch: expected=$canonicalRootPath actual=$script:rootPath"
}
if (-not $invokedGuardPath.Equals($canonicalGuardPath, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Scope guard must run from the canonical path: expected=$canonicalGuardPath actual=$invokedGuardPath"
}
$repoPrefix = (Full $script:rootPath).TrimEnd('\') + '\'
$projectDirectoryName = [string]([char]0x9879) + [string]([char]0x76ee)
$artifactRoot = Join-Path (Join-Path 'E:\' $projectDirectoryName) 'Veyra'
if ([string]::IsNullOrWhiteSpace($Evidence)) {
  $Evidence = Join-Path $artifactRoot 'logs\ui-qml-migration-20260927\scope-guard\result.json'
}
$evidencePath = Full $Evidence
$artifactPrefix = (Full $artifactRoot).TrimEnd('\') + '\'
if (-not $baselinePath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Baseline must be inside the repository: $baselinePath"
}
if (-not $evidencePath.StartsWith($artifactPrefix, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Evidence must be under ${artifactRoot}: $evidencePath"
}

$branch = Get-BranchName
$head = Get-Commit 'HEAD'
$main = Get-Commit 'main'
$statusEntries = @(Get-StatusEntries)
$changedMap = Get-ChangedPathMap $main $head
$allPaths = @($changedMap.Keys | Sort-Object)

if ($branch -ne $canonicalBranch) {
  throw "Scope guard branch mismatch: expected=$canonicalBranch actual=$branch"
}
if ($main -ne $canonicalMainCommit) {
  throw "main moved since the migration baseline: expected=$canonicalMainCommit actual=$main"
}

function New-PathRecord([string]$Path) {
  $status = if ($changedMap.ContainsKey($Path)) { [string]$changedMap[$Path] } else { '' }
  return [ordered]@{
    path = $Path
    status = $status
    scope = (Get-UiScopeReason $Path)
    headBlob = (Get-HeadBlob $head $Path)
    worktreeSha256 = (Get-WorktreeHash $Path)
  }
}

if ($Initialize) {
  if (Test-Path -LiteralPath $baselinePath -PathType Leaf) {
    throw "Baseline already exists; refusing to replace it: $baselinePath"
  }
  $records = foreach ($path in $allPaths) { New-PathRecord $path }
  $frozen = @($records | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.scope) })
  $record = [ordered]@{
    schema = 'veyra.ui-migration.scope-baseline.v1'
    generatedUtc = [DateTime]::UtcNow.ToString('o')
    root = $script:rootPath
    branch = $branch
    mainCommit = $main
    initialHead = $head
    policy = 'Existing out-of-scope paths are frozen at these hashes. New out-of-scope paths fail. UI paths may continue to change.'
    control = [ordered]@{
      baselinePath = $canonicalBaselineRelative
      guardPath = $canonicalGuardRelative
      guardSha256 = Get-WorktreeHash $canonicalGuardRelative
      requiresTrackedCleanControlFiles = $true
    }
    initialChangedPaths = @($records)
    frozenPaths = @($frozen)
  }
  [IO.Directory]::CreateDirectory((Split-Path -Parent $baselinePath)) | Out-Null
  $record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
  $result = [ordered]@{ status = 'initialized'; branch = $branch; head = $head; main = $main; initialChanged = $records.Count; frozen = $frozen.Count; baseline = $baselinePath }
  Write-Output ($result | ConvertTo-Json -Depth 6)
} else {
  if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    throw "Missing scope baseline. Run with -Initialize once after the audit is complete: $baselinePath"
  }
  $baselineDocument = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$baselineDocument.schema -ne 'veyra.ui-migration.scope-baseline.v1') { throw 'Unsupported scope baseline schema' }
  if ([string]$baselineDocument.root -ne $script:rootPath) { throw "Scope baseline root mismatch: $($baselineDocument.root)" }
  if ([string]$baselineDocument.branch -ne $canonicalBranch) { throw "Scope baseline branch is not canonical: $($baselineDocument.branch)" }
  if ([string]$baselineDocument.mainCommit -ne $canonicalMainCommit) { throw "Scope baseline main is not canonical: $($baselineDocument.mainCommit)" }

  $controlFailures = [Collections.Generic.List[string]]::new()
  if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    $controlFailures.Add("missing control file: $baselinePath")
  }
  $controlFiles = @(
    [ordered]@{ path = $canonicalBaselineRelative; full = $baselinePath },
    [ordered]@{ path = $canonicalGuardRelative; full = $canonicalGuardPath }
  )
  foreach ($control in $controlFiles) {
    $indexLines = @(& git -C $script:rootPath ls-files --stage -- $control.path 2>$null)
    $indexExit = $LASTEXITCODE
    if ($indexExit -ne 0 -or $indexLines.Count -eq 0) {
      $controlFailures.Add("control file is not tracked: $($control.path)")
      continue
    }
    $dirty = @($statusEntries | Where-Object { [string]$_.path -eq [string]$control.path })
    if ($dirty.Count -gt 0) {
      $controlFailures.Add("control file has worktree/index changes: $($control.path)")
    }
  }
  $control = $baselineDocument.control
  if ($null -eq $control) {
    $controlFailures.Add('baseline is missing the control integrity record')
  } else {
    if ([string]$control.baselinePath -ne $canonicalBaselineRelative) { $controlFailures.Add('baseline control path is not canonical') }
    if ([string]$control.guardPath -ne $canonicalGuardRelative) { $controlFailures.Add('guard control path is not canonical') }
    $guardHash = Get-WorktreeHash $canonicalGuardRelative
    if ([string]$control.guardSha256 -ne $guardHash) { $controlFailures.Add('scope guard content hash changed') }
    $guardBlob = Get-HeadBlob $head $canonicalGuardRelative
    if ([string]$control.guardHeadBlob -ne $guardBlob) { $controlFailures.Add('scope guard HEAD blob changed or is not recorded') }
    if (-not [bool]$control.requiresTrackedCleanControlFiles) { $controlFailures.Add('baseline does not require tracked clean control files') }
  }
  if ($controlFailures.Count -gt 0) {
    $controlFailures | ForEach-Object { Write-Error $_ }
    throw "UI migration control integrity failed with $($controlFailures.Count) violation(s)."
  }

  $failures = [Collections.Generic.List[string]]::new()
  $initial = @{}
  foreach ($entry in @($baselineDocument.initialChangedPaths)) { $initial[[string]$entry.path] = $entry }
  foreach ($entry in @($baselineDocument.frozenPaths)) {
    $path = [string]$entry.path
    if (-not $changedMap.ContainsKey($path)) {
      $failures.Add("frozen path disappeared from branch/worktree: $path")
      continue
    }
    $currentHeadBlob = Get-HeadBlob $head $path
    $expectedHeadBlob = if ($null -eq $entry.headBlob) { $null } else { [string]$entry.headBlob }
    if ($currentHeadBlob -ne $expectedHeadBlob) {
      $failures.Add("frozen committed content changed: $path")
    }
    $currentWorktreeHash = Get-WorktreeHash $path
    $expectedWorktreeHash = if ($null -eq $entry.worktreeSha256) { $null } else { [string]$entry.worktreeSha256 }
    if ($currentWorktreeHash -ne $expectedWorktreeHash) {
      $failures.Add("frozen worktree content changed: $path")
    }
  }
  foreach ($path in @($changedMap.Keys | Sort-Object)) {
    if ($initial.ContainsKey($path)) { continue }
    $reason = Get-UiScopeReason $path
    if ([string]::IsNullOrWhiteSpace($reason)) {
      $failures.Add("new out-of-scope path: $path")
    }
  }
  $result = [ordered]@{
    status = if ($failures.Count -eq 0) { 'pass' } else { 'fail' }
    branch = $branch
    head = $head
    main = $main
    currentChanged = $allPaths.Count
    frozenChecked = @($baselineDocument.frozenPaths).Count
    failures = @($failures)
  }
  if ($failures.Count -gt 0) {
    Write-Output ($result | ConvertTo-Json -Depth 8)
    throw "UI migration scope guard failed with $($failures.Count) violation(s)."
  }
  Write-Output ($result | ConvertTo-Json -Depth 8)
}

[IO.Directory]::CreateDirectory((Split-Path -Parent $evidencePath)) | Out-Null
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
if ($result.status -eq 'fail') { exit 1 }
exit 0
