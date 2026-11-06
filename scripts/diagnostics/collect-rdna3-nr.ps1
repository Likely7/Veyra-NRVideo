param(
    [string]$AppRoot = $PSScriptRoot,
    [string]$OutputRoot = '',
    [switch]$SkipWindowsEvents
)
$ErrorActionPreference = 'Stop'
# A PowerShell 7 parent can pass its own module search path to Windows
# PowerShell. Prefer this process's built-in modules without changing the
# user/system environment or requiring an installed third-party module.
$nativeModules = Join-Path $PSHOME 'Modules'
if (-not (($env:PSModulePath -split ';') -contains $nativeModules)) { $env:PSModulePath = $nativeModules + ';' + $env:PSModulePath }
$AppRoot = (Get-Item -LiteralPath $AppRoot).FullName
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $AppRoot 'logs' }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0,8))
$stage = Join-Path $OutputRoot ('Veyra-NR-Diagnostics-' + $stamp)
New-Item -ItemType Directory -Path $stage | Out-Null
$notes = [Collections.Generic.List[string]]::new()
function Get-ComponentSHA256([string]$Path) {
    $stream = $null; $digest = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        $digest = [Security.Cryptography.SHA256]::Create()
        return [BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-', '')
    } finally { if ($digest) { $digest.Dispose() }; if ($stream) { $stream.Dispose() } }
}
foreach ($spec in @(@('runtime/amd-nr-rdna3/cache','worker-logs'), @('logs','app-logs'))) {
    $from = Join-Path $AppRoot $spec[0]
    $to = Join-Path $stage $spec[1]
    if (-not (Test-Path -LiteralPath $from -PathType Container)) { $notes.Add('Missing log directory: ' + $from); continue }
    New-Item -ItemType Directory -Path $to | Out-Null
    foreach ($file in (Get-ChildItem -LiteralPath $from -File -Filter '*.log')) {
        # Read shared logs without requiring the user to stop/restart again.
        $inputStream = $null; $outputStream = $null
        try {
            $inputStream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            $outputStream = [IO.File]::Create((Join-Path $to $file.Name))
            $inputStream.CopyTo($outputStream)
        } catch { $notes.Add('Could not copy ' + $file.Name + ': ' + $_.Exception.Message) }
        finally { if ($outputStream) { $outputStream.Dispose() }; if ($inputStream) { $inputStream.Dispose() } }
    }
}
$components = foreach ($relative in @(
    'runtime/amd-nr-rdna3/veyra_amd_nr_worker.exe',
    'runtime/amd-nr-rdna3/zluda/zluda_nvapi.dll',
    'runtime/amd-nr-rdna3/zluda/nvapi64.dll',
    'runtime/amd-nr-rdna3/zluda/nvcuda.dll',
    'runtime/amd-nr-rdna3/zluda/zluda_real.dll',
    'runtime/amd-nr-rdna3/zluda/nvngx.dll',
    'runtime/amd-nr-rdna3/nvngx_dlssnr.dll'
)) {
    $path = Join-Path $AppRoot $relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $file = Get-Item -LiteralPath $path
        [pscustomobject]@{ Path=$relative; Bytes=$file.Length; SHA256=(Get-ComponentSHA256 $path); Version=$file.VersionInfo.FileVersion }
    } else { [pscustomobject]@{ Path=$relative; Missing=$true } }
}
ConvertTo-Json -InputObject @($components) -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'components.json') -Encoding UTF8
$eventResult = @{ Skipped=[bool]$SkipWindowsEvents; Events=@(); Error=$null; ErrorId=$null }
if (-not $SkipWindowsEvents) {
    try {
        $eventResult.Events = @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000,1001;StartTime=(Get-Date).AddHours(-3)} -ErrorAction Stop |
            Where-Object { $_.ToXml().IndexOf('veyra_amd_nr_worker.exe', [StringComparison]::OrdinalIgnoreCase) -ge 0 } |
            Select-Object -First 20 TimeCreated,Id,ProviderName,Message,@{Name='Xml';Expression={$_.ToXml()}})
    } catch { $eventResult.Error=$_.Exception.Message; $eventResult.ErrorId=$_.FullyQualifiedErrorId }
}
ConvertTo-Json -InputObject $eventResult -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'worker-windows-events.json') -Encoding UTF8
ConvertTo-Json -InputObject @{CollectedUtc=[DateTime]::UtcNow.ToString('o');AppRoot=$AppRoot;Notes=@($notes);DatabaseIncluded=$false} -Depth 5 |
    Set-Content -LiteralPath (Join-Path $stage 'collection.json') -Encoding UTF8
$archive = $stage + '.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
Write-Host ('Diagnostics saved: ' + $archive)
