$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=Join-Path $root 'out/build/x64-release/veyra_qml_ui.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'Build Veyra first using scripts/build.ps1 -Preset x64-release'}
Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Normal
