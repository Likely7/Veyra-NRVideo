# Wrapper for scripts/build-qt-probe.cmd: sets the non-ASCII paths (a .cmd is
# parsed in the ANSI code page) and runs the build through cmd's redirection.
param([string[]]$Targets = @(), [string]$Log = 'E:\项目\Veyra\logs\ui-qml-migration-20260925\qt-build.log',
      [string]$Out = 'E:\项目\Veyra\build\qt-probe-20260926')
$env:VEYRA_QT_OUT = $Out
$env:VEYRA_QT_TMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'
$env:VEYRA_QT_DIR = 'E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64'
$cmd = Join-Path $PSScriptRoot 'build-qt-probe.cmd'
# CMake decodes cl /showIncludes in the console code page. Under 936 the include-probe
# prefix comes back garbled and the Ninja generator silently stops writing
# CMakeFiles/rules.ninja (WORKLOG 0.0.4 build failure; build-isolated.ps1 does the same).
# The code page is switched out here, not inside the .cmd: there cmd re-reads the
# non-ASCII output path as UTF-8 and the build lands in a mojibake directory.
$savedCodePage = (& cmd.exe /d /c chcp) -replace '[^0-9]', ''
& cmd.exe /d /c 'chcp 65001 >nul'
try { & cmd.exe /c "`"$cmd`" $($Targets -join ' ') > `"$Log`" 2>&1"; $code = $LASTEXITCODE }
finally { & cmd.exe /d /c "chcp $savedCodePage >nul" }
Get-Content -LiteralPath $Log -Tail 30 -Encoding utf8
"exit=$code"
exit $code
