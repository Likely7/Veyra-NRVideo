# Stage the current branch build into a runnable app directory for tests.
# Runtime components come from the verified 1.4.4 release (junctions, never copied
# or modified); settings under runtime_local are fresh test-only files, so running
# tests never touches the user's real configuration.
param([string]$Build = 'E:\项目\Veyra\build\ui-qml-migration-20260925',
      [string]$App = 'E:\项目\Veyra\tests\ui-qml-migration-20260925\app',
      [string]$Release = 'E:\项目\Veyra\releases\1.4.4\final\Veyra-1.4.4-win64-portable')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $App | Out-Null
foreach ($name in 'runtime') {
    $link = Join-Path $App $name
    if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target (Join-Path $Release $name) | Out-Null }
}
$local = Join-Path $App 'runtime_local'
New-Item -ItemType Directory -Force $local | Out-Null
foreach ($name in 'amd', 'intel') {
    $link = Join-Path $local $name
    if (-not (Test-Path -LiteralPath $link)) { New-Item -ItemType Junction -Path $link -Target (Join-Path (Join-Path $Release 'runtime_local') $name) | Out-Null }
}
# Application binaries, test executables, FFmpeg DLLs and shaders from the build.
Get-ChildItem -LiteralPath $Build -File | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $App -Force
}
foreach ($dll in 'avcodec-63.dll','avformat-63.dll','avutil-61.dll','dav1d.dll','swresample-7.dll','swscale-10.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') {
    $target = Join-Path $App $dll
    if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath (Join-Path $Release $dll) -Destination $target }
}
robocopy (Join-Path $Build 'shaders') (Join-Path $App 'shaders') /MIR /NJH /NJS /NP /NFL /NDL | Out-Null
"staged $App"
