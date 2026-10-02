"""Run bounded tests from this branch; keep inputs, profiles and evidence isolated."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'release-2.0.0-20261002'
BUILD = BASE / 'build' / TASK


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('label')
    parser.add_argument('--app', type=Path, default=BASE / 'tests' / TASK / 'baseline-app')
    parser.add_argument('--only', nargs='+')
    parser.add_argument('--export', action='store_true')
    args = parser.parse_args()
    if not args.label.replace('-', '').isalnum():
        raise ValueError('Invalid evidence label')
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/release-2.0.0-control.py')], check=True)
    out = BASE / 'tests' / TASK / args.label
    out.mkdir(exist_ok=False)
    tmp = BASE / 'tmp' / TASK / args.label
    tmp.mkdir(exist_ok=False)
    env = os.environ.copy()
    env.update(TEMP=str(tmp), TMP=str(tmp), QT_QPA_PLATFORM='offscreen',
               QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic',
               QT_FORCE_STDERR_LOGGING='1', QML_DISABLE_DISK_CACHE='1',
               QT_QPA_FONTDIR='C:/Windows/Fonts', VEYRA_EXPORT_TEST_ARTIFACTS=str(out / 'qml-images'))
    (out / 'qml-images').mkdir()
    (out / 'queue').mkdir()
    tests = [(name, []) for name in (
        'live_timing', 'realtime_preview', 'hdr_display_state',
        'capture_format_selection', 'capture_color', 'presentation_worker',
        'iec61937_probe', 'repair_contract', 'unified', 'ui_i18n',
        'moonlight_protocol', 'moonlight_model', 'qml_easing', 'xbox')]
    tests += [('qml_data', [out / 'data']), ('qml_quick', ['-input', args.app / 'qml-tests']),
              ('export_queue', [out / 'queue']), ('preview_geometry', [out / 'geometry']),
              ('audio_timeline', [out / 'audio'])]
    if args.export:
        media = BASE / 'tests/export-page-20261002/media'
        tests = []
        for mode in ('mp4', 'mkv', 'mkv-trim', 'mp4-selected', 'mp4-none',
                     'mp4-pgs-skip', 'mp4-pgs-reject', 'lifecycle', 'lifecycle-boundary', 'queue'):
            source = media / ('pgs-plain.mkv' if 'pgs' in mode else 'tracks.mkv' if 'mkv' in mode else 'tracks.mp4')
            target = out / ('queue-outputs' if mode == 'queue' else mode + ('.mkv' if 'mkv' in mode else '.mp4'))
            tests.append((mode, [mode, source, target]))
    results = []
    if args.only:
        known = {name for name, _ in tests}
        if set(args.only) - known:
            raise ValueError('Unknown test names')
        tests = [(name, params) for name, params in tests if name in args.only]
    for name, params in tests:
        exe = 'veyra_export_workflow_tests.exe' if args.export else f'veyra_{name}_tests.exe'
        shutil.copy2(BUILD / exe, args.app / exe)
        log = out / (name + '.log')
        started = time.monotonic()
        with log.open('xb') as stream:
            process = subprocess.Popen([str(args.app / exe), *map(str, params)], cwd=args.app,
                                       env=env, stdout=stream, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=290)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                process.wait(timeout=10)
                code = 124
        result = dict(name=name, exit=code, seconds=round(time.monotonic() - started, 2),
                      executableSha256=hashlib.sha256((args.app / exe).read_bytes()).hexdigest(),
                      log=str(log), status='pass' if code == 0 else 'fail')
        results.append(result)
        if args.export and code == 0 and name not in ('queue', 'mp4-pgs-reject'):
            target = params[-1]
            probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames',
                               '-show_streams', '-of', 'json', str(target)], env=env, timeout=30))
            (out / (name + '-probe.json')).write_text(json.dumps(probe, indent=2), encoding='utf8')
            video = [s for s in probe['streams'] if s['codec_type'] == 'video']
            audio = [s for s in probe['streams'] if s['codec_type'] == 'audio']
            subs = [s for s in probe['streams'] if s['codec_type'] == 'subtitle']
            if len(video) != 1 or int(video[0]['nb_read_frames']) <= 0:
                result.update(exit=1, status='fail', reason='No decoded output frames')
            if name in ('mp4', 'mkv', 'mkv-trim', 'mp4-selected', 'mp4-none'):
                expected = 1 if 'selected' in name else 0 if 'none' in name else 2
                if len(audio) != expected or len(subs) != expected:
                    result.update(exit=1, status='fail', reason='Wrong audio/subtitle track count')
        print(json.dumps(result), flush=True)
    (out / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf8')
    raise SystemExit(any(r['exit'] != 0 for r in results))


if __name__ == '__main__':
    main()
