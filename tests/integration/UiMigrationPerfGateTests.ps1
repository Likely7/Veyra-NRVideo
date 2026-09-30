# Tests the gate, not GPU performance. Preserve each run's fixtures and output.
# UTF-8 BOM preserves the artifact-root literal in Windows PowerShell 5.1.
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetFullPath($OutputDirectory)
$artifactRoot = [IO.Path]::GetFullPath('E:/项目/Veyra/')
if (-not $out.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be inside E:/项目/Veyra/'
}
if (Test-Path -LiteralPath $out) { throw 'OutputDirectory already exists; preserve previous evidence' }
New-Item -ItemType Directory -Path $out | Out-Null
$gate = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../scripts/compare-ui-migration-perf.ps1'))
$cases = 'nr-1080','nr-fg-1080','nr-fg-4k','plain-1080'
function New-Result {
    $result = [ordered]@{}
    foreach ($case in $cases) {
        $result[$case] = [pscustomobject]@{ exit=0; samples=6; realPresented=300; graphSubmitP95Ms=1.0; gpuReadyP95Ms=8.0 }
    }
    return [pscustomobject]$result
}
$specs = @(
    @{name='equal'; expected=0},
    @{name='exact-boundaries'; expected=0},
    @{name='submit-over-limit'; expected=1},
    @{name='ready-over-limit'; expected=1},
    @{name='missing-case'; expected=1},
    @{name='missing-metric'; expected=1},
    @{name='no-samples'; expected=1},
    @{name='no-frames'; expected=1},
    @{name='failed-process'; expected=1},
    @{name='timeout'; expected=1},
    @{name='null-metric'; expected=1},
    @{name='string-metric'; expected=1},
    @{name='boolean-metric'; expected=1},
    @{name='zero-metric'; expected=1},
    @{name='negative-metric'; expected=1},
    @{name='invalid-baseline'; expected=1},
    @{name='malformed-results'; expected=2},
    @{name='malformed-baseline'; expected=2},
    @{name='protect-baseline'; expected=2},
    @{name='protect-results'; expected=2}
)
$summary = @()
foreach ($spec in $specs) {
    $dir = Join-Path $out $spec.name
    New-Item -ItemType Directory -Path $dir | Out-Null
    $current = New-Result; $baseline = New-Result
    $row = $current.'nr-1080'
    switch ($spec.name) {
        'exact-boundaries' { foreach ($case in $cases) { $current.$case.graphSubmitP95Ms=1.1; $current.$case.gpuReadyP95Ms=8.4 } }
        'submit-over-limit' { $row.graphSubmitP95Ms=1.10001 }
        'ready-over-limit' { $row.gpuReadyP95Ms=8.40001 }
        'missing-case' { $current.PSObject.Properties.Remove('nr-1080') }
        'missing-metric' { $row.PSObject.Properties.Remove('graphSubmitP95Ms') }
        'no-samples' { $row.samples=0 }
        'no-frames' { $row.realPresented=0 }
        'failed-process' { $row.exit=1 }
        'timeout' { $current.'nr-1080'='timeout' }
        'null-metric' { $row.graphSubmitP95Ms=$null }
        'string-metric' { $row.graphSubmitP95Ms='1.0' }
        'boolean-metric' { $row.graphSubmitP95Ms=$true }
        'zero-metric' { $row.graphSubmitP95Ms=0 }
        'negative-metric' { $row.graphSubmitP95Ms=-1 }
        'invalid-baseline' { $baseline.'nr-1080'.realPresented=0 }
    }
    $resultsPath=Join-Path $dir 'results.json'; $baselinePath=Join-Path $dir 'baseline.json'
    $reportPath=Join-Path $dir 'gate.json'
    $current | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultsPath -Encoding utf8
    $baseline | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $baselinePath -Encoding utf8
    switch ($spec.name) {
        'malformed-results' { '{' | Set-Content -LiteralPath $resultsPath -Encoding utf8 }
        'malformed-baseline' { '{' | Set-Content -LiteralPath $baselinePath -Encoding utf8 }
        'protect-baseline' { $reportPath=$baselinePath }
        'protect-results' { $reportPath=$resultsPath }
    }
    $beforeResults=(Get-FileHash -LiteralPath $resultsPath -Algorithm SHA256).Hash
    $beforeBaseline=(Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash
    # Separate host: the gate's exit must not terminate this test driver.
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$gate+'"'),
        '-Results',('"'+$resultsPath+'"'),'-Baseline',('"'+$baselinePath+'"'),'-Report',('"'+$reportPath+'"'))
    $launch=@{ FilePath='powershell.exe'; ArgumentList=$arguments; PassThru=$true; WindowStyle='Hidden';
        RedirectStandardOutput=(Join-Path $dir 'stdout.log'); RedirectStandardError=(Join-Path $dir 'stderr.log') }
    $p=Start-Process @launch; $null=$p.Handle
    if (-not $p.WaitForExit(15000)) { $p.Kill(); $p.WaitForExit(); $exitCode=-1 }
    else { $p.WaitForExit(); $exitCode=$p.ExitCode }
    $unchanged=$beforeResults -eq (Get-FileHash -LiteralPath $resultsPath -Algorithm SHA256).Hash -and
        $beforeBaseline -eq (Get-FileHash -LiteralPath $baselinePath -Algorithm SHA256).Hash
    $reportOk=$true
    if ($spec.expected -ne 2) {
        $reportOk=Test-Path -LiteralPath $reportPath
        if ($reportOk) {
            $report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
            $reportOk=$report.passed -eq ($spec.expected -eq 0) -and @($report.cases).Count -eq 4
        }
    } elseif ($spec.name -notlike 'protect-*') { $reportOk=-not (Test-Path -LiteralPath $reportPath) }
    $passed=$exitCode -eq $spec.expected -and $unchanged -and $reportOk
    $summary += [pscustomobject]@{ name=$spec.name; passed=$passed; expected=$spec.expected; actual=$exitCode; inputHashesUnchanged=$unchanged; reportValid=$reportOk }
    Write-Output (('{0} {1} exit={2}' -f $(if($passed){'PASS'}else{'FAIL'}),$spec.name,$exitCode))
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'summary.json') -Encoding utf8
if (@($summary | Where-Object { -not $_.passed }).Count) { exit 1 }
exit 0
