# Runs veyra_qml_quick_tests (G1.6) with the Qt runtime on PATH for this process only.
param([string]$Build = 'E:\项目\Veyra\build\qt-probe-20260926',
      [string]$Qt = 'E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64',
      [string]$Out = '')
$env:PATH = "$Qt\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$Qt\plugins"
$env:TEMP = 'E:\项目\Veyra\tmp\ui-qml-migration-20260925'; $env:TMP = $env:TEMP
# QtTest's console output does not survive the pipe here; results go through -o.
$res = Join-Path $env:TEMP 'quick-tests.txt'
& (Join-Path $Build 'veyra_qml_quick_tests.exe') -o "$res,txt" | Out-Null
$code = $LASTEXITCODE
$text = Get-Content -LiteralPath $res -Raw -Encoding utf8
$text.TrimEnd()
if ($Out) { $text | Set-Content -LiteralPath $Out -Encoding utf8 }
"exit=$code"
exit $code
