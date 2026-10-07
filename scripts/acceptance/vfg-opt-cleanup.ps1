$ErrorActionPreference = 'Stop'
$base = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if ([IO.Path]::GetPathRoot($base) -ne 'E:\' -or (Split-Path $base -Leaf) -ne 'Veyra') { throw 'Unexpected artifact root' }
$root = (Resolve-Path -LiteralPath (Join-Path $base 'tmp/vfg-optimization-20261007')).Path
$prefix = $root.TrimEnd('\') + '\'
if (Get-Process -Name veyra,veyra_qml_ui,veyra_vfg_gpu_tests,veyra_vfg_export_probe -ErrorAction SilentlyContinue) { throw 'Test process still running' }
$receipt = Join-Path $base 'logs/vfg-optimization-20261007/cleanup.json'
if (Test-Path -LiteralPath $receipt) { throw 'Existing cleanup receipt' }
$targets = @()
foreach ($case in Get-ChildItem -LiteralPath $root -Directory) {
    $path = Join-Path $case.FullName 'cuda-cache'
    if (!(Test-Path -LiteralPath $path)) { continue }
    $resolved = (Resolve-Path -LiteralPath $path).Path
    if (!$resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -ne 'cuda-cache') { throw 'Cache escaped task directory' }
    $entries = @((Get-Item -LiteralPath $resolved)) + @(Get-ChildItem -LiteralPath $resolved -Recurse -Force)
    if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Reparse point in cache' }
    $files = @($entries | Where-Object { !$_.PSIsContainer })
    $targets += [pscustomobject]@{ path=$resolved; files=$files.Count; bytes=[long](($files | Measure-Object Length -Sum).Sum) }
}
# Every resolved target has been checked before the first recursive deletion.
foreach ($target in $targets) { Remove-Item -LiteralPath $target.path -Recurse -Force }
[pscustomobject]@{ task='vfg-optimization-20261007'; removed=$targets; bytes=[long](($targets | Measure-Object bytes -Sum).Sum); retained='Build, candidate, sources, test fixtures, profiles, outputs, receipts and logs' } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receipt -Encoding UTF8
Write-Output ('Removed task-only CUDA caches: ' + $targets.Count + ' directories')
