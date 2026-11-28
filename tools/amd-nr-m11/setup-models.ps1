# Veyra 2.2.1 AMD Mochi (DLSSNR-AMD 0.0.9m11) Model Setup Helper
# Extracts proprietary NVIDIA NR weights from user-provided nvngx_dlssnr.dll (or official zip),
# applies open-source XOR deltas & INT4 layer weights, and verifies all 5 SHA-256 checksums.
param(
    [string]$Dll = '',
    [string]$OutDir = '',
    [switch]$NoPause,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch { }

$ToolsDir = $PSScriptRoot
$DefaultOut = (Resolve-Path (Join-Path $ToolsDir '..\dlssnr-amd')).Path
if (-not $OutDir) {
    $OutDir = $DefaultOut
} else {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $OutDir = (Resolve-Path -LiteralPath $OutDir).Path
}

$VeyraRoot = $ToolsDir
for ($up = 0; $up -lt 8; $up++) {
    $cand = Split-Path -Parent $VeyraRoot
    if (-not $cand) { break }
    $VeyraRoot = $cand
    if (Test-Path -LiteralPath (Join-Path $VeyraRoot 'Veyra.exe') -PathType Leaf) { break }
}

$Int4Data = Join-Path $OutDir 'int4-ultra'
if (-not (Test-Path -LiteralPath $Int4Data -PathType Container)) {
    $Int4Data = Join-Path $DefaultOut 'int4-ultra'
}

function Say([string]$msg) { Write-Host $msg }
function Ok([string]$msg) { Write-Host $msg -ForegroundColor Green }
function Warn([string]$msg) { Write-Host $msg -ForegroundColor Yellow }
function Finish([int]$code) {
    if (-not $NoPause) {
        Write-Host ''
        Read-Host '按 Enter 键关闭 / Press Enter to close' | Out-Null
    }
    exit $code
}
function Fail([string]$msg) {
    Write-Host "[ERROR] $msg" -ForegroundColor Red
    Finish 1
}

function Read-ExpectedSha([string]$shaFile) {
    if (-not (Test-Path -LiteralPath $shaFile -PathType Leaf)) {
        Fail "缺少校验文件: $shaFile"
    }
    return ((Get-Content -LiteralPath $shaFile -Raw -Encoding UTF8).Trim() -split '\s+')[0].ToUpperInvariant()
}

function Test-Sha([string]$path, [string]$expectedUpper) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    $got = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    return ($got -eq $expectedUpper)
}

$ShaModel = '2B41C888CF4155B8958C665BA64018AB0BD25C85FC71A2B6DB86D0D04D1F7FBD'
$ShaSmall = '7E966E2639DBEC6596AA0BAC3B8761137D2C40CA8CB4ACA30DF7CA10043C6E33'
$ShaExp   = Read-ExpectedSha (Join-Path $ToolsDir 'CC_SMALL_FP8_EXPERIMENTAL.bin.sha256')
$ShaOpt   = Read-ExpectedSha (Join-Path $ToolsDir 'CC_SILVER_AARDWOLD_OPTIMIZED.bin.sha256')
$ShaInt4  = Read-ExpectedSha (Join-Path $ToolsDir 'dlssnr-int4.sha256')

$ExtractExe = Join-Path $ToolsDir 'dlssnr_extract_model.exe'
$Int4Exe    = Join-Path $ToolsDir 'dlssnr-int4-weights.exe'
$ExpDelta   = Join-Path $ToolsDir 'dlssnr-small-experimental.delta'
$OptDelta   = Join-Path $ToolsDir 'dlssnr-optimized.delta'

foreach ($req in @($ExtractExe, $Int4Exe, $ExpDelta, $OptDelta)) {
    if (-not (Test-Path -LiteralPath $req -PathType Leaf)) {
        Fail "工具文件不完整，请重新解压整合包 / Missing file: $req"
    }
}
if (-not (Test-Path -LiteralPath $Int4Data -PathType Container)) {
    Fail "缺少 int4-ultra 配置表目录 / Missing directory: $Int4Data"
}

$OutSilver = Join-Path $OutDir 'CC_SILVER_AARDWOLD.bin'
$OutSmall  = Join-Path $OutDir 'CC_SMALL_FP8.bin'
$OutExp    = Join-Path $OutDir 'CC_SMALL_FP8_EXPERIMENTAL.bin'
$OutOpt    = Join-Path $OutDir 'CC_SILVER_AARDWOLD_OPTIMIZED.bin'
$OutInt4   = Join-Path $OutDir 'CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin'

$ExpectedMap = [ordered]@{
    'CC_SMALL_FP8.bin'                      = @{ Path = $OutSmall;  Sha = $ShaSmall }
    'CC_SMALL_FP8_EXPERIMENTAL.bin'         = @{ Path = $OutExp;    Sha = $ShaExp   }
    'CC_SILVER_AARDWOLD.bin'                = @{ Path = $OutSilver; Sha = $ShaModel }
    'CC_SILVER_AARDWOLD_OPTIMIZED.bin'      = @{ Path = $OutOpt;    Sha = $ShaOpt   }
    'CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin' = @{ Path = $OutInt4;   Sha = $ShaInt4  }
}

$allReady = $true
foreach ($k in $ExpectedMap.Keys) {
    $info = $ExpectedMap[$k]
    if (-not (Test-Sha $info.Path $info.Sha)) { $allReady = $false; break }
}
if ($allReady -and -not $Force -and -not $Dll) {
    Ok '所有 5 个 DLSSNR-AMD 0.0.9m11 模型权重已存在且 SHA-256 校验全部通过！'
    Say "模型目录 / Output directory: $OutDir"
    foreach ($k in $ExpectedMap.Keys) {
        $p = $ExpectedMap[$k].Path
        $sz = (Get-Item -LiteralPath $p).Length
        Say ("  [OK] {0,-39} {1,12:N0} bytes" -f $k, $sz)
    }
    Finish 0
}

# Compile tiny native-speed XOR helper for 100+ MB NRMODEL1 packs (fallback to PS loop if restricted).
$script:UseNativeXor = $false
try {
    Add-Type -TypeDefinition @'
using System;
public static class VeyraNrXor {
    public static void XorInPlace(byte[] dst, int dstOff, byte[] src, int srcOff, int len) {
        for (int i = 0; i < len; i++) {
            dst[dstOff + i] ^= src[srcOff + i];
        }
    }
    public static byte[] XorCopy(byte[] a, int aOff, byte[] b, int bOff, int len) {
        var r = new byte[len];
        for (int i = 0; i < len; i++) {
            r[i] = (byte)(a[aOff + i] ^ b[bOff + i]);
        }
        return r;
    }
}
'@ -Language CSharp
    $script:UseNativeXor = $true
} catch {
    $script:UseNativeXor = $false
}

function Apply-NrWeightDelta([string]$basePackPath, [string]$deltaPath, [string]$outPackPath) {
    $pack = [System.IO.File]::ReadAllBytes($basePackPath)
    if ($pack.Length -lt 16 -or [System.Text.Encoding]::ASCII.GetString($pack, 0, 8) -ne 'NRMODEL1') {
        throw "$basePackPath 不是有效的 NRMODEL1 模型包"
    }
    $countPack = [BitConverter]::ToUInt32($pack, 8)
    $index = @{}
    $at = 16
    for ($k = 0; $k -lt $countPack; $k++) {
        $n = [BitConverter]::ToUInt32($pack, $at)
        $name = [System.Text.Encoding]::UTF8.GetString($pack, $at + 4, $n)
        $off = [BitConverter]::ToUInt64($pack, $at + 4 + $n)
        $sz  = [BitConverter]::ToUInt64($pack, $at + 12 + $n)
        $index[$name] = @($off, $sz)
        $at += 4 + $n + 16
    }
    $indexEnd = $at

    $d = [System.IO.File]::ReadAllBytes($deltaPath)
    if ($d.Length -lt 12 -or [System.Text.Encoding]::ASCII.GetString($d, 0, 8) -ne 'NRXDIFF1') {
        throw "$deltaPath 不是有效的 NRXDIFF1 增量补丁"
    }
    $count = [BitConverter]::ToUInt32($d, 8)
    $orig = $null
    $at = 12
    for ($k = 0; $k -lt $count; $k++) {
        $n = [BitConverter]::ToUInt32($d, $at)
        $ename = [System.Text.Encoding]::UTF8.GetString($d, $at + 4, $n)
        if (-not $index.ContainsKey($ename)) {
            $orig = [byte[]]$pack.Clone()
            break
        }
        $at += 4 + $n + 8 + [int64][BitConverter]::ToUInt64($d, $at + 4 + $n)
    }

    $added = New-Object System.Collections.Generic.List[object]
    $at = 12
    for ($k = 0; $k -lt $count; $k++) {
        $n = [BitConverter]::ToUInt32($d, $at)
        $name = [System.Text.Encoding]::UTF8.GetString($d, $at + 4, $n)
        $size = [int][BitConverter]::ToUInt64($d, $at + 4 + $n)
        $at += 4 + $n + 8
        if (-not $index.ContainsKey($name)) {
            $slash = $name.IndexOf('/')
            $base = if ($slash -ge 0) { $name.Substring($slash + 1) } else { '' }
            if (-not $index.ContainsKey($base) -or [int]$index[$base][1] -ne $size) {
                throw "${name}: 无法在基础模型中找到匹配的 $base ($size bytes)"
            }
            $boff = [int]$index[$base][0]
            if ($script:UseNativeXor) {
                $bytes = [VeyraNrXor]::XorCopy($orig, $boff, $d, $at, $size)
            } else {
                $bytes = New-Object byte[] $size
                for ($i = 0; $i -lt $size; $i++) { $bytes[$i] = $orig[$boff + $i] -bxor $d[$at + $i] }
            }
            $added.Add(@($name, $bytes))
        } else {
            if ([int]$index[$name][1] -ne $size) {
                throw "${name}: 大小不匹配 (期望 $size bytes)"
            }
            $off = [int]$index[$name][0]
            if ($script:UseNativeXor) {
                [VeyraNrXor]::XorInPlace($pack, $off, $d, $at, $size)
            } else {
                for ($i = 0; $i -lt $size; $i++) { $pack[$off + $i] = $pack[$off + $i] -bxor $d[$at + $i] }
            }
        }
        $at += $size
    }
    if ($at -ne $d.Length) {
        throw "${deltaPath}: 补丁尾部存在多余字节 ($($d.Length - $at))"
    }

    if ($added.Count -gt 0) {
        $grow = 0
        foreach ($e in $added) { $grow += 4 + [System.Text.Encoding]::UTF8.GetByteCount($e[0]) + 16 }
        $ms = New-Object System.IO.MemoryStream
        $bw = New-Object System.IO.BinaryWriter($ms)
        $bw.Write($pack, 0, 8)
        $bw.Write([uint32]($countPack + $added.Count))
        $bw.Write($pack, 12, 4)
        $pos = 16
        for ($k = 0; $k -lt $countPack; $k++) {
            $n = [BitConverter]::ToUInt32($pack, $pos)
            $bw.Write($pack, $pos, 4 + $n)
            $bw.Write([uint64]([BitConverter]::ToUInt64($pack, $pos + 4 + $n) + $grow))
            $bw.Write([uint64]([BitConverter]::ToUInt64($pack, $pos + 12 + $n)))
            $pos += 4 + $n + 16
        }
        $end = [uint64]($pack.Length + $grow)
        foreach ($e in $added) {
            $nb = [System.Text.Encoding]::UTF8.GetBytes($e[0])
            $bw.Write([uint32]$nb.Length)
            $bw.Write($nb)
            $bw.Write([uint64]$end)
            $bw.Write([uint64]$e[1].Length)
            $end += [uint64]$e[1].Length
        }
        $bw.Write($pack, $indexEnd, $pack.Length - $indexEnd)
        foreach ($e in $added) { $bw.Write([byte[]]$e[1]) }
        $bw.Flush()
        $pack = $ms.ToArray()
        $bw.Dispose()
    }
    [System.IO.File]::WriteAllBytes($outPackPath, $pack)
}

function Extract-FromDll([string]$dllPath, [string]$workOutDir) {
    New-Item -ItemType Directory -Force -Path $workOutDir | Out-Null
    & $ExtractExe $dllPath $workOutDir | ForEach-Object { Say "  [extract] $_" }
    $exCode = $LASTEXITCODE
    $sPath = Join-Path $workOutDir 'CC_SILVER_AARDWOLD.bin'
    $mPath = Join-Path $workOutDir 'CC_SMALL_FP8.bin'
    $okSilver = ($exCode -eq 0) -and (Test-Sha $sPath $ShaModel)
    $okSmall  = ($exCode -eq 0) -and (Test-Sha $mPath $ShaSmall)
    return @{
        ExitCode = $exCode
        SilverOk = $okSilver
        SmallOk  = $okSmall
        Silver   = $sPath
        Small    = $mPath
    }
}

$needExtract = $Force -or ($Dll -ne '') -or (-not (Test-Sha $OutSilver $ShaModel)) -or (-not (Test-Sha $OutSmall $ShaSmall))
if ($needExtract) {
    if (-not $Dll) {
        $autoCands = @(
            (Join-Path $VeyraRoot 'nvngx_dlssnr.dll'),
            (Join-Path $VeyraRoot 'versions\2.2.1\runtime\experimental\nvngx_dlssnr.dll')
        )
        foreach ($c in $autoCands) {
            if (Test-Path -LiteralPath $c -PathType Leaf) {
                $Dll = $c
                Say "自动检测到本地 DLL: $Dll"
                break
            }
        }
    }
    if (-not $Dll) {
        Say '本分享包未内置 NVIDIA 闭源模型权重，首次运行需要读取一次 NVIDIA 官方 nvngx_dlssnr.dll（建议 310.9.16 版本）'
        Say '或官方 Veyra 2.2.1 压缩包（仅离线提取权重数据，绝不运行该 DLL）。'
        Say ''
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = '选择 nvngx_dlssnr.dll (310.9.16) 或包含它的官方 Veyra/DLSS .zip 压缩包'
        $dlg.Filter = 'nvngx_dlssnr.dll or zip (*.dll;*.zip)|*.dll;*.zip'
        if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            Fail '未选择 nvngx_dlssnr.dll 或压缩包，已取消 / Cancelled.'
        }
        $Dll = $dlg.FileName
    }

    $Dll = $Dll.Trim('"')
    if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
        Fail "找不到指定文件 / File not found: $Dll"
    }
    $srcPath = (Resolve-Path -LiteralPath $Dll).Path
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ('veyra-m11-models-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $work | Out-Null

    try {
        $extRes = $null
        if ([System.IO.Path]::GetExtension($srcPath) -ieq '.zip') {
            Say "正在从压缩包中查找并提取 nvngx_dlssnr.dll: $srcPath"
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $zip = [System.IO.Compression.ZipFile]::OpenRead($srcPath)
            try {
                $entries = @($zip.Entries | Where-Object {
                    (-not $_.FullName.EndsWith('/')) -and
                    ([System.IO.Path]::GetFileName($_.FullName) -ieq 'nvngx_dlssnr.dll')
                })
                if ($entries.Count -eq 0) {
                    Fail "压缩包内未找到 nvngx_dlssnr.dll: $srcPath"
                }
                $ranked = $entries | Sort-Object {
                    $fn = $_.FullName.Replace('\', '/').ToLowerInvariant()
                    if ($fn -like '*amd-nr-rdna3*') { 0 }
                    elseif ($fn -like '*nr-ampere*') { 1 }
                    elseif ($fn -like '*experimental*') { 2 }
                    elseif ($fn -like '*nr-original*') { 4 }
                    else { 3 }
                }
                $idx = 0
                foreach ($entry in $ranked) {
                    $idx++
                    $tmpDll = Join-Path $work "cand_${idx}_nvngx_dlssnr.dll"
                    $inStream = $entry.Open()
                    try {
                        $outFs = [System.IO.File]::Create($tmpDll)
                        try { $inStream.CopyTo($outFs) } finally { $outFs.Close() }
                    } finally {
                        $inStream.Close()
                    }
                    Say "尝试提取 ($idx/$($ranked.Count)): $($entry.FullName)"
                    $tryRes = Extract-FromDll $tmpDll (Join-Path $work "out_$idx")
                    Remove-Item -LiteralPath $tmpDll -Force -ErrorAction SilentlyContinue
                    if ($tryRes.SilverOk -and $tryRes.SmallOk) {
                        $extRes = $tryRes
                        break
                    }
                    if ($tryRes.SilverOk -and (-not $extRes)) {
                        $extRes = $tryRes
                    }
                }
            } finally {
                $zip.Dispose()
            }
        } else {
            Say "正在从 DLL 提取基础模型权重: $srcPath"
            $extRes = Extract-FromDll $srcPath (Join-Path $work 'out')
        }

        if (-not $extRes -or -not $extRes.SilverOk) {
            Fail '未能从所选 DLL 提取有效的 CC_SILVER_AARDWOLD.bin（请确认使用 NVIDIA 310.9.16 或 310.8.0 的 nvngx_dlssnr.dll）'
        }
        Copy-Item -LiteralPath $extRes.Silver -Destination $OutSilver -Force
        Ok "  [OK] CC_SILVER_AARDWOLD.bin 已生成并校验通过"
        if ($extRes.SmallOk) {
            Copy-Item -LiteralPath $extRes.Small -Destination $OutSmall -Force
            Ok "  [OK] CC_SMALL_FP8.bin 已生成并校验通过"
        } else {
            Warn '  [WARN] 当前 DLL 仅包含 310.8.0 (CC_SILVER_AARDWOLD)，未生成 CC_SMALL_FP8.bin（建议改用 310.9.16 版本 DLL）。'
        }
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Build CC_SMALL_FP8_EXPERIMENTAL.bin via XOR delta
if ((Test-Sha $OutSmall $ShaSmall) -and ($Force -or -not (Test-Sha $OutExp $ShaExp))) {
    Say '正在合成 CC_SMALL_FP8_EXPERIMENTAL.bin ...'
    $tmpExp = "$OutExp.tmp"
    Apply-NrWeightDelta $OutSmall $ExpDelta $tmpExp
    if (-not (Test-Sha $tmpExp $ShaExp)) {
        Remove-Item -LiteralPath $tmpExp -Force -ErrorAction SilentlyContinue
        Fail 'CC_SMALL_FP8_EXPERIMENTAL.bin 校验和 mismatch'
    }
    Move-Item -LiteralPath $tmpExp -Destination $OutExp -Force
    Ok '  [OK] CC_SMALL_FP8_EXPERIMENTAL.bin 校验通过'
}

# Build CC_SILVER_AARDWOLD_OPTIMIZED.bin via XOR delta
if ((Test-Sha $OutSilver $ShaModel) -and ($Force -or -not (Test-Sha $OutOpt $ShaOpt))) {
    Say '正在合成 CC_SILVER_AARDWOLD_OPTIMIZED.bin ...'
    $tmpOpt = "$OutOpt.tmp"
    Apply-NrWeightDelta $OutSilver $OptDelta $tmpOpt
    if (-not (Test-Sha $tmpOpt $ShaOpt)) {
        Remove-Item -LiteralPath $tmpOpt -Force -ErrorAction SilentlyContinue
        Fail 'CC_SILVER_AARDWOLD_OPTIMIZED.bin 校验和 mismatch'
    }
    Move-Item -LiteralPath $tmpOpt -Destination $OutOpt -Force
    Ok '  [OK] CC_SILVER_AARDWOLD_OPTIMIZED.bin 校验通过'
}

# Build CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin via dlssnr-int4-weights.exe
if ((Test-Sha $OutOpt $ShaOpt) -and ($Force -or -not (Test-Sha $OutInt4 $ShaInt4))) {
    Say '正在生成 INT4 分层权重包 CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin ...'
    $tmpInt4 = "$OutInt4.tmp"
    & $Int4Exe $OutOpt $Int4Data $tmpInt4 | ForEach-Object { Say "  [int4] $_" }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Sha $tmpInt4 $ShaInt4)) {
        Remove-Item -LiteralPath $tmpInt4 -Force -ErrorAction SilentlyContinue
        Fail 'CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin 生成或校验失败'
    }
    Move-Item -LiteralPath $tmpInt4 -Destination $OutInt4 -Force
    Ok '  [OK] CC_SILVER_AARDWOLD_OPTIMIZED_INT4.bin 校验通过'
}

Say ''
Say '=== DLSSNR-AMD 0.0.9m11 模型校验汇总 ==='
$missingCount = 0
foreach ($k in $ExpectedMap.Keys) {
    $info = $ExpectedMap[$k]
    if (Test-Sha $info.Path $info.Sha) {
        $sz = (Get-Item -LiteralPath $info.Path).Length
        Ok ("  [OK] {0,-39} {1,12:N0} bytes  SHA256={2}" -f $k, $sz, $info.Sha.Substring(0, 12))
    } else {
        $missingCount++
        Warn ("  [MISSING] {0,-34} (needs 310.9.16 nvngx_dlssnr.dll)" -f $k)
    }
}

if ($missingCount -gt 0) {
    Fail "有 $missingCount 个模型未完成生成或校验，请使用 NVIDIA 310.9.16 版本的 nvngx_dlssnr.dll 重试。"
}

Ok ''
Ok '全部 5 个模型权重准备完成，现在可以直接双击 Veyra.exe 启动！'
Finish 0
