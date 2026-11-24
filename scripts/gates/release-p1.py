"""Generate owned fixtures and exercise the three 2026-09-13 release P1 fixes.

Uses the production engine through its headless export probe, FFmpeg/ffprobe, and the local NVIDIA GPU.
All outputs go into a new directory; existing evidence is never overwritten.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
parser.add_argument('--build-directory', type=pathlib.Path, required=True)
parser.add_argument('--output-directory', type=pathlib.Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
build = args.build_directory.resolve()
output = args.output_directory.resolve()
output.mkdir(parents=True, exist_ok=False)
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('FFmpeg and ffprobe must be on PATH')
test = build / 'veyra_file_safety_tests.exe'
player = build / 'veyra_export_probe.exe'
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
records = []
assertions = []


def run(name, command, expected=0):
    command = [str(item) for item in command]
    start = time.monotonic()
    with (output / (name + '.stdout.log')).open('wb') as stdout, (output / (name + '.stderr.log')).open('wb') as stderr:
        try:
            code = subprocess.run(command, cwd=root, stdout=stdout, stderr=stderr,
                                  timeout=180, startupinfo=startup).returncode
        except subprocess.TimeoutExpired:
            code = 124
    records.append(dict(name=name, command=command, exit=code, expected=expected,
                        passed=code == expected, seconds=round(time.monotonic() - start, 3)))
    save()
    print(f'{name}: exit={code} expected={expected}', flush=True)
    return code == expected


def check(name, condition):
    assertions.append(dict(name=name, passed=bool(condition)))
    save()
    print(('PASS ' if condition else 'FAIL ') + name, flush=True)


def save():
    (output / 'result.json').write_text(json.dumps(dict(runs=records, assertions=assertions,
        passed=all(r['passed'] for r in records + assertions)), indent=2), encoding='utf-8')


def make_clip(name, duration):
    path = output / (name + '.mp4')
    if not run('make-' + name, [ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
        f'testsrc2=size=640x360:rate=30:duration={duration}', '-f', 'lavfi', '-i',
        f'sine=frequency=400:sample_rate=48000:duration={duration}', '-c:v', 'libx264',
        '-preset', 'ultrafast', '-g', '1', '-bf', '0', '-c:a', 'aac', '-movflags', '+faststart', path]):
        raise SystemExit('Fixture generation failed')
    return path


def damage_tail(source, name):
    run('packets-' + name, [ffprobe, '-v', 'error', '-select_streams', 'v:0', '-show_packets',
                           '-show_entries', 'packet=pos,size', '-of', 'json', source])
    packets = json.loads((output / ('packets-' + name + '.stdout.log')).read_text())['packets']
    data = bytearray(source.read_bytes())
    for packet in packets[-8:]:
        position, size = int(packet['pos']), int(packet['size'])
        data[position:position + size] = bytes(size)
    path = output / (name + '.mp4')
    path.write_bytes(data)
    return path


short = make_clip('healthy', 2)
long = make_clip('healthy-long', 8)
bad_short = damage_tail(short, 'bad-early')
bad_late = damage_tail(long, 'bad-late')
for name, size, seconds in [('small', '320x180', 5), ('large', '640x360', 1)]:
    run('make-' + name, [ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
        f'testsrc2=size={size}:rate=30:duration={seconds}', '-c:v', 'libx264', '-preset', 'ultrafast',
        '-g', '30', '-bf', '0', '-f', 'h264', output / (name + '.h264')])
(output / 'resize.h264').write_bytes((output / 'small.h264').read_bytes() + (output / 'large.h264').read_bytes())
resize = output / 'resize.mp4'
run('mux-resize', [ffmpeg, '-v', 'error', '-fflags', '+genpts', '-r', '30', '-i',
                  output / 'resize.h264', '-c:v', 'copy', resize])
bframes = output / 'bframes-4k.mp4'
run('make-bframes-4k', [ffmpeg, '-v', 'error', '-f', 'lavfi', '-i',
    'testsrc2=size=3840x2160:rate=30:duration=0.8', '-c:v', 'libx264', '-preset', 'ultrafast',
    '-g', '12', '-bf', '3', '-threads', '4', bframes])

run('graph-contract', [test, 'graph'])
for name, path, frames, damaged in [('healthy', short, 60, False), ('healthy-long', long, 240, False),
                                  ('bframes-4k', bframes, 24, False), ('bad-early', bad_short, 60, True),
                                  ('bad-late', bad_late, 240, True), ('resize', resize, 180, True)]:
    run('source-' + name, [test, 'source-bad' if damaged else 'source-good', path, frames])
run('threaded-bframes-reference', [build / 'veyra_source_tests.exe', bframes, '--threaded'])
run('play-eof', [test, 'play-eof', short])
run('play-overload-eof', [test, 'play-overload-eof', short])

for name, path, expected, partial in [('healthy', short, 0, False), ('healthy-long', long, 0, False),
    ('bad-early', bad_short, 1, False), ('bad-late', bad_late, 1, True), ('resize', resize, 1, True)]:
    destination = output / (name + '-export.mp4')
    run('export-' + name, [player, path, destination], expected)
    check(name + '-final-file-state', destination.exists() == (expected == 0))
    check(name + '-partial-state', pathlib.Path(str(destination) + '.partial').exists() == partial)
    if expected == 0:
        run('inspect-' + name, [ffprobe, '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', destination])
        streams = json.loads((output / ('inspect-' + name + '.stdout.log')).read_text())['streams']
        video = next(s for s in streams if s['codec_type'] == 'video')
        audio = next(s for s in streams if s['codec_type'] == 'audio')
        duration = 2 if name == 'healthy' else 8
        check(name + '-all-frames-audio-duration', int(video['nb_read_frames']) == duration * 30
              and abs(float(video['duration']) - duration) < .001 and abs(float(audio['duration']) - duration) < .03)

for mode in ['export-corrupt-output', 'export-cancel-verify', 'export-abort-boundary']:
    run(mode, [test, mode, short, output / (mode + '.mp4')])
run('export-worker-failure', [test, 'export-worker-failure', bad_short, output / 'worker-failure.mp4'])

raise SystemExit(0 if all(r['passed'] for r in records + assertions) else 1)
