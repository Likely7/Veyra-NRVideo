$ErrorActionPreference = 'Stop'
$repairTestsRoot = [IO.Path]::GetFullPath('E:\项目\Veyra\tests\obs-export-amdnr-20261006')
$repairVerifyRoot = [IO.Path]::GetFullPath('E:\项目\Veyra\verify\obs-export-amdnr-20261006')
$repairLogsRoot = [IO.Path]::GetFullPath('E:\项目\Veyra\logs\obs-export-amdnr-20261006')
$repairReceipt = Join-Path $repairLogsRoot 'cleanup.json'
if (Test-Path -LiteralPath $repairReceipt) { throw 'Do not overwrite cleanup evidence' }
foreach ($repairProcess in Get-Process) {
    try { $repairExe = $repairProcess.Path } catch { continue }
    if ($repairExe -and ($repairExe.StartsWith($repairTestsRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or $repairExe.StartsWith($repairVerifyRoot + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw ('Owned test process is still running: ' + $repairProcess.Id)
    }
}
function Confirm-RepairCopy([string]$targetPath, [string]$expectedRoot) {
    $resolvedPath = (Resolve-Path -LiteralPath $targetPath).Path
    if (-not $resolvedPath.StartsWith($expectedRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw ('Out of task root: ' + $resolvedPath) }
    $targetItem = Get-Item -LiteralPath $resolvedPath -Force
    if ($targetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse target: ' + $resolvedPath) }
    foreach ($copyItem in Get-ChildItem -LiteralPath $resolvedPath -Force -Recurse) {
        if ($copyItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse child: ' + $copyItem.FullName) }
    }
    return $resolvedPath
}
$repairUiLogs = Join-Path $repairVerifyRoot 'ui-app\logs'
if (Test-Path -LiteralPath $repairUiLogs) {
    $checkedUiLogs = Confirm-RepairCopy $repairUiLogs $repairVerifyRoot
    $savedUiLogs = Join-Path $repairLogsRoot 'gui-worker-logs'
    if (Test-Path -LiteralPath $savedUiLogs) { throw 'Worker evidence destination already exists' }
    Move-Item -LiteralPath $checkedUiLogs -Destination $savedUiLogs
}
$repairTargets = [Collections.Generic.List[string]]::new()
foreach ($copyName in @('ui-app','AMD','NVIDIA')) {
    $copyTarget = Join-Path $repairVerifyRoot $copyName
    if (Test-Path -LiteralPath $copyTarget) { $repairTargets.Add((Confirm-RepairCopy $copyTarget $repairVerifyRoot)) }
}
foreach ($caseName in @('field-stack-after-v1','field-stack-after-v2','field-single-final-v1','lifecycle-final-v1','lifecycle-boundary-final-v1')) {
    foreach ($componentName in @('runtime','runtime_local')) {
        $copyTarget = Join-Path (Join-Path $repairTestsRoot $caseName) $componentName
        if (Test-Path -LiteralPath $copyTarget) { $repairTargets.Add((Confirm-RepairCopy $copyTarget $repairTestsRoot)) }
    }
}
$repairRecords = @()
foreach ($checkedTarget in $repairTargets) {
    $copyFiles = @(Get-ChildItem -LiteralPath $checkedTarget -File -Recurse -Force)
    $copyBytes = ($copyFiles | Measure-Object -Property Length -Sum).Sum
    $repairRecords += [pscustomobject]@{ path=$checkedTarget; files=$copyFiles.Count; bytes=$copyBytes }
    Remove-Item -LiteralPath $checkedTarget -Recurse -Force
    if (Test-Path -LiteralPath $checkedTarget) { throw ('Copy cleanup incomplete: ' + $checkedTarget) }
}
[pscustomobject]@{ copies=$repairRecords; preserved='Final candidate directories/ZIPs, source/bundle, build, logs, screenshots, test outputs, failed receipts and identity fixtures'; bytesRemoved=($repairRecords | Measure-Object -Property bytes -Sum).Sum } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $repairReceipt -Encoding UTF8
Write-Output ('CLEANUP PASS ' + $repairTargets.Count + ' owned redundant copies')
