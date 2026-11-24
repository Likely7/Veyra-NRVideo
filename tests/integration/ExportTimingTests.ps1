param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
# This probe tests encoder and muxer timestamps. Generated-frame cadence
# runs in veyra_vfg_export_probe and the corresponding VFG tests.
$exe=Join-Path (Resolve-Path $BuildDirectory).Path 'veyra_export_probe.exe'
$directory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $directory -ErrorAction Stop | Out-Null
$fixture=Join-Path $directory 'irregular.mkv'
# Change cadence halfway through. The old CFR preflight rejected this file.
& ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=640x360:rate=30:duration=2' -f lavfi -i 'sine=frequency=880:sample_rate=48000:duration=3' -vf "setpts='if(lt(N,30),N/(30*TB),(1+(N-30)/15)/TB)'" -fps_mode passthrough -c:v libx264 -preset veryfast -bf 0 -c:a ac3 $fixture
if($LASTEXITCODE){throw 'Fixture generation failed'}
function Probe([string]$path,[string]$selector,[string]$entries,[string]$show){
    $json=& ffprobe -v error -select_streams $selector $show -show_entries $entries -of json $path
    if($LASTEXITCODE){throw "ffprobe failed: $path"}
    return ($json -join "`n" | ConvertFrom-Json)
}
function RunExport([string]$name,[string[]]$flags,[int]$expectedExit=0){
    $destination=Join-Path $directory "$name.mp4"
    $arguments=@($fixture,$destination)+$flags
    $quoted=@($arguments | ForEach-Object {'"'+$_+'"'})
    $logName=$name.Replace('/','-')
    $process=Start-Process -FilePath $exe -ArgumentList $quoted -PassThru -WindowStyle Hidden -RedirectStandardOutput "$directory/$logName.stdout.log" -RedirectStandardError "$directory/$logName.stderr.log"
    $null=$process.Handle
    if(!$process.WaitForExit(120000)){Stop-Process -Id $process.Id -Force;throw "$name timed out"}
    $process.Refresh()
    if($process.ExitCode -ne $expectedExit){throw "$name exit=$($process.ExitCode) expected=$expectedExit"}
    return $destination
}
$source=(Probe $fixture 'v:0' 'frame=best_effort_timestamp_time' '-show_frames').frames
if($source.Count -ne 60){throw 'Unexpected source frame count'}
$results=@()
foreach($case in @(
    @{name='irregular-h264';multiplier=1;flags=@()},
    @{name='irregular-hevc';multiplier=1;flags=@('--hevc')},
    @{name='irregular-mf';multiplier=1;flags=@();forceMf=$true},
    @{name='irregular-nvenc-fallback';multiplier=1;flags=@();failNvenc=$true}
)){
    try {
        if($case.forceMf){$env:VEYRA_TEST_FORCE_MF_ENCODER='1'}
        if($case.failNvenc){$env:VEYRA_TEST_NVENC_FIRST_OPEN_FAILS='1'}
        $output=RunExport $case.name $case.flags
    } finally {
        Remove-Item Env:VEYRA_TEST_FORCE_MF_ENCODER,Env:VEYRA_TEST_NVENC_FIRST_OPEN_FAILS -ErrorAction SilentlyContinue
    }
    $packets=(Probe $output 'v:0' 'packet=pts_time,duration_time' '-show_packets').packets
    if($packets.Count -ne $source.Count*$case.multiplier){throw "$($case.name): frame count mismatch"}
    $maxError=0.0
    for($i=0;$i -lt $source.Count;$i++){
        $timestampError=[math]::Abs([double]$packets[$i*$case.multiplier].pts_time-([double]$source[$i].best_effort_timestamp_time-[double]$source[0].best_effort_timestamp_time))
        $maxError=[math]::Max($timestampError,$maxError)
        if($timestampError -gt 0.000002){throw "$($case.name): source timestamp shifted by $timestampError seconds"}
        if($i+1 -lt $source.Count){
            $start=[double]$packets[$i*$case.multiplier].pts_time
            $end=[double]$packets[($i+1)*$case.multiplier].pts_time
            for($j=1;$j -lt $case.multiplier;$j++){
                $expected=$start+($end-$start)*$j/$case.multiplier
                if([math]::Abs([double]$packets[$i*$case.multiplier+$j].pts_time-$expected) -gt 0.000002){throw 'Generated timestamp misplaced'}
            }
        }
    }
    for($i=0;$i+1 -lt $packets.Count;$i++){
        $end=[double]$packets[$i].pts_time+[double]$packets[$i].duration_time
        if([math]::Abs($end-[double]$packets[$i+1].pts_time) -gt 0.000002){throw 'Noncontiguous sample duration'}
    }
    $audio=(Probe $output 'a:0' 'stream=codec_name,duration' '-show_streams').streams
    if($audio.Count -ne 1 -or $audio[0].codec_name -ne 'ac3' -or [double]$audio[0].duration -lt 2.9){throw 'Audio timeline lost'}
    # Independent development check only; the product no longer reopens output.
    & ffmpeg -v error -xerror -i $output -f null -
    if($LASTEXITCODE){throw 'Output decode failed'}
    $results+=@{case=$case.name;frames=$packets.Count;maxTimestampErrorSeconds=$maxError;audioSeconds=$audio[0].duration;passed=$true}
    Write-Output "PASS $($case.name): $($packets.Count) frames, source timestamps and audio preserved"
}
$failedOutput=RunExport 'missing-directory/write-error' @() 1
if(Test-Path -LiteralPath $failedOutput){throw 'Write failure reported success'}
$results | ConvertTo-Json -Depth 4 | Set-Content "$directory/result.json" -Encoding utf8
Write-Output 'PASS real output I/O failure remains a failure'
