"""Bounded regression and a real-product fullscreen memory containment check."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'field-2.0.1-20261002'
BUILD = BASE / 'build' / TASK
QT = Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('label')
    parser.add_argument('--app', type=Path, default=BASE / 'tests' / TASK / 'app')
    parser.add_argument('--memory', action='store_true')
    parser.add_argument('--fg', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch('[a-z0-9-]+', args.label):
        raise ValueError('Invalid evidence label')
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/field-2.0.1-control.py')], check=True)
    out = BASE / 'tests' / TASK / args.label
    tmp = BASE / 'tmp' / TASK / args.label
    out.mkdir(exist_ok=False)
    tmp.mkdir(exist_ok=False)
    env = os.environ.copy()
    env.update(TEMP=str(tmp), TMP=str(tmp), QML_DISABLE_DISK_CACHE='1',
               PATH=str(QT / 'bin') + os.pathsep + env['PATH'])
    results = []
    if not args.memory:
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   QT_QUICK_CONTROLS_STYLE='Basic', QT_QPA_FONTDIR='C:/Windows/Fonts',
                   QT_FORCE_STDERR_LOGGING='1',
                   VEYRA_EXPORT_TEST_ARTIFACTS=str(out / 'qml-images'))
        (out / 'qml-images').mkdir()
        shutil.copytree(ROOT / 'tests/qml/quick', args.app / 'qml-tests', dirs_exist_ok=True)
        for name in ('Qt6Test.dll', 'Qt6QuickTest.dll'):
            shutil.copy2(QT / 'bin' / name, args.app / name)
        for name in ('repair_contract', 'xbox', 'qml_data', 'ui_i18n', 'qml_quick'):
            exe = args.app / ('veyra_' + name + '_tests.exe')
            shutil.copy2(BUILD / exe.name, exe)
            parameters = [str(out / 'data')] if name == 'qml_data' else []
            with (out / (name + '.log')).open('xb') as log:
                completed = subprocess.run([str(exe), *parameters], cwd=args.app, env=env,
                                           stdout=log, stderr=subprocess.STDOUT, timeout=290)
            results.append({'name': name, 'exit': completed.returncode,
                            'exeSha256': hashlib.sha256(exe.read_bytes()).hexdigest()})
            print(json.dumps(results[-1]), flush=True)
    else:
        # Inject retained GPU allocations only while the real UI is fullscreen.
        # This checks containment, never claims to reproduce the field allocation owner.
        snippet = out / 'memory.qml'
        snippet.write_text('''
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) dialogs.open("capture")
            if (ticks === 6) {
                const f = veyra.captureFormats.find(x => x.label.indexOf("2560 x 1440 @ 60") === 0 && x.label.indexOf("NV12") > 0)
                if (!f) { veyra.logUi("uitest", "UITEST_FAIL capture format unavailable"); return }
                veyra.captureFormatKey = f.id
                veyra.nrEnabled = true
                veyra.fgEnabled = FG_VALUE
                veyra.startCaptureSession(); dialogs.close()
            }
            if (ticks === 20) root.toggleFullscreen()
            if (ticks === 62) {
                if (root.fullscreen || veyra.fullscreenMemorySafe !== false || !veyra.hasSource || !veyra.nrEnabled || veyra.fgEnabled !== FG_VALUE) {
                    veyra.logUi("uitest", "UITEST_FAIL memory fallback did not preserve source/effects"); return
                }
                root.toggleFullscreen()
                if (root.fullscreen) { veyra.logUi("uitest", "UITEST_FAIL unsafe fullscreen re-entry accepted"); return }
                veyra.logUi("uitest", "UITEST_NOTE windowed source/effects preserved; unsafe fullscreen blocked")
            }
            if (ticks === 72) veyra.stopPlayback()
            if (ticks === 76) {
                veyra.logUi("uitest", "UITEST_DONE"); running = false; root.close()
            }
        }
    }
'''.replace('FG_VALUE', 'true' if args.fg else 'false'), encoding='utf8')
        env.update(VEYRA_TEST_VRAM_LEAK_MIB='64', VEYRA_TEST_VRAM_LEAK_FULLSCREEN_ONLY='1')
        completed = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/ui-check/ui-check.py'),
                                    str(args.app), str(out / 'run'), str(snippet)], cwd=ROOT, env=env, timeout=110)
        log = (out / 'run/app.log').read_text(encoding='utf8', errors='replace')
        event = re.search(r'(\S+).*sustained fullscreen growth (\d+) MiB', log)
        post = log[event.end():] if event else ''
        samples = [int(x) for x in re.findall(r'vramMiB=(\d+)', post)]
        frames = [int(x) for x in re.findall(r'completedReal=(\d+)', post)]
        ok = (completed.returncode == 0 and event and 'unsafe fullscreen blocked' in log and
              len(samples) >= 10 and max(samples[3:]) - min(samples[3:]) <= 128 and
              len(frames) >= 10 and frames[-1] > frames[0] + 300 and
              'rebuilding the' not in post and 'UITEST_FAIL' not in log)
        results.append({'name': 'fullscreen_memory_containment', 'fg': args.fg,
                        'exit': 0 if ok else 1, 'injectedAllocations': True,
                        'postFallbackMiB': [min(samples), max(samples)] if samples else [],
                        'postFallbackCompletedReal': [frames[0], frames[-1]] if frames else []})
        print(json.dumps(results[-1]), flush=True)
    (out / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf8')
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/field-2.0.1-control.py')], check=True)
    raise SystemExit(any(r['exit'] for r in results))


if __name__ == '__main__':
    main()
