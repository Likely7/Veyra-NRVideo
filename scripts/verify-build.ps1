<#
.SYNOPSIS
  构建后验证：单元测试 + 部署一致性 + 运行时日志零错误。

.DESCRIPTION
  融合 2026-11-10 升级 2.1.4 时暴露的三类问题，每次构建后跑一遍：
    1. 单元测试（预设库 encode/decode、效果链、修复合同）——抓版本号/自检类回归；
    2. 部署一致性（部署目录的 exe 必须等于刚构建的 exe）——防止只改源码没换程序；
    3. 运行时启动/退出，只看本次新增的日志行——抓只有跑起来才出现的错误
       （例如 2.1.4 的「模式会话编码校验失败」、QML 绑定已删属性）。

  任何一项失败，退出码为 1，并打印失败项。

.PARAMETER BuildDir
  构建目录，默认 D:\Program\Veyra\build。

.PARAMETER DeployDir
  已部署的运行目录（含 runtime_local 与 logs）。默认 2.1.4 部署目录。

.PARAMETER SkipBuild
  跳过构建，只跑测试与运行时检查。

.PARAMETER RunSeconds
  运行时检查的启动时长，默认 8 秒（--exit-after）。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/verify-build.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts/verify-build.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [string]$BuildDir  = 'D:\Program\Veyra\build',
    [string]$DeployDir = 'D:\Program\Veyra-2.1.5-NVIDIA-win64-portable',
    [switch]$SkipBuild,
    [int]$RunSeconds = 8
)

$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]

function Fail([string]$msg) {
    $failures.Add($msg) | Out-Null
    Write-Host "FAIL  $msg" -ForegroundColor Red
}
function Ok([string]$msg) {
    Write-Host "OK    $msg" -ForegroundColor Green
}
function Info([string]$msg) {
    Write-Host "      $msg" -ForegroundColor DarkGray
}

Write-Host '=== 1/4 构建 ===' -ForegroundColor Cyan
if ($SkipBuild) {
    Info '跳过（-SkipBuild）'
} else {
    $vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { Fail "找不到 vcvars64.bat: $vcvars" }
    $targets = 'veyra_qml_ui veyra_preset_library_tests veyra_effect_chain_tests veyra_repair_contract_tests veyra_live_timing_tests'
    $cmd = "call `"$vcvars`" >nul && cmake --build `"$BuildDir`" --target $targets -j 8"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { Fail "构建失败（exit $LASTEXITCODE）" } else { Ok '构建通过' }
}

Write-Host ''
Write-Host '=== 2/4 单元测试 ===' -ForegroundColor Cyan

# 部分测试动态链接 FFmpeg（veyra_engine 的 AV 库），DLL 不在 build 目录：
# 从 CMakeCache 找到 FFmpeg 根，把 bin 注入 PATH，否则测试以 0xC0000135 退出。
$ffmpegBin = $null
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (Test-Path $cache) {
    $m = Select-String -Path $cache -Pattern '^VEYRA_FFMPEG_ROOT:PATH=(.+)$' | Select-Object -First 1
    if ($m) {
        $root = $m.Matches[0].Groups[1].Value.Trim()
        $candidate = Join-Path $root 'bin'
        if (Test-Path $candidate) { $ffmpegBin = $candidate }
    }
}
if ($ffmpegBin) {
    $env:PATH = "$ffmpegBin;$env:PATH"
    Info "FFmpeg DLL: $ffmpegBin"
} else {
    Info '未找到 FFmpeg bin，若测试报 0xC0000135 请检查 CMakeCache 的 VEYRA_FFMPEG_ROOT'
}
$tests = @('veyra_preset_library_tests', 'veyra_effect_chain_tests', 'veyra_repair_contract_tests', 'veyra_live_timing_tests')
foreach ($t in $tests) {
    $exe = Join-Path $BuildDir "$t.exe"
    if (-not (Test-Path $exe)) { Fail "$t.exe 不存在"; continue }
    # Run from the build directory: the test binaries load runtime DLLs relative to cwd.
    Push-Location $BuildDir
    try { $out = & $exe 2>&1 | Out-String; $rc = $LASTEXITCODE }
    finally { Pop-Location }
    $pass = ([regex]::Matches($out, 'PASS')).Count
    $fail = ([regex]::Matches($out, 'FAIL')).Count
    if ($rc -ne 0) {
        Fail "$t 退出码 $rc"
        $out -split "`n" | Select-String -Pattern 'FAIL' | Select-Object -First 5 | ForEach-Object { Info $_.Line.Trim() }
    } elseif ($fail -gt 0) {
        Fail "$t 有 $fail 个 FAIL（PASS $pass）"
    } else {
        Ok "$t 通过（PASS $pass）"
    }
}

Write-Host ''
Write-Host '=== 3/4 部署一致性 ===' -ForegroundColor Cyan
$built = Join-Path $BuildDir 'veyra_qml_ui.exe'
$deployed = Join-Path $DeployDir 'veyra_qml_ui.exe'
$runtimeOk = $false
if (-not (Test-Path $built)) {
    Fail "构建产物不存在: $built"
} elseif (-not (Test-Path $deployed)) {
    Fail "部署目录不存在或没有 exe: $deployed"
} else {
    $h1 = (Get-FileHash $built -Algorithm SHA256).Hash.ToLower()
    $h2 = (Get-FileHash $deployed -Algorithm SHA256).Hash.ToLower()
    if ($h1 -ne $h2) {
        Fail "部署 exe 与构建不一致（部署 $($h2.Substring(0,16)) / 构建 $($h1.Substring(0,16))），先部署再验证"
    } else {
        Ok "部署 exe 一致（$($h1.Substring(0,16))…）"
        $runtimeOk = $true
    }
}

Write-Host ''
Write-Host '=== 4/4 运行时日志 ===' -ForegroundColor Cyan
if (-not $runtimeOk) {
    Info '跳过（部署不一致或缺失）'
} else {
    $logsDir = Join-Path $DeployDir 'logs'
    New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
    $log = Join-Path $logsDir 'veyra-qml.log'
    $before = 0
    if (Test-Path $log) { $before = (Get-Item $log).Length }
    $dataDir = Join-Path $DeployDir 'runtime_local\user-data-2.0.0-verify'
    $proc = Start-Process -FilePath $deployed `
        -ArgumentList @('--data-dir', $dataDir, '--exit-after', "$RunSeconds") `
        -WorkingDirectory $DeployDir -PassThru -Wait
    if ($proc.ExitCode -ne 0) {
        Fail "运行时启动退出码 $($proc.ExitCode)"
    } else {
        Ok "运行时启动/退出正常（--exit-after $RunSeconds）"
    }
    Start-Sleep -Milliseconds 500
    $text = ''
    if (Test-Path $log) {
        $fs = [System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        try {
            if ($fs.Length -gt $before) {
                $fs.Position = $before
                $sr = New-Object System.IO.StreamReader($fs)
                $text = $sr.ReadToEnd()
                $sr.Dispose()
            }
        } finally { $fs.Dispose() }
    }
    $errors = @($text -split "`n" | Where-Object { $_ -match '\[ERROR\]' })
    $warns  = @($text -split "`n" | Where-Object { $_ -match '\[WARN \]' })
    if ($errors.Count -gt 0) {
        Fail "本次运行有 $($errors.Count) 条 ERROR"
        $errors | Select-Object -First 5 | ForEach-Object { Info $_.Trim() }
    } else {
        Ok '本次运行 0 条 ERROR'
    }
    if ($warns.Count -gt 0) {
        Info "另有 $($warns.Count) 条 WARN（不判失败，人工看一眼）"
        $warns | Select-Object -First 5 | ForEach-Object { Info $_.Trim() }
    }
}

Write-Host ''
if ($failures.Count -eq 0) {
    Write-Host '=== 全部通过 ===' -ForegroundColor Green
    exit 0
} else {
    Write-Host "=== $($failures.Count) 项失败 ===" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
