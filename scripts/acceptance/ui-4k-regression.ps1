[CmdletBinding()]
param([string]$Root=(Join-Path $PSScriptRoot '../..'),[ValidateRange(1,300)][int]$MaxRuntimeSeconds=300)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $Root
# Supplement the CURRENT Repair v2 CFR contract. The protected delivery gate
# remains unchanged and FAILED at its older 23-frame assertion; this is not a
# replacement Phase gate or a way to claim that protected gate passed.
$timer=[Diagnostics.Stopwatch]::StartNew()
$run=[guid]::NewGuid().ToString('N')
$dir=Join-Path $Root ('logs/ui-dual-mode/4k-'+$run)
New-Item -ItemType Directory -Path $dir|Out-Null
$bin=Join-Path $Root 'out/build/x64-release'
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed,[string]$Detail){
    $checks.Add([ordered]@{name=$Name;passed=$Passed;detail=$Detail})
    if(-not $Passed){throw "$Name : $Detail"}
}
function Run([string]$Name,[string]$Exe,[string[]]$Argv,[int]$ExpectedExit=0){
    $remaining=$MaxRuntimeSeconds-$timer.Elapsed.TotalSeconds
    if($remaining -lt 1){throw 'Per-invocation time limit reached'}
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $p=Start-Process -FilePath $Exe -ArgumentList @($Argv|ForEach-Object{'"'+$_.Replace('"','\"')+'"'}) -PassThru -WindowStyle Hidden -RedirectStandardOutput "$dir/$Name.stdout.log" -RedirectStandardError "$dir/$Name.stderr.log"
    $processHandle=$p.Handle
    if(-not $p.WaitForExit([int]([Math]::Min(60,$remaining)*1000))){
        & "$env:SystemRoot/System32/taskkill.exe" /PID $p.Id /T /F|Out-Null
        throw "$Name timed out"
    }
    Check "$Name-exit" ($p.ExitCode -eq $ExpectedExit) "exit=$($p.ExitCode) expected=$ExpectedExit seconds=$($watch.Elapsed.TotalSeconds)"
}
$passed=$false;$failure=$null
try {
    if(Test-Path -LiteralPath 'loop/STOP'){throw 'User STOP exists'}
    $ffmpeg=(Get-Command ffmpeg).Source;$ffprobe=(Get-Command ffprobe).Source
    $source=(Resolve-Path 'loop/local/fixed_clips/test_av_4k.mp4').Path
    foreach($codec in @('h264','hevc')){
        $fixture="$dir/bframes-$codec.mp4"
        $encoder=if($codec -eq 'h264'){'libx264'}else{'libx265'}
        $args=@('-nostdin','-v','error','-i',$source,'-frames:v','24','-an','-c:v',$encoder,'-preset','ultrafast','-bf','2','-g','12','-threads','4')
        if($codec -eq 'hevc'){$args+=@('-x265-params','pools=4:frame-threads=4:log-level=error')}
        Run "fixture-$codec" $ffmpeg ($args+@($fixture))
        Run "fixture-inspect-$codec" $ffprobe @('-v','error','-count_frames','-show_streams','-of','json',$fixture)
        $stream=(Get-Content "$dir/fixture-inspect-$codec.stdout.log" -Raw|ConvertFrom-Json).streams[0]
        Check "fixture-bframes-$codec" ($stream.has_b_frames -gt 0 -and [int]$stream.nb_read_frames -eq 24) '24 real 4K frames with B-frame reorder'
        Run "threaded-EOS-seek-$codec" "$bin/veyra_source_tests.exe" @($fixture,'--threaded')
        $threadLog=Get-Content "$dir/threaded-EOS-seek-$codec.stdout.log" -Raw
        Check "threaded-proof-$codec" ($threadLog -match 'threads=1 ' -and $threadLog -match 'threads=4 activeThreadType=1' -and $threadLog -match 'threaded-file: 9 checks, 0 failures' -and ([regex]::Matches($threadLog,'\[PASS\] threaded:every-pixel-and-PTS-equal')).Count -eq 2) 'actual frame-threaded codec, first read plus seek-after-EOS reread, all effective YUV pixel Adler32 and PTS match'
        $file="$dir/native-$codec.mp4"
        $exportArgs=@($source,'--export-out',$file,'--max-frames','12','--fg')
        if($codec -eq 'hevc'){$exportArgs+='--hevc'}
        Run "export-$codec" "$bin/veyra.exe" $exportArgs
        $exportLog=Get-Content "$dir/export-$codec.stdout.log" -Raw
        Check "native-graph-counts-$codec" ($exportLog -match '\[resolution\] source=3840x2160 base=3840x2160 nr=1920x1080 flow=3840x2160 fg=3840x2160 output=3840x2160' -and $exportLog -match '\[export-counts\] source=12 generated=11 hold=1 output=24 multiplier=2 ') 'actual graph extents and separate source/generated/hold/output counters'
        Run "inspect-$codec" $ffprobe @('-v','error','-count_frames','-show_streams','-of','json',$file)
        $streams=(Get-Content "$dir/inspect-$codec.stdout.log" -Raw|ConvertFrom-Json).streams
        $v=@($streams|Where-Object codec_type -eq 'video')[0];$a=@($streams|Where-Object codec_type -eq 'audio')
        Check "native-CFR-$codec" ($v.width -eq 3840 -and $v.height -eq 2160 -and $v.codec_name -eq $codec -and [int]$v.nb_read_frames -eq 24 -and $v.r_frame_rate -eq '120/1' -and [Math]::Abs([double]$v.duration-0.2) -lt 0.001 -and $a.Count -eq 1 -and $a[0].codec_name -eq 'aac' -and [Math]::Abs([double]$a[0].duration-[double]$v.duration) -lt 0.05) '12 source + 11 generated + 1 explicit tail hold = 24 CFR frames at 120fps, preserves 0.2s source duration and AAC'
        Run "decode-$codec" $ffmpeg @('-nostdin','-v','error','-xerror','-i',$file,'-f','null','-')
    }
    Run 'cancel' "$bin/veyra.exe" @($source,'--export-out',"$dir/cancel.mp4",'--cancel-after-ms','3000') 3
    Check 'cancel-partial' ((-not(Test-Path "$dir/cancel.mp4")) -and (Test-Path "$dir/cancel.mp4.partial")) 'cancel never promotes unfinished native4K output'
    Check 'time-limit' ($timer.Elapsed.TotalSeconds -lt $MaxRuntimeSeconds) 'single test invocation <=300 seconds'
    $passed=$true
} catch {$failure=$_.ToString()}
[ordered]@{runId=$run;passed=$passed;seconds=$timer.Elapsed.TotalSeconds;exeSha256=(Get-FileHash "$bin/veyra.exe").Hash;sourceTestSha256=(Get-FileHash "$bin/veyra_source_tests.exe").Hash;error=$failure;checks=$checks;protectedGate='unchanged; latest run failed obsolete 23-frame assertion';scope='current CFR and threaded file input regression; no capture or long stability'}|ConvertTo-Json -Depth 8|Set-Content "$dir/result.json" -Encoding UTF8
Write-Host "4K current-contract regression passed=$passed evidence=$dir/result.json error=$failure"
if($passed){exit 0}else{exit 1}
