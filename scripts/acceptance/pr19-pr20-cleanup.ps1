$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
# ASCII script text also works in Windows PowerShell 5.1 (UTF-8 without BOM
# would decode a Chinese path literal using the active ANSI code page).
if ($taskRoot.TrimEnd('\') -ne ('E:\' + [char]0x9879 + [char]0x76EE + '\Veyra')) { throw 'Unexpected task artifact root' }
$taskName = 'pr19-pr20-20261006'
$testRoot = [IO.Path]::GetFullPath((Join-Path $taskRoot "tests\$taskName"))
$logRoot = Join-Path $taskRoot "logs\$taskName"
$approvedApp = Join-Path $taskRoot 'test-packages\obs-export-amdnr-20261006\Veyra-2.0.4-fix1-NVIDIA-win64-portable'
$pythonGuard = Join-Path $PSScriptRoot 'pr19-pr20-control.py'
python -B $pythonGuard --published
if ($LASTEXITCODE -ne 0) { throw 'Opening inventory protection failed' }

function Confirm-OwnedPath([string] $candidate) {
    $absolute = [IO.Path]::GetFullPath($candidate)
    if (-not $absolute.StartsWith($testRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Outside owned test task: $absolute" }
    if ((Get-Item -LiteralPath $testRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Test task is a reparse point' }
    $cursor = $absolute
    while ($cursor.Length -gt $testRoot.Length) {
        if ((Get-Item -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse path: $cursor" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $absolute
}
# Keep all source/bundles, current builds, exports, profiles, screenshots,
# diagnostic EXEs and fake-provider fixtures. Delete only verified duplicate
# root DLLs and full runtime copies inside this task's test cases.
$ownedRunning = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -and $_.ExecutablePath.StartsWith($testRoot + '\', [StringComparison]::OrdinalIgnoreCase)
})
if ($ownedRunning.Count) { throw 'An own test process remains active; no cleanup' }
$targets = [Collections.Generic.List[object]]::new()
foreach ($folder in Get-ChildItem -LiteralPath $testRoot -Directory) {
    foreach ($file in Get-ChildItem -LiteralPath $folder.FullName -File -Filter '*.dll') {
        $absolute = Confirm-OwnedPath $file.FullName
        $approved = Join-Path $approvedApp $file.Name
        if (-not (Test-Path -LiteralPath $approved -PathType Leaf)) { throw "Unknown DLL duplicate: $absolute" }
        $actualHash = (Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash
        if ($actualHash -ne (Get-FileHash -LiteralPath $approved -Algorithm SHA256).Hash) { throw "DLL duplicate differs: $absolute" }
        $targets.Add([pscustomobject]@{ path=$absolute; recursive=$false; bytes=$file.Length; sha256=$actualHash })
    }
    foreach ($runtimeName in @('runtime','runtime_local')) {
        $runtimePath = Join-Path $folder.FullName $runtimeName
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Container)) { continue }
        # The AMD identity tests have deliberately different, small fixtures.
        if ($folder.Name -like 'graph-*') { continue }
        $absolute = Confirm-OwnedPath $runtimePath
        $allItems = @(Get-ChildItem -LiteralPath $absolute -Force -Recurse)
        if (@($allItems | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw "Runtime copy contains reparse points: $absolute" }
        $approvedRoot = Join-Path $approvedApp $runtimeName
        $files = @($allItems | Where-Object { -not $_.PSIsContainer })
        $approvedFiles = @(Get-ChildItem -LiteralPath $approvedRoot -Force -Recurse -File)
        if ($files.Count -ne $approvedFiles.Count) { throw "Runtime copy count differs: $absolute" }
        [long] $bytes = 0
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($absolute.Length + 1)
            $approved = Join-Path $approvedRoot $relative
            if (-not (Test-Path -LiteralPath $approved -PathType Leaf)) { throw "Unknown runtime duplicate: $relative" }
            if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $approved -Algorithm SHA256).Hash) { throw "Runtime duplicate differs: $relative" }
            $bytes += $file.Length
        }
        $targets.Add([pscustomobject]@{ path=$absolute; recursive=$true; bytes=$bytes; files=$files.Count })
    }
}
$receipt = [ordered]@{ task=$taskName; checked=@($targets.ToArray()); removed=@(); deletedBytes=0; completed=$false }
$receiptPath = Join-Path $logRoot 'cleanup.json'
if (Test-Path -LiteralPath $receiptPath) { throw 'Cleanup receipt already exists' }
try {
    foreach ($target in $targets) {
        $absolute = Confirm-OwnedPath $target.path
        if ($target.recursive) { Remove-Item -LiteralPath $absolute -Force -Recurse }
        else { Remove-Item -LiteralPath $absolute -Force }
        $receipt.removed += $absolute
        $receipt.deletedBytes += $target.bytes
    }
    $receipt.completed = $true
} finally {
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
}
python -B $pythonGuard --published
if ($LASTEXITCODE -ne 0) { throw 'Closing inventory protection failed' }
Write-Output ("OWN TEST DUPLICATES CLEANED: {0} targets; {1} bytes" -f $receipt.removed.Count,$receipt.deletedBytes)
