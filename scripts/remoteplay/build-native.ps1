[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ChiakiCheckout,
    [string]$Root = "",
    [string]$StageDirectory = "",
    [string]$BuildDirectory = "",
    [string]$PrefixPath = "",
    [string]$ToolchainFile = "",
    [string]$ProtocPath = "",
    [string]$PkgConfigPath = "",
    [string]$Generator = "Ninja"
)
$ErrorActionPreference = "Stop"
if (-not $Root) { $Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path }
else { $Root = (Resolve-Path $Root).Path }
$ChiakiCheckout = (Resolve-Path $ChiakiCheckout).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $Root "out/remoteplay/native" }
if (-not $StageDirectory) { $StageDirectory = Join-Path $Root "out/remoteplay/chiaki-msvc-stage" }
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
$PinnedCommit = "0e16950165f06e5c3291537c2eeba6e852be7120"
$PatchFile = Join-Path $Root "scripts/remoteplay/patches/0001-chiaki-msvc-vla-compat.patch"
$head = (& git -C $ChiakiCheckout rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne $PinnedCommit) { throw "Chiaki source pin mismatch: $head" }
$sourceStatus = (& git -C $ChiakiCheckout status --porcelain --untracked-files=no) -join "`n"
if ($LASTEXITCODE -ne 0 -or $sourceStatus) { throw "Chiaki source checkout must be clean" }
if (-not (Test-Path -LiteralPath (Join-Path $StageDirectory ".git"))) {
    if (Test-Path -LiteralPath $StageDirectory) { throw "Refusing to replace an existing non-worktree stage: $StageDirectory" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StageDirectory) | Out-Null
    Invoke-Checked "git" @("-C",$ChiakiCheckout,"worktree","add","--detach",$StageDirectory,$PinnedCommit)
    Invoke-Checked "git" @("-C",$StageDirectory,"submodule","update","--init","--recursive")
}
$stageHead = (& git -C $StageDirectory rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $stageHead -ne $PinnedCommit) { throw "Chiaki staging pin mismatch: $stageHead" }
# Each reviewed patch can be applied once to an existing verified base stage.
# Full content verification below rejects unrelated changes even in these files.
$patches = @(
    "0001-chiaki-msvc-vla-compat.patch",
    "0002-chiaki-video-metadata.patch",
    "0003-chiaki-feedback-history.patch",
    "0004-chiaki-pad-proc-no.patch"
)
foreach ($name in $patches) {
    $patch = Join-Path $Root "scripts/remoteplay/patches/$name"
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue" # reverse-check failure means patch not applied yet
        & git -C $StageDirectory apply --reverse --check $patch 2>$null
        $alreadyApplied = $LASTEXITCODE -eq 0
    } finally { $ErrorActionPreference = $savedPreference }
    if (-not $alreadyApplied) {
        Invoke-Checked "git" @("-C",$StageDirectory,"apply","--check",$patch)
        Invoke-Checked "git" @("-C",$StageDirectory,"apply",$patch)
    }
}
Invoke-Checked "python" @((Join-Path $Root "scripts/remoteplay/verify-chiaki-stage.py"),"--stage",$StageDirectory,"--clean",$ChiakiCheckout)
$Args = @("-S", (Join-Path $Root "services/remoteplay-probe"), "-B", $BuildDirectory,
    "-G", $Generator, "-DCMAKE_BUILD_TYPE=Release", "-DVEYRA_RP_BUILD_NATIVE=ON",
    "-DVEYRA_RP_CHIAKI_SOURCE_DIR=$StageDirectory", "-DVEYRA_RP_CHIAKI_VERIFY_DIR=$ChiakiCheckout",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
if ($PrefixPath) { $Args += "-DCMAKE_PREFIX_PATH=$PrefixPath" }
if ($ToolchainFile) { $Args += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile" }
if ($ProtocPath) {
    $ProtocPath = (Resolve-Path $ProtocPath).Path
    $Args += "-DPROTOC=$ProtocPath"
}
if ($PkgConfigPath) {
    $PkgConfigPath = (Resolve-Path $PkgConfigPath).Path
    $Args += "-DPKG_CONFIG_EXECUTABLE=$PkgConfigPath"
}
Invoke-Checked "cmake" $Args
Invoke-Checked "cmake" @("--build", $BuildDirectory, "--config", "Release", "--target", "veyra_remoteplay_native_probe")
Invoke-Checked "ctest" @("--test-dir", $BuildDirectory, "-C", "Release", "-L", "native-core", "--output-on-failure", "--timeout", "30")
Write-Host "Native initialization gate completed. PS5 connection, decoder and GPU are still NOT tested by this probe."
