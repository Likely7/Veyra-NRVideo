"""Anti-aliasing (#53) GUI acceptance: real QML card, bridge, engine and live player.

usage: py -3.11 -I scripts/acceptance/aa-53-gui.py <staged-app-name> [task-folder]

Runs a private copy of the staged app (tests/claude-aa-53-20261017/<name>) with an
isolated profile, injects aa-53-gui.qml into that copy's Main.qml (restored
afterwards), plays a synthetic clip, and checks the console and engine log. Other
Veyra processes are left alone; nothing outside the task folders is written.
"""
from pathlib import Path
import json, os, re, subprocess, sys, time

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = sys.argv[2] if len(sys.argv) > 2 else 'claude-aa-53-20261017'
APP = BASE / 'tests' / TASK / sys.argv[1]
MEDIA = BASE / 'tests' / TASK / 'media' / 'aa-player-720p.mp4'
LOG = BASE / 'logs' / TASK / ('gui-' + time.strftime('%Y%m%d-%H%M%S')); LOG.mkdir(parents=True)
profile = BASE / 'tests' / TASK / 'profiles' / LOG.name; profile.mkdir(parents=True)
(profile / 'qml-preferences.v1.json').write_text(json.dumps({'language': 'zh-CN', 'autoResume': False, 'rememberPosition': False,
    'prewarmEnhancement': False, 'gpuPriority': 'normal', 'subtitleEnabled': False}), encoding='utf8')
tmp = BASE / 'tmp' / TASK / LOG.name; tmp.mkdir(parents=True)
assert MEDIA.is_file(), MEDIA
main = APP / 'qml/Veyra/Main.qml'; saved = main.read_bytes()
shot = LOG / 'pro-quality-anti-alias.png'
loader = ('\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/aa-53-gui.qml').as_uri()) +
          '; onLoaded: { item.media = ' + json.dumps(MEDIA.as_posix()) + '; item.shotPath = ' + json.dumps(shot.as_posix()) + ' } }\n')
text = saved.decode('utf8'); at = text.rfind('}'); assert at > 0
main.write_bytes((text[:at] + loader + text[at:]).encode('utf8'))
try:
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_'))}
    env.update(PATH=str(APP) + os.pathsep + env['PATH'], TEMP=str(tmp), TMP=str(tmp), CUDA_CACHE_PATH=str(tmp / 'cuda'),
               QML_DISK_CACHE_PATH=str(tmp / 'qml'), QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(LOG / 'engine.log'))
    command = [str(APP / 'veyra_qml_ui.exe'), '--data-dir', str(profile), '--page', 'pro', '--size', '1600x960', '--exit-after', '58000']
    start = time.monotonic()
    with (LOG / 'console.log').open('wb') as stream:
        run = subprocess.run(command, cwd=APP, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=70)
finally:
    main.write_bytes(saved)
console = (LOG / 'console.log').read_text(encoding='utf8', errors='replace')
engine = (LOG / 'engine.log').read_text(encoding='utf8', errors='replace') if (LOG / 'engine.log').exists() else ''
errors = [l for l in console.splitlines() if re.search(r'AA_UI_FAIL|ReferenceError:|TypeError:|\[ERROR\s*\]|\[FATAL\s*\]|D3D12 ERROR|Cannot assign|Binding loop', l)]
middle = engine.split('BEGIN anti-alias edits', 1)[-1].split('END anti-alias edits', 1)[0] if 'BEGIN anti-alias edits' in engine else ''
created = re.findall(r'\[anti-alias\] instance=\d+ [^\r\n]*', middle)
recorded = len(re.findall(r'recorded source-size aa', middle))
if not created: errors.append('the live player never created an anti-aliasing pass')
if not recorded: errors.append('the live player never recorded the source-size anti-aliasing pass')
row = {'command': command, 'exit': run.returncode, 'seconds': round(time.monotonic() - start, 1),
       'passed': run.returncode == 0 and 'AA_UI_PASS' in console and not errors, 'errors': errors,
       'checks': len(re.findall(r'AA_UI_CHECK', console)), 'passesCreated': created, 'recordedSourceSize': recorded,
       'screenshot': str(shot) if shot.exists() else None, 'mainRestored': main.read_bytes() == saved, 'profile': str(profile)}
(LOG / 'receipt.json').write_text(json.dumps(row, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(row, ensure_ascii=False, indent=1))
raise SystemExit(not (row['passed'] and row['mainRestored']))
