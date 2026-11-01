"""Real recording regression: the production bridge records the presented picture.

Copies the built app and this task's QML into a private directory, opens a
media file, starts the recorder through the real bridge, lets it record a few
seconds, stops it and inspects the file the engine wrote.

usage: screen-recording-control.py
"""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'screen-recording-20261020'
BUILD = BASE / 'build' / TASK
OUT = BASE / 'tests' / TASK / 'recording'
TMP = BASE / 'tmp' / TASK / 'recording'
LOGS = BASE / 'logs' / TASK
for path in (OUT, TMP, LOGS):
    path.mkdir(parents=True, exist_ok=True)
# The packaged test build carries the Qt runtime and the runtime components;
# this test only drives it.
DONOR = BASE / 'test-packages/screen-recording-20261020/Veyra-2.1.0-rec-NVIDIA-win64-portable'
app = OUT / 'app'
if app.exists():
    shutil.rmtree(app)
shutil.copytree(DONOR, app)
assert (app / 'veyra_qml_ui.exe').exists()

media = OUT / 'clip.mkv'
recordings = OUT / 'recordings'
recordings.mkdir(exist_ok=True)

env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_'))}
env.update(TEMP=str(TMP), TMP=str(TMP), VEYRA_LOG_FILE=str(LOGS / 'recording-engine.log'),
           QT_FORCE_STDERR_LOGGING='1', QML_DISABLE_DISK_CACHE='1')

# A short synthetic clip: no user media is copied or touched.
with (LOGS / 'fixture.log').open('wb') as log:
    ffmpeg = os.environ.get('FFMPEG', 'ffmpeg')
    try:
        subprocess.run([ffmpeg, '-v', 'warning', '-y', '-f', 'lavfi', '-i',
                        'testsrc2=size=1280x720:rate=30', '-t', '20',
                        '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p', str(media)],
                       env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print('fixture failed:', error)
        print((LOGS / 'fixture.log').read_text('utf8', errors='replace')[-2000:])
        raise SystemExit(2)
assert media.exists() and media.stat().st_size > 0

# The control QML is injected into this staging copy of Main.qml only.
main = app / 'qml/Veyra/Main.qml'
original = main.read_bytes()
source = original.decode('utf8')
loader = ('\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/screen-recording-ui.qml').as_uri())
          + '; onLoaded: { item.media=' + json.dumps(str(media))
          + '; item.evidence=' + json.dumps(str(OUT)) + ' } }\n')
at = source.rfind('}')
main.write_text(source[:at] + loader + source[at:], encoding='utf8')

args = [str(app / 'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1280x800',
        '--data-dir', str(OUT / 'profile'), '--exit-after', '120000', str(media)]
console = LOGS / 'recording-console.log'
try:
    with console.open('wb') as log:
        code = subprocess.run(args, cwd=app, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=280).returncode
finally:
    main.write_bytes(original)

text = console.read_text('utf8', errors='replace')
print('exit', code)
for line in text.splitlines():
    if 'RECORD_' in line or '[ERROR]' in line:
        print(' |', line[:200])
assert 'RECORD_UI_PASS' in text, 'the recording control test did not pass'
assert 'RECORD_UI_FAIL' not in text

produced = sorted(recordings.glob('*.mp4'))
assert produced, 'no mp4 was written into the recording folder'
report = []
for file in produced:
    size = file.stat().st_size
    assert size > 1024, f'{file} is too small to be a real recording: {size}'
    probe = {}
    ffprobe = os.environ.get('FFPROBE', 'ffprobe')
    try:
        data = subprocess.check_output([ffprobe, '-v', 'error', '-count_frames', '-show_streams',
                                        '-show_format', '-of', 'json', str(file)], env=env, timeout=60)
        probe = json.loads(data)
    except (OSError, subprocess.CalledProcessError) as error:
        print('ffprobe unavailable:', error)
    report.append({'file': str(file), 'bytes': size, 'probe': probe})
    if probe:
        stream = next(x for x in probe['streams'] if x['codec_type'] == 'video')
        print('video:', stream.get('codec_name'), stream.get('width'), 'x', stream.get('height'),
              'frames', stream.get('nb_read_frames'), 'duration', probe['format'].get('duration'))

(LOGS / 'recording-files.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print('RECORDING CONTROL PASS', json.dumps([{'file': r['file'], 'bytes': r['bytes']} for r in report], ensure_ascii=False))
