# Read-only comparison against the frozen UI migration performance baseline.
# Do not overwrite/re-record the baseline or relax limits to make a run pass.
param([Parameter(Mandatory=$true)][string]$Results,
      [Parameter(Mandatory=$true)][string]$Baseline,
      [Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference = 'Stop'
$cases = 'nr-1080','nr-fg-1080','nr-fg-4k','plain-1080'
$limits = [ordered]@{ graphSubmitP95Ms = 10.0; gpuReadyP95Ms = 5.0 }
function IsNumber($value) {
    return ($null -ne $value -and $value -is [ValueType] -and $value -isnot [bool] -and
        -not [double]::IsNaN([double]$value) -and -not [double]::IsInfinity([double]$value))
}
try {
    $resultsPath = (Resolve-Path -LiteralPath $Results).Path
    $baselinePath = (Resolve-Path -LiteralPath $Baseline).Path
    $reportPath = [IO.Path]::GetFullPath($Report)
    if ($reportPath -eq $baselinePath -or $reportPath -eq $resultsPath) { throw 'Report must not overwrite input evidence' }
    $now = Get-Content -LiteralPath $resultsPath -Raw | ConvertFrom-Json
    $base = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
    $rows = @()
    foreach ($case in $cases) {
        $current = $now.$case; $previous = $base.$case
        $errors = @(); $metrics = @()
        foreach ($pair in @(@('current',$current),@('baseline',$previous))) {
            $label = $pair[0]; $row = $pair[1]
            if ($null -eq $row -or $row -is [string]) { $errors += "$label missing or failed case"; continue }
            if (-not (IsNumber $row.exit) -or $row.exit -ne 0) { $errors += "$label process did not exit successfully" }
            if (-not (IsNumber $row.samples) -or $row.samples -lt 1) { $errors += "$label has no timing samples" }
            if (-not (IsNumber $row.realPresented) -or $row.realPresented -lt 1) { $errors += "$label has no presented source frames" }
        }
        foreach ($field in $limits.Keys) {
            $a = $current.$field; $b = $previous.$field
            if (-not (IsNumber $a) -or -not (IsNumber $b) -or $a -le 0 -or $b -le 0) {
                $errors += "$field missing or invalid timing"; continue
            }
            $pct = 100.0 * ([double]$a / [double]$b - 1.0)
            $pass = [double]$a -le ([double]$b * (1.0 + $limits[$field] / 100.0) + 1e-12)
            $metrics += [pscustomobject]@{ field=$field; baseline=$b; current=$a; changePct=$pct; limitPct=$limits[$field]; passed=$pass }
        }
        $passed = $errors.Count -eq 0 -and @($metrics | Where-Object { -not $_.passed }).Count -eq 0
        $rows += [pscustomobject]@{ name=$case; passed=$passed; errors=$errors; metrics=$metrics }
        $status = if ($passed) { 'PASS' } else { 'FAIL' }
        Write-Output ("$status $case : " + (($metrics | ForEach-Object { '{0} {1:F2}% (limit {2:F0}%)' -f $_.field,$_.changePct,$_.limitPct }) -join ' | '))
        foreach ($message in $errors) { Write-Output "  $message" }
    }
    $passed = @($rows | Where-Object { -not $_.passed }).Count -eq 0
    [ordered]@{ passed=$passed; results=$resultsPath; baseline=$baselinePath; cases=$rows } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
    if (-not $passed) { exit 1 }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
}
exit 0
