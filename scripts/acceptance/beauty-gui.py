"""Beauty GUI acceptance: real QML group, bridge, engine and live player on a face clip.

usage: py -3.11 -I scripts/acceptance/beauty-gui.py <staged-app-name> [task]

Runs a private copy of the staged app (tests/claude-beauty-20261017/<name>, with the
face model in its models/ folder) with an isolated profile, injects beauty-gui.qml
into that copy's Main.qml (restored afterwards), plays a clip with a face, and
checks the console and engine log: the node, the shared face tracker and the face
mesh are created, live values never rebuild them, and the log carries no face positions. Other Veyra
processes are left alone; nothing outside the task folders is written.
"""
from pathlib import Path
import json, os, re, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = sys.argv[2] if len(sys.argv) > 2 else 'claude-beauty-20261017'
APP = BASE / 'tests' / TASK / sys.argv[1]
MEDIA = BASE / 'tests' / TASK / 'media' / 'beauty-player-720p.mp4'
LOG = BASE / 'logs' / TASK / ('gui-' + time.strftime('%Y%m%d-%H%M%S')); LOG.mkdir(parents=True)
profile = BASE / 'tests' / TASK / 'profiles' / LOG.name; profile.mkdir(parents=True)
(profile / 'qml-preferences.v1.json').write_text(json.dumps({'language': 'zh-CN', 'autoResume': False, 'rememberPosition': False,
    'prewarmEnhancement': False, 'gpuPriority': 'normal', 'subtitleEnabled': False}), encoding='utf8')
tmp = BASE / 'tmp' / TASK / LOG.name; tmp.mkdir(parents=True)
assert MEDIA.is_file(), MEDIA
assert any((APP / 'models').glob('face_detection_yunet_*.onnx')), 'stage the face model into ' + str(APP / 'models')
main = APP / 'qml/Veyra/Main.qml'; saved = main.read_bytes()
shot = LOG / 'pro-quality-beauty.png'
loader = ('\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/beauty-gui.qml').as_uri()) +
          '; onLoaded: { item.media = ' + json.dumps(MEDIA.as_posix()) + '; item.shotPath = ' + json.dumps(shot.as_posix()) + ' } }\n')
text = saved.decode('utf8'); at = text.rfind('}'); assert at > 0
main.write_bytes((text[:at] + loader + text[at:]).encode('utf8'))
try:
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_'))}
    env.update(PATH=str(APP) + os.pathsep + env['PATH'], TEMP=str(tmp), TMP=str(tmp), CUDA_CACHE_PATH=str(tmp / 'cuda'),
               QML_DISK_CACHE_PATH=str(tmp / 'qml'), QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(LOG / 'engine.log'))
    command = [str(APP / 'veyra_qml_ui.exe'), '--data-dir', str(profile), '--page', 'pro', '--size', '1600x960', '--exit-after', '75000']
    start = time.monotonic()
    with (LOG / 'console.log').open('wb') as stream:
        run = subprocess.run(command, cwd=APP, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=90)
finally:
    main.write_bytes(saved)
console = (LOG / 'console.log').read_text(encoding='utf8', errors='replace')
engine = (LOG / 'engine.log').read_text(encoding='utf8', errors='replace') if (LOG / 'engine.log').exists() else ''
errors = [l for l in console.splitlines() if re.search(r'BEAUTY_UI_FAIL|ReferenceError:|TypeError:|\[ERROR\s*\]|\[FATAL\s*\]|D3D12 ERROR|Cannot assign|Binding loop', l)]


def between(text, begin, end):
    return text.split(begin, 1)[-1].split(end, 1)[0] if begin in text else ''


edits = between(engine, 'BEGIN beauty edits', 'END beauty edits')
live = between(engine, 'BEGIN live beauty values', 'END live beauty values')
created = re.findall(r'\[beauty\] instance=\d+ [^\r\n]*', edits)
tracker = re.findall(r'face tracker ready[^\r\n]*', edits)
rebuiltLive = re.findall(r'\[beauty\] instance=\d+ [^\r\n]*', live)
detectErrors = [l for l in engine.splitlines() if 'beauty-detect' in l and re.search(r'unavailable|failed|could not', l)]
# AGENTS.md: the log never records face crops, landmark tracks or pixels.
positions = [l for l in engine.splitlines() if 'beauty' in l and re.search(r'landmarks?\s*[=:]|\bbox\s*[=:]|\bcx=|\bcy=|score=|points?\s*[=:]', l)]
if not created: errors.append('the live player never created a beauty node')
if not tracker: errors.append('the shared face tracker never started')
if 'mesh=ready' not in edits: errors.append('the face mesh (contour, eyes, brows, lips) was not active')
if rebuiltLive: errors.append('live beauty values rebuilt the node: ' + rebuiltLive[0])
if detectErrors: errors.append('face detection reported a problem: ' + detectErrors[0])
if positions: errors.append('the log records face positions: ' + positions[0])
info = [l for l in console.splitlines() if 'BEAUTY_UI_INFO' in l]
row = {'command': command, 'exit': run.returncode, 'seconds': round(time.monotonic() - start, 1),
       'passed': run.returncode == 0 and 'BEAUTY_UI_PASS' in console and not errors, 'errors': errors,
       'checks': len(re.findall(r'BEAUTY_UI_CHECK', console)), 'nodesCreated': created, 'tracker': tracker[:1],
       'info': info, 'screenshot': str(shot) if shot.exists() else None, 'mainRestored': main.read_bytes() == saved,
       'profile': str(profile)}
(LOG / 'receipt.json').write_text(json.dumps(row, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(row, ensure_ascii=False, indent=1))
raise SystemExit(not (row['passed'] and row['mainRestored']))
