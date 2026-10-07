# Read-only process/GPU evidence. Does not launch/stop a player, alter settings,
# inject code, create ETW sessions, or attribute retained memory to a module.
# OutputPath must be explicit, its parent must exist, and existing files are refused.
param(
    [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ProbeProcessId,
    [Parameter(Mandatory=$true)][string]$OwnedExe,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [ValidateRange(1,280)][int]$DurationSeconds = 280,
    [ValidateRange(1,30)][int]$SampleSeconds = 5
)
$ErrorActionPreference = 'Stop'
$expectedExe = [IO.Path]::GetFullPath($OwnedExe)
$target = Get-Process -Id $ProbeProcessId
if ($target.Path -ne $expectedExe) { throw 'Process identity mismatch; no capture started' }
$processStart = $target.StartTime.ToUniversalTime()
$outputFile = [IO.Path]::GetFullPath($OutputPath)
$file = [IO.File]::Open($outputFile, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$encoding = New-Object System.Text.UTF8Encoding($false)
$stream = New-Object System.IO.StreamWriter($file, $encoding)
$knownModules = @{}
try {
    $metadata = @{type='metadata'; utc=[DateTime]::UtcNow.ToString('o'); pid=$ProbeProcessId;
                  processStartUtc=$processStart.ToString('o'); executable=$expectedExe;
                  scriptSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;
                  durationSeconds=$DurationSeconds; sampleSeconds=$SampleSeconds;
                  scope='Read-only WDDM process counters, not resource ownership or physical scanout'}
    try {
        $metadata.videoControllers = @(Get-CimInstance -ClassName Win32_VideoController -OperationTimeoutSec 5 |
            Select-Object Name,PNPDeviceID,DriverVersion)
    } catch { $metadata.videoControllersUnavailable = $_.Exception.Message }
    $stream.WriteLine(($metadata | ConvertTo-Json -Depth 6 -Compress))
    $stream.Flush()
    $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $target = Get-Process -Id $ProbeProcessId -ErrorAction SilentlyContinue
        if (-not $target) { break }
        if ($target.Path -ne $expectedExe -or $target.StartTime.ToUniversalTime() -ne $processStart) {
            throw 'Process identity changed; capture stopped'
        }
        $record = @{type='sample'; utc=[DateTime]::UtcNow.ToString('o'); pid=$ProbeProcessId;
                    processStartUtc=$processStart.ToString('o')}
        try {
            $record.gpu = @(Get-CimInstance -ClassName Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -Filter "Name LIKE 'pid_${ProbeProcessId}_%'" -OperationTimeoutSec 5 |
                Where-Object { $_.Name.StartsWith("pid_${ProbeProcessId}_") } |
                Select-Object Name,DedicatedUsage,SharedUsage,TotalCommitted)
            $record.gpuAvailable = $record.gpu.Count -gt 0
        } catch { $record.gpuAvailable=$false; $record.gpuUnavailable=$_.Exception.Message }
        try {
            $record.modules = @($target.Modules | Where-Object { $_.ModuleName -in @('nvngx_dlssnr.dll','_nvngx.dll','nvofapi64.dll') } |
                Select-Object ModuleName,FileName)
            foreach ($module in $record.modules) {
                if (-not $knownModules.ContainsKey($module.FileName)) {
                    $identity = @{type='module-identity'; utc=[DateTime]::UtcNow.ToString('o'); pid=$ProbeProcessId;
                                  name=$module.ModuleName; path=$module.FileName}
                    try {
                        $item = Get-Item -LiteralPath $module.FileName
                        $identity.bytes = $item.Length
                        $identity.version = $item.VersionInfo.FileVersion
                        $identity.sha256 = (Get-FileHash -LiteralPath $module.FileName -Algorithm SHA256).Hash
                    } catch { $identity.identityUnavailable = $_.Exception.Message }
                    $stream.WriteLine(($identity | ConvertTo-Json -Depth 4 -Compress))
                    $knownModules[$module.FileName] = $true
                }
            }
        } catch { $record.modulesUnavailable = $_.Exception.Message }
        try {
            $target.Refresh()
            $record.privateBytes = $target.PrivateMemorySize64
            $record.workingSetBytes = $target.WorkingSet64
            $record.handles = $target.HandleCount
        } catch {
            $record.processUnavailable = $_.Exception.Message
            $stream.WriteLine(($record | ConvertTo-Json -Depth 6 -Compress))
            $stream.Flush()
            break
        }
        $stream.WriteLine(($record | ConvertTo-Json -Depth 6 -Compress))
        $stream.Flush()
        Start-Sleep -Seconds $SampleSeconds
    }
} finally { $stream.Dispose() }
