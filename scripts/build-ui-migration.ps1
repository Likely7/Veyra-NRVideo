# Wrapper that sets the non-ASCII output paths and runs build-ui-migration.cmd.
# Build output goes to the log file through cmd's own redirection, so compiler
# warnings on stderr never become PowerShell errors.
param([string[]]$Targets = @(), [string]$Log = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\build.log')
$env:VEYRA_OUT = 'E:\项目\Veyra\build\ui-qml-migration-20260925'
$env:VEYRA_TMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'
$env:VEYRA_LOG = $Log
$cmd = Join-Path $PSScriptRoot 'build-ui-migration.cmd'
& cmd.exe /c "`"$cmd`" $($Targets -join ' ') > `"%VEYRA_LOG%`" 2>&1"
$code = $LASTEXITCODE
Get-Content -LiteralPath $Log -Tail 25 -Encoding utf8
"exit=$code"
exit $code
