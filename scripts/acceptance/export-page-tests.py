"""Bounded product export tests. All generated files stay in the task's E: tree."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TEST = BASE / 'tests/export-page-20261002'
LOG = BASE / 'logs/export-page-20261002'
APP = TEST / 'engine-app'
MEDIA = TEST / 'media'
BUILD = BASE / 'build/export-page-20261002'
TMP = BASE / 'tmp/export-page-20261002/tests'
for path in (APP, MEDIA, TMP):
    path.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(TEMP=str(TMP), TMP=str(TMP))

def run(args, name, expected=0):
    target = LOG / (name + '.log')
    if target.exists():
        raise RuntimeError('Refusing to overwrite evidence: ' + str(target))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with target.open('wb') as out:
        code = subprocess.run([str(x) for x in args], cwd=APP, env=env,
                              stdout=out, stderr=subprocess.STDOUT, timeout=290, startupinfo=startup).returncode
    print(name, code, target)
    if code != expected:
        print(target.read_text(encoding='utf-8', errors='replace')[-7000:])
        raise RuntimeError('Unexpected exit code')

def stage():
    # Dependency snapshot was audited by stage-ui-migration.ps1. Copy from our
    # own baseline, never into another agent's candidate or build directory.
    source = TEST / 'baseline-app'
    if not source.is_dir():
        source = BASE / 'test-packages/export-page-20261002'
    for dll in source.glob('*.dll'):
        shutil.copy2(dll, APP / dll.name)
    for folder in ('shaders', 'runtime'):
        if not (APP / folder).exists():
            shutil.copytree(source / folder, APP / folder)
    (APP / 'runtime_local').mkdir(exist_ok=True)
    (APP / 'logs').mkdir(exist_ok=True)
    shutil.copy2(BUILD / 'veyra_export_workflow_tests.exe', APP)

mode = sys.argv[1]
tag = sys.argv[2] if len(sys.argv) > 2 else '001'
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/acceptance/export-page-control.py')], check=True)
if mode == 'generate':
    subtitles = MEDIA / 'crossing.srt'
    subtitles.write_text('1\n00:00:00,200 --> 00:00:01,300\nCrosses the trim in point\n\n2\n00:00:01,500 --> 00:00:02,700\nSecond subtitle\n', encoding='utf-8')
    for container, codec in [('mkv', 'srt'), ('mp4', 'mov_text')]:
        run(['ffmpeg', '-v', 'warning', '-n', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=10:duration=3',
             '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=3',
             '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000:duration=3',
             '-i', subtitles, '-map', '0:v', '-map', '1:a', '-map', '2:a', '-map', '3:s', '-map', '3:s',
             '-c:v', 'libx264', '-threads', '2', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-c:s', codec,
             '-metadata:s:a:0', 'language=eng', '-metadata:s:a:0', 'title=English',
             '-metadata:s:a:1', 'language=jpn', '-metadata:s:a:1', 'title=Japanese',
             '-metadata:s:s:0', 'language=eng', '-metadata:s:s:1', 'language=zho',
             '-disposition:a:0', 'default', '-disposition:a:1', '0', '-disposition:s:1', 'forced',
             MEDIA / ('tracks.' + container)], 'fixture-' + container + '-' + tag)
elif mode == 'generate-extra':
    ass = MEDIA / 'styled.ass'
    ass.write_text('[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,20,&H0000FFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.20,0:00:02.70,Default,,0,0,0,,Styled {\\i1}subtitle{\\i0}\n', encoding='utf-8')
    run(['ffmpeg', '-v', 'warning', '-n', '-i', MEDIA / 'tracks.mkv', '-i', ass,
         '-map', '0:v', '-map', '0:a', '-map', '1:s', '-c', 'copy', '-attach', 'C:/Windows/Fonts/arial.ttf',
         '-metadata:s:t', 'mimetype=application/x-truetype-font', '-metadata:s:t', 'filename=Arial.ttf', MEDIA / 'font-ass.mkv'], 'fixture-font-' + tag)
    run(['ffmpeg', '-v', 'warning', '-n', '-i', MEDIA / 'tracks.mkv', '-f', 'lavfi', '-i',
         'aevalsrc=0.1*sin(2*PI*220*t)|0.1*sin(2*PI*330*t)|0.1*sin(2*PI*440*t)|0.1*sin(2*PI*60*t)|0.1*sin(2*PI*550*t)|0.1*sin(2*PI*660*t):s=48000:d=3:c=5.1',
         '-map', '0:v', '-map', '1:a', '-map', '0:a', '-map', '0:s', '-c', 'copy', '-c:a:0', 'flac',
         '-metadata:s:a:0', 'language=eng', '-metadata:s:a:0', 'title=Six discrete channels', MEDIA / 'six-channel.mkv'], 'fixture-six-' + tag)
    run(['ffmpeg', '-v', 'warning', '-n', '-i', MEDIA / 'tracks.mp4', '-map', '0:v', '-c', 'copy', MEDIA / 'silent.mp4'], 'fixture-silent-' + tag)
    sparse = MEDIA / 'sparse.srt'
    sparse.write_text('1\n00:00:00,200 --> 00:00:01,000\nOpening\n\n2\n00:00:29,000 --> 00:00:30,000\nA sparse subtitle\n', encoding='utf-8')
    run(['ffmpeg', '-v', 'warning', '-n', '-stream_loop', '9', '-i', MEDIA / 'tracks.mp4', '-i', sparse,
         '-map', '0:v', '-map', '0:a', '-map', '1:s', '-c', 'copy', '-c:s', 'srt', '-t', '30', MEDIA / 'sparse.mkv'], 'fixture-sparse-' + tag)
elif mode == 'regression':
    stage()
    for name in ('veyra_export_queue_tests.exe', 'veyra_qml_data_tests.exe', 'veyra_export_worker_failure_tests.exe'):
        shutil.copy2(BUILD / name, APP)
    run([APP / 'veyra_export_queue_tests.exe', TEST], 'queue-unit-' + tag)
    run([APP / 'veyra_qml_data_tests.exe', TEST / ('data-' + tag)], 'qml-data-' + tag)
    env['VEYRA_TEST_COLOR_CHAIN_DATA_ROOT'] = str(APP / 'runtime_local')
    run([APP / 'veyra_export_worker_failure_tests.exe', 'color-chain', MEDIA / 'tracks.mp4', TEST / ('color-' + tag)], 'color-chain-' + tag)
    qml_app = TEST / 'candidate-ui-001'
    shutil.copy2(BUILD / 'veyra_qml_easing_tests.exe', qml_app)
    env.update(QT_QPA_PLATFORM='offscreen', QT_QPA_FONTDIR='C:/Windows/Fonts')
    run([qml_app / 'veyra_qml_easing_tests.exe'], 'qml-easing-' + tag)
elif mode in ('qml', 'qml-full'):
    app = TEST / 'candidate-ui-001'
    if not app.is_dir():
        shutil.copytree(BASE / 'test-packages/export-page-20261002', app)
    for name in ('veyra_qml_quick_tests.exe', 'veyra_qml_ui.exe'):
        shutil.copy2(BUILD / name, app / name)
    shutil.copytree(ROOT / 'qml', app / 'qml', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'tests/qml/quick', app / 'qml-tests', dirs_exist_ok=True)
    artifacts = TEST / ('qml-' + tag)
    artifacts.mkdir(exist_ok=True)
    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic',
               QT_FORCE_STDERR_LOGGING='1', QT_QPA_FONTDIR='C:/Windows/Fonts',
               QT_SCALE_FACTOR=tag.split('-')[0], VEYRA_EXPORT_TEST_ARTIFACTS=str(artifacts))
    run([app / 'veyra_qml_quick_tests.exe', '-input', app / ('qml-tests/tst_export.qml' if mode == 'qml' else 'qml-tests'),
         '-o', str(LOG / ('qml-' + tag + '-results.log')) + ',txt'], 'qml-' + tag)
    print((LOG / ('qml-' + tag + '-results.log')).read_text(encoding='utf-8', errors='replace')[-2000:])
    for name in ('export-1280.png', 'export-720-settings.png'):
        pixels = (artifacts / name).read_bytes()
        assert pixels.startswith(b'\x89PNG\r\n\x1a\n') and len(pixels) > 1000
elif mode == 'ui':
    app = Path(os.environ.get('VEYRA_EXPORT_UI_APP', str(TEST / 'candidate-ui-001')))
    if not app.is_dir():
        shutil.copytree(BASE / 'test-packages/export-page-20261002', app)
    shutil.copy2(BUILD / 'veyra_qml_ui.exe', app)
    shutil.copytree(ROOT / 'qml', app / 'qml', dirs_exist_ok=True)
    artifacts = TEST / ('production-ui-' + tag)
    artifacts.mkdir(exist_ok=False)
    output = artifacts / 'outputs'
    output.mkdir()
    (artifacts / 'images').mkdir()
    (artifacts / 'cancel-images').mkdir()
    (MEDIA / 'broken.mkv').write_text('Intentionally invalid local test fixture', encoding='utf-8')
    main = app / 'qml/Veyra/Main.qml'
    original = main.read_bytes()
    source = original.decode('utf-8')
    loader = '\nLoader { source: ' + json.dumps((ROOT / 'scripts/acceptance/export-page-ui.qml').as_uri()) + '; onLoaded: { item.outputDirectory = ' + json.dumps(str(output)) + '; item.evidenceDirectory = ' + json.dumps(str(artifacts)) + ' } }\n'
    try:
        at = source.rfind('}')
        main.write_text(source[:at] + loader + source[at:], encoding='utf-8')
        env.update(QT_FORCE_STDERR_LOGGING='1')
        run([app / 'veyra_qml_ui.exe', '--page', 'exp', '--size', '1280x720', '--reduced-motion',
             '--export-out', output, '--data-dir', artifacts / 'profile', '--exit-after', '125000'], 'production-ui-' + tag)
    finally:
        main.write_bytes(original)
    result = (LOG / ('production-ui-' + tag + '.log')).read_text(encoding='utf-8', errors='replace')
    assert 'EXPORT_UI_PASS' in result and 'EXPORT_UI_FAIL' not in result, result[-4000:]
    notices = [line for line in result.splitlines() if '[export-notify]' in line]
    assert len(notices) == 3 and sum('systemSoundQueued=true' in line for line in notices) == 2, notices
    assert any('soundEnabled=false systemSoundQueued=false' in line for line in notices)
    assert sum('kind=image-batch' in line for line in notices) == 1
    prefs = json.loads((artifacts / 'profile/qml-preferences.v1.json').read_text(encoding='utf-8'))
    assert prefs['exportCompletionSound'] is False
    assert (artifacts / 'production-export.png').is_file()
elif mode == 'queue':
    stage()
    folder = TEST / ('queue-' + tag)
    run([APP / 'veyra_export_workflow_tests.exe', mode, MEDIA / 'tracks.mp4', folder], mode + '-' + tag)
    outputs = sorted(folder.glob('*.mp4'))
    assert len(outputs) == 4, outputs
    for index, path in enumerate(outputs):
        raw = subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', str(path)], env=env, timeout=30)
        (LOG / (mode + '-' + tag + '-probe-' + str(index) + '.json')).write_bytes(raw)
        streams = json.loads(raw)['streams']
        assert [s['codec_type'] for s in streams] == ['video', 'audio', 'audio', 'subtitle', 'subtitle']
        assert int(streams[0]['nb_read_frames']) == 30
    print('PASS actual queue outputs', len(outputs))
else:
    stage()
    container = 'mkv' if 'mkv' in mode else 'mp4'
    source = MEDIA / ('tracks.' + ('mp4' if 'convert' in mode and container == 'mkv' else 'mkv' if 'convert' in mode else container))
    if len(sys.argv) > 3:
        source = Path(sys.argv[3])
    output = TEST / (mode + '-' + tag + '.' + container)
    run([APP / 'veyra_export_workflow_tests.exe', mode, source, output], mode + '-' + tag)
    if mode == 'lifecycle-boundary':
        assert output.exists()
        raise SystemExit(0)
    if 'reject' in mode:
        assert not output.exists()
        raise SystemExit(0)
    raw = subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', str(output)], env=env, timeout=30)
    (LOG / (mode + '-' + tag + '-probe.json')).write_bytes(raw)
    data = json.loads(raw)
    video = [s for s in data['streams'] if s['codec_type'] == 'video']
    audio = [s for s in data['streams'] if s['codec_type'] == 'audio']
    subs = [s for s in data['streams'] if s['codec_type'] == 'subtitle']
    assert len(video) == 1
    if mode == 'lifecycle':
        assert int(video[0]['nb_read_frames']) == 20
    else:
        source_data = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(source)], env=env, timeout=30))
        source_audio = [s for s in source_data['streams'] if s['codec_type'] == 'audio']
        source_subs = [s for s in source_data['streams'] if s['codec_type'] == 'subtitle']
        if 'none' in mode:
            source_audio = source_subs = []
        elif 'skip' in mode:
            # Bitmap subtitles the container/trim cannot carry are left out.
            source_subs = [s for s in source_subs if s.get('codec_name') != 'hdmv_pgs_subtitle']
        elif 'selected' in mode:
            source_audio = source_audio[1:2]
            source_subs = source_subs[1:2]
        assert len(audio) == len(source_audio) and len(subs) == len(source_subs), data
        for original, copied in zip(source_audio, audio):
            for key in ('codec_name', 'channels', 'channel_layout'):
                assert copied.get(key) == original.get(key), (key, original, copied)
            assert copied.get('tags', {}).get('language') == original.get('tags', {}).get('language')
        for original, copied in zip(source_subs, subs):
            assert copied['disposition']['forced'] == original['disposition']['forced']
        # A muxed track header alone does not prove that subtitle content survived.
        if subs:
            def subtitle_packets(path):
                return json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 's',
                    '-show_packets', '-show_data_hash', 'sha256', '-show_entries', 'packet=stream_index,pts_time,duration_time,data_hash',
                    '-of', 'json', str(path)], env=env, timeout=30)).get('packets', [])
            original_packets, copied_packets = subtitle_packets(source), subtitle_packets(output)
            for original, copied in zip(source_subs, subs):
                first = [p for p in original_packets if p['stream_index'] == original['index']]
                second = [p for p in copied_packets if p['stream_index'] == copied['index']]
                if first:
                    assert second, ('Source subtitle packets vanished', original)
                if original['codec_name'] == copied['codec_name'] and 'trim' not in mode:
                    assert [p['data_hash'] for p in first] == [p['data_hash'] for p in second], ('Subtitle payload changed', original)
            (LOG / (mode + '-' + tag + '-subtitle-packets.json')).write_text(json.dumps({'source': original_packets, 'output': copied_packets}, indent=2), encoding='utf-8')
    run(['ffmpeg', '-v', 'error', '-i', output, '-map', '0:v', '-map', '0:a?', '-f', 'null', '-'], mode + '-' + tag + '-decode')
    print('PASS', output)
