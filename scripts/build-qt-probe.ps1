# Wrapper for scripts/build-qt-probe.cmd: sets the non-ASCII paths (a .cmd is
# parsed in the ANSI code page) and runs the build through cmd's redirection.
param([string[]]$Targets = @(), [string]$Log = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qt-build.log')
$env:VEYRA_QT_OUT = 'E:\项目\Veyra\build\qt-probe-20260925'
$env:VEYRA_QT_TMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'
$env:VEYRA_QT_DIR = 'E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64'
$cmd = Join-Path $PSScriptRoot 'build-qt-probe.cmd'
& cmd.exe /c "`"$cmd`" $($Targets -join ' ') > `"$Log`" 2>&1"
$code = $LASTEXITCODE
Get-Content -LiteralPath $Log -Tail 30 -Encoding utf8
"exit=$code"
exit $code
