[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Root,[switch]$VisiblePlayer,[Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$PlayerExe,
    [Parameter(Mandatory=$true)][ValidateSet('legacy')][string]$UiTarget,[switch]$PortablePlayer,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$FixtureRoot)
# User-authorized local software acceptance, <=300s for this entire suite.
# Native-4K realtime60 is NOT asserted: user selected an explicit 1080 working realtime profile.
# Real capture acceptance and proprietary distribution are separate, never synthetic PASS.
$ErrorActionPreference='Stop'
# A PowerShell 7 parent can pass module paths that exclude Windows PowerShell's
# own Utility module. Resolve hashing/JSON cmdlets from the executing host.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force
$Root=(Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $Root
$timer=[Diagnostics.Stopwatch]::StartNew()
$run=[Guid]::NewGuid().ToString('N')
$dir=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $run
[IO.Directory]::CreateDirectory($dir)|Out-Null
$entryScript=Join-Path $Root 'scripts/acceptance/resolve-ui-migration-entry.ps1'
$null=& $entryScript -Root $Root -UiTarget $UiTarget -PlayerExe $PlayerExe -BuildDirectory $BuildDirectory -StagingDirectory (Split-Path -Parent $PlayerExe) -ArtifactDirectory $dir -OutputFile (Join-Path $dir 'entry.json')
if($UiTarget -ne 'legacy'){throw 'delivery.ps1 is legacy-only. QML uses the separate qml-ui-smoke contract.'}
# The quality probe reads its arguments from the UTF-16 command line, so the
# absolute output directory works even when it is on another drive or has
# Unicode names (a relative path cannot cross from the repo drive to E:).
$qualityDir=$dir
$bin=Join-Path $Root 'out/build/x64-release'
if($BuildDirectory){$bin=(Resolve-Path -LiteralPath $BuildDirectory).Path}
$PlayerExe=(Resolve-Path -LiteralPath $PlayerExe).Path
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed,[string]$Detail){$checks.Add([pscustomobject]@{name=$Name;passed=$Passed;detail=$Detail});if(-not $Passed){throw "$Name : $Detail"}}
function Run([string]$Name,[string]$Exe,[string[]]$Argv,[int]$Limit=30,[int]$ExpectedExit=0){
    $remaining=290-[int]$timer.Elapsed.TotalSeconds
    if($remaining -lt 1){throw 'Combined suite budget exhausted'}
    $timeout=[Math]::Min($Limit,$remaining)
    $windowStyle='Hidden'
    if($VisiblePlayer -and $Argv -contains '--smoke-seconds'){$windowStyle='Normal'}
    $savedPath=$env:PATH
    try {
        if($PortablePlayer -and $Exe -eq $PlayerExe){$env:PATH="$env:SystemRoot/System32;$env:SystemRoot"}
        $workingDirectory=$Root
        if($PortablePlayer -and $Exe -eq $PlayerExe){$workingDirectory=Split-Path $Exe}
        # A test with no arguments is valid; Start-Process rejects an empty
        # -ArgumentList, so only pass it when there is something to pass.
        $startArgs=@{FilePath=$Exe;WorkingDirectory=$workingDirectory;PassThru=$true;WindowStyle=$windowStyle;RedirectStandardOutput="$dir/$Name.stdout.log";RedirectStandardError="$dir/$Name.stderr.log"}
        $forwarded=@($Argv|ForEach-Object{'"'+$_.Replace('"','\"')+'"'})
        if($forwarded.Count -gt 0){$startArgs.ArgumentList=$forwarded}
        $p=Start-Process @startArgs
    } finally {$env:PATH=$savedPath}
    $processHandle=$p.Handle
    if(-not $p.WaitForExit($timeout*1000)){Stop-Process -Id $p.Id -Force;throw "$Name timeout"}
    $exitCode=$p.ExitCode;Check "$Name-exit" ($null -ne $exitCode -and $exitCode -eq $ExpectedExit) "exit=$exitCode expected=$ExpectedExit"
}
try{
    $ffprobe=(Get-Command ffprobe -ErrorAction Stop).Source
    $clip1080=(Resolve-Path -LiteralPath (Join-Path $FixtureRoot 'test_av_1080p.mp4')).Path
    $clip4k=(Resolve-Path -LiteralPath (Join-Path $FixtureRoot 'test_av_4k.mp4')).Path
    Run quality1080 "$bin/veyra_quality_probe.exe" @('--input',$clip1080,'--native','--guidance','motion','--frames','60','--diag','--json-file',"$qualityDir/quality1080.json",'--dump',"$qualityDir/enhanced.png") 45
    $q=Get-Content "$dir/quality1080.json" -Raw|ConvertFrom-Json
    Check core-correct ($q.failures -eq 0 -and $q.processedFrames -eq 60 -and $q.nrEvaluateCount -eq 60 -and $q.nrMotionFrames -gt 0 -and $q.nvofExecuteCount -gt 0 -and $q.nonZeroMotionCount -gt 0 -and $q.diagnosticsEnabled -and $q.d3dDiagErrors -eq 0 -and $q.normalPathReadbackCount -eq 0) 'actual NR/NVOF/motion/GBV/diagnostic PNG'
    Run quality4k "$bin/veyra_quality_probe.exe" @('--input',$clip4k,'--native','--guidance','motion','--frames','12','--json-file',"$qualityDir/quality4k.json",'--dump',"$qualityDir/enhanced4k.png")
    $q4=Get-Content "$dir/quality4k.json" -Raw|ConvertFrom-Json
    Check native4k ($q4.failures -eq 0 -and $q4.sourceExtent.width -eq 3840 -and $q4.workingExtent.width -eq 3840 -and $q4.nrEvaluateCount -eq 12) 'native4K correctness only; not a realtime60 claim'
    Run player $PlayerExe @($clip4k,'--nr','--realtime','--fg','--smoke-seconds','6') 20
    $log=Get-Content "$dir/player.stdout.log" -Raw
    Check player-sync ($log -match 'smoke frames=(\d+).*generated=(\d+).*failed=false.*absLatenessP95Ms=([\d.]+)' -and [int]$Matches[1] -gt 60 -and [int]$Matches[2] -gt 0 -and [double]$Matches[3] -le 50) 'real-time profile 4K ingress, generated frames, observed abs lateness P95 <=50ms'
    Run controls $PlayerExe @($clip1080,'--nr','--realtime','--smoke-seconds','6','--smoke-controls','--smoke-save',"$dir/snapshot.jpg") 20
    # Colour page (T3): master switch, slider -> engine wiring, one-click reset
    # with undo, accordion folding and its ui-preferences persistence.
    Run color-page $PlayerExe @($clip1080,'--smoke-color','--smoke-seconds','12') 25
    $controlLog=Get-Content "$dir/controls.stdout.log" -Raw
    Check paused-seek ($controlLog -match 'seek done' -and $controlLog -match 'controlsStep=4' -and (Test-Path "$dir/snapshot.jpg")) 'pause / seek while paused / resume / real WIC JPEG'
    Run image $PlayerExe @("$dir/enhanced.png",'--nr','--smoke-seconds','3') 15
    Run image4k $PlayerExe @("$dir/enhanced4k.png",'--nr','--realtime','--smoke-seconds','6','--smoke-controls','--smoke-save',"$dir/image4k.jpg") 15
    Run inspect-image4k $ffprobe @('-v','error','-show_streams','-of','json',"$dir/image4k.jpg") 10
    $img=(Get-Content "$dir/inspect-image4k.stdout.log" -Raw|ConvertFrom-Json).streams[0]
    Check native4k-image ($img.width -eq 3840 -and $img.height -eq 2160) 'realtime default must not shrink image export'
    foreach($codec in @('h264','hevc')){
        $file="$dir/$codec.mp4"
        $a=@($clip4k,'--nr','--export-out',$file,'--max-frames','12','--fg');if($codec -eq 'hevc'){$a+='--hevc'}
        Run "export-$codec" $PlayerExe $a 30
        Run "inspect-$codec" $ffprobe @('-v','error','-count_frames','-show_streams','-of','json',$file) 10
        $streams=(Get-Content "$dir/inspect-$codec.stdout.log" -Raw|ConvertFrom-Json).streams
        $v=@($streams|Where-Object codec_type -eq 'video')[0];$a=@($streams|Where-Object codec_type -eq 'audio')
        # The approved CFR tail policy preserves all 12/60 seconds: 12 source
        # pictures + 11 actual interpolations + 1 explicit tail hold = 24/120.
        # This is a developer gate only; the per-export integrity path is unchanged.
        $exportLog=Get-Content "$dir/export-$codec.stdout.log" -Raw
        Check "frame-kinds-$codec" ($exportLog -match 'export-counts\] source=12 generated=11 hold=1 output=24 multiplier=2') '12 real + 11 generated + 1 declared CFR tail hold; never count hold as DLSSG'
        Check "decode-$codec" ($v.width -eq 3840 -and $v.height -eq 2160 -and $v.codec_name -eq $codec -and [int]$v.nb_read_frames -eq 24 -and $v.avg_frame_rate -eq '120/1' -and [math]::Abs([double]$v.duration-0.2) -lt 0.00001 -and $a.Count -ge 1) 'all short frames decoded; native4K 2X CFR frame count, duration, rate and audio'
    }
    Run cancel $PlayerExe @($clip4k,'--nr','--export-out',"$dir/cancel.mp4",'--cancel-after-ms','3000') 15 3
    Check cancel-not-success ((-not (Test-Path "$dir/cancel.mp4")) -and (Test-Path "$dir/cancel.mp4.partial")) 'cancel drains with live callback; never promotes incomplete output'
    # Compressed capture payload decoder: the exact backend the capture worker
    # uses, driven with local H.264/HEVC elementary streams. Exits 0 with an
    # explicit SKIP when the git-ignored local corpus is absent.
    Run capture-decode "$bin/veyra_capture_compressed_tests.exe" @() 30
    $decodeLog=Get-Content "$dir/capture-decode.stdout.log" -Raw
    Check capture-decode-result (($decodeLog -match 'ALL PASS') -or ($decodeLog -match 'SKIP')) 'hardware+software decode parity for H.264/HEVC, or an explicit skip'
    Check test-budget ($timer.Elapsed.TotalSeconds -lt 300) "$($timer.Elapsed.TotalSeconds)s; short validation is not endurance proof"
    $result=[ordered]@{runId=$run;status='software_short_gate_passed';capture='awaiting_user_capture_test';distribution='not_assessed_by_software_gate';seconds=$timer.Elapsed.TotalSeconds;exeHash=(Get-FileHash $PlayerExe -Algorithm SHA256).Hash;checks=$checks}
    $result|ConvertTo-Json -Depth 8|Set-Content "$dir/result.json" -Encoding UTF8
    Write-Host "DELIVERY SHORT GATE PASS: $dir/result.json";exit 0
}catch{
    [ordered]@{runId=$run;status='failed';seconds=$timer.Elapsed.TotalSeconds;error=$_.ToString();checks=$checks}|ConvertTo-Json -Depth 8|Set-Content "$dir/result.json" -Encoding UTF8
    Write-Host "DELIVERY SHORT GATE FAIL: $_ ($dir/result.json)";exit 1
}
