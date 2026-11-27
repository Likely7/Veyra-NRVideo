param(
    [string]$PreviewRoot = (Join-Path $env:LOCALAPPDATA 'VeyraDev\ui-preview')
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$appRoot = Join-Path $PreviewRoot 'app'
$exe = Join-Path $appRoot 'veyra_qml_ui.exe'
$running = Get-Process -Name veyra_qml_ui -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $exe }
if ($running) {
    Write-Host 'The UI preview is already running. Close it and rerun to load source changes.'
    exit 0
}
if (-not (Test-Path -LiteralPath $exe)) {
    $installation = @('Veyra (NVIDIA)', 'Veyra (AMD)') |
        ForEach-Object { Join-Path $env:LOCALAPPDATA "Programs\$_" } |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'current-version.txt') } |
        Select-Object -First 1
    if (-not $installation) { throw 'Install the official Veyra portable runtime or installer before running this preview.' }
    $version = (Get-Content -LiteralPath (Join-Path $installation 'current-version.txt') -Raw).Trim()
    if ($version -ne '2.2.1') { throw "This checkout was verified against runtime 2.2.1; found $version." }
    $donor = Join-Path $installation "versions\$version"
    if (-not (Test-Path -LiteralPath (Join-Path $donor 'veyra_qml_ui.exe'))) { throw 'Installed runtime is incomplete.' }
    New-Item -ItemType Directory -Force -Path $appRoot | Out-Null
    & robocopy $donor $appRoot /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "Runtime copy failed: $LASTEXITCODE" }
}
# Real copies: never modify the installed application's QML or user settings.
Copy-Item -Path (Join-Path $sourceRoot 'qml\Veyra\*') -Destination (Join-Path $appRoot 'qml\Veyra') -Recurse -Force
$dataRoot = Join-Path $PreviewRoot 'data'
$cacheRoot = Join-Path $PreviewRoot 'cache'
New-Item -ItemType Directory -Force -Path $dataRoot,$cacheRoot | Out-Null
$previousCacheRoot = $env:VEYRA_TEST_QML_CACHE_ROOT
try {
    $env:VEYRA_TEST_QML_CACHE_ROOT = $cacheRoot
    Start-Process -FilePath $exe -ArgumentList @('--data-dir', ('"' + $dataRoot + '"'), '--page', 'home', '--size', '1280x800') -WorkingDirectory $appRoot | Out-Null
} finally {
    $env:VEYRA_TEST_QML_CACHE_ROOT = $previousCacheRoot
}
