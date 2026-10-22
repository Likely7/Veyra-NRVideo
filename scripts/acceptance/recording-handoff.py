"""Live recording acceptance on this PC (claude/screen-recording-20261022).

The app is a hard-linked copy of the released 2.1.0 NVIDIA package with this
branch's veyra_qml_ui.exe and QML copied over (copies replace links; nothing is
written through a link). A sync clip (white box + 1 kHz beep at every second)
is played; the recording is checked for tracks, length, A/V offset (flash vs
beep onset) and frame cadence.

usage: py -3.11 -X utf8 -I recording_test.py CASE [--media sync|4k] [--keep]
cases: basic settings resize pause fg mf amd source-check
"""
from pathlib import Path
import argparse, json, os, re, shutil, statistics, subprocess, sys, time
import numpy as np

os.environ['PATH'] = str(next((Path(os.environ['LOCALAPPDATA']) / 'Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe').glob('*/bin'))) + os.pathsep + os.environ['PATH']
B = Path('E:/项目/Veyra'); T = 'codex-claude-handoff-20261022'
ROOT = Path(__file__).resolve().parents[2]
PKG = B / 'test-packages/bilibili-2.1.0-20261019/Veyra-2.1.0-test-NVIDIA-win64-portable'
BUILD = B / 'build/claude-screen-recording-20261022'
HERE = Path(__file__).resolve().parent
APP = B / 'tests' / T / 'app'
MEDIA = B / 'tests' / T / 'media'
GTA4K = Path('E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4')

CASES = {
    'basic': dict(seconds=12),
    'muted': dict(seconds=8, muted=True),
    'baseline': dict(seconds=12, noRecord=True),
    'resize-baseline': dict(seconds=15, noRecord=True, actions=[{'at': 4, 'do': 'sr-on'}, {'at': 9, 'do': 'sr-off'}]),
    'resize-fsr': dict(seconds=15, fsr=True, actions=[{'at': 4, 'do': 'sr-on'}, {'at': 9, 'do': 'sr-off'}]),
    'rapid': dict(seconds=1, repeats=5),
    'bad-directory': dict(seconds=1, expectFailure=True),
    'long': dict(seconds=60),
    'silent': dict(seconds=8, audio=False),
    'hevc': dict(seconds=8, hevc=True),
    'limited': dict(seconds=8, fps=30),
    'stop': dict(seconds=12, actions=[{'at': 4, 'do': 'stop-playback'}], expectStop=True),
    'settings': dict(seconds=14, effects={'colour': False}, actions=[{'at': 4, 'do': 'colour-on'}, {'at': 8, 'do': 'colour-off'}]),
    'resize': dict(seconds=15, actions=[{'at': 4, 'do': 'sr-on'}, {'at': 9, 'do': 'sr-off'}]),
    'pause': dict(seconds=14, actions=[{'at': 4, 'do': 'pause'}, {'at': 8, 'do': 'resume'}]),
    'fg': dict(seconds=12, effects={'fg': 4}, settle=60),
    'mf': dict(seconds=12, env={'VEYRA_TEST_FORCE_MF_ENCODER': '1'}),
    'amd': dict(seconds=12, prefs={'monitorGpu': '1002:13c0:88771043', 'computeOnMonitorGpu': True}),
}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def replace(path, data):
    if path.exists(): path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_app():
    if not APP.exists():
        for src in PKG.rglob('*'):
            dst = APP / src.relative_to(PKG)
            if src.is_dir(): dst.mkdir(parents=True, exist_ok=True)
            else: dst.parent.mkdir(parents=True, exist_ok=True); os.link(src, dst)
    replace(APP / 'veyra_qml_ui.exe', (BUILD / 'veyra_qml_ui.exe').read_bytes())
    for src in (ROOT / 'qml/Veyra').rglob('*'):
        if src.is_file(): replace(APP / 'qml/Veyra' / src.relative_to(ROOT / 'qml/Veyra'), src.read_bytes())
    i18n = ROOT / 'i18n/catalog.json'
    for target in APP.rglob('catalog.json'):
        replace(target, i18n.read_bytes())


def make_sync_clip():
    clip = MEDIA / 'sync1080p60-wideflash.mp4'
    if clip.exists(): return clip
    MEDIA.mkdir(parents=True, exist_ok=True)
    video = "testsrc2=size=1920x1080:rate=60,drawbox=x=0:y=0:w=240:h=240:color=white:t=fill:enable='lt(mod(n\\,60)\\,3)'"
    # Sample-accurate gating (the volume filter's enable switches per 1024-sample frame).
    audio = "aevalsrc='if(lt(mod(t\\,1)\\,0.05)\\,0.25*sin(2*PI*1000*t)\\,0)':s=48000:c=stereo"
    r = run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', video, '-f', 'lavfi', '-i', audio, '-t', '40',
             '-c:v', 'libx264', '-preset', 'veryfast', '-pix_fmt', 'yuv420p', '-g', '60', '-c:a', 'aac', '-b:a', '192k',
             '-shortest', str(clip)], timeout=240)
    assert r.returncode == 0, r.stderr.decode(errors='replace')[-2000:]
    return clip


def probe(path):
    r = run(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)], timeout=60)
    return json.loads(r.stdout or b'{}')


def frame_times(path):
    r = run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'frame=pts_time', '-of', 'csv=p=0', str(path)], timeout=240)
    fields=[line.split(',')[0].strip() for line in r.stdout.decode().splitlines()]
    return [float(x) for x in fields if x and x != 'N/A']


def flashes(path, times):
    r = run(['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:v:0', '-vf', 'crop=iw*0.1:ih*0.18:0:0,scale=4:4,format=gray',
             '-fps_mode', 'passthrough', '-f', 'rawvideo', '-'], timeout=300)
    lum = np.frombuffer(r.stdout, np.uint8).reshape(-1, 16).mean(axis=1)
    n = min(len(lum), len(times))
    bright = lum[:n] > 200
    out = [times[i] for i in range(n) if bright[i] and (i == 0 or not bright[i - 1])]
    return out


def beeps(path):
    r = run(['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:a:0', '-ac', '1', '-ar', '48000', '-f', 's16le', '-'], timeout=300)
    pcm = np.frombuffer(r.stdout, np.int16).astype(np.float32)
    if not len(pcm): return [], 0.0
    win = 48  # 1 ms
    env = np.abs(pcm[: len(pcm) // win * win]).reshape(-1, win).max(axis=1)
    on = env > 2000
    out, quiet = [], 1000
    for i, v in enumerate(on):
        if v and quiet >= 200: out.append(i / 1000.0)
        quiet = 0 if v else quiet + 1
    return out, len(pcm) / 48000.0


def analyze(path, label):
    info = probe(path)
    streams = info.get('streams', [])
    v = next((s for s in streams if s.get('codec_type') == 'video'), {})
    a = next((s for s in streams if s.get('codec_type') == 'audio'), None)
    times = frame_times(path)
    deltas = [b - a_ for a_, b in zip(times, times[1:])]
    flash = flashes(path, times) if times else []
    beep, audio_seconds = beeps(path) if a else ([], 0.0)
    offsets = []
    for f in flash:
        near = min(beep, key=lambda b: abs(b - f), default=None)
        if near is not None and abs(near - f) < 0.4: offsets.append(round((near - f) * 1000, 1))
    med = statistics.median(deltas) if deltas else 0
    result = dict(label=label, file=str(path), bytes=path.stat().st_size, codec=v.get('codec_name'), width=v.get('width'),
                  height=v.get('height'), pix=v.get('pix_fmt'), frames=len(times),
                  duration=float(info.get('format', {}).get('duration', 0) or 0),
                  video_span=round(times[-1] - times[0], 3) if times else 0, audio=a.get('codec_name') if a else None,
                  audio_seconds=round(audio_seconds, 3), flashes=len(flash), beeps=len(beep),
                  av_offset_ms=dict(n=len(offsets), median=statistics.median(offsets) if offsets else None,
                                    min=min(offsets) if offsets else None, max=max(offsets) if offsets else None),
                  cadence_ms=dict(median=round(med * 1000, 2), p99=round(float(np.percentile(deltas, 99)) * 1000, 2) if deltas else 0,
                                  max=round(max(deltas) * 1000, 2) if deltas else 0,
                                  over_1_5x=sum(1 for d in deltas if med and d > med * 1.5)))
    return result


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('case'); ap.add_argument('--media', default='sync'); a = ap.parse_args()
    if a.case == 'source-check':
        print(json.dumps(analyze(make_sync_clip(), 'source'), ensure_ascii=False, indent=1)); return
    case = CASES[a.case]
    running = [l for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, encoding='mbcs', errors='replace').splitlines() if l.lower().startswith('"veyra')]
    assert not running, 'another Veyra process is running: ' + repr(running)
    make_app()
    media = make_sync_clip() if a.media == 'sync' else GTA4K
    if a.case == 'long':
        extended = MEDIA / 'sync1080p60-wideflash-120s.mp4'
        if not extended.exists():
            r = run(['ffmpeg', '-v', 'error', '-y', '-stream_loop', '2', '-i', str(media), '-c', 'copy', str(extended)], timeout=60)
            assert r.returncode == 0, r.stderr
        media = extended
    stamp = time.strftime('%H%M%S')
    out = B / 'logs' / T / ('rec-' + a.case + '-' + stamp); out.mkdir(parents=True)
    rec_dir = B / 'tests' / T / 'recordings' / (a.case + '-' + stamp); rec_dir.mkdir(parents=True)
    profile = B / 'tests' / T / 'profiles' / (a.case + '-' + stamp); profile.mkdir(parents=True)
    prefs = dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)
    prefs.update(case.get('prefs', {}))
    (profile / 'qml-preferences.v1.json').write_text(json.dumps(prefs), 'utf8')
    cfg = dict(case); cfg.pop('env', None); cfg.pop('prefs', None); cfg['dir'] = str(rec_dir)
    if a.case == 'bad-directory':
        blocked = rec_dir / 'not-a-directory'; blocked.write_text('intentional test file', 'utf8'); cfg['dir'] = str(blocked)
    main_qml = APP / 'qml/Veyra/Main.qml'
    source = (ROOT / 'qml/Veyra/Main.qml').read_text('utf8')
    at = source.rfind('}')
    loader = '\nLoader { source: ' + json.dumps((HERE / 'recording-handoff.qml').as_uri()) + '; onLoaded: item.config = ' + json.dumps(json.dumps(cfg)) + ' }\n'
    replace(main_qml, (source[:at] + loader + source[at:]).encode('utf8'))
    tmp = B / 'tmp' / T / ('run-' + a.case + '-' + stamp); tmp.mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
    env.update(TEMP=str(tmp), TMP=str(tmp), VEYRA_LOG_FILE=str(out / 'engine.log'), QT_FORCE_STDERR_LOGGING='1', QML_DISABLE_DISK_CACHE='1')
    env.update(APPDATA=str(profile/'AppData'), LOCALAPPDATA=str(profile/'LocalAppData'), CUDA_CACHE_PATH=str(tmp/'cuda'), QML_DISK_CACHE_PATH=str(tmp/'qml'))
    env.update(case.get('env', {}))
    started = time.monotonic()
    with (out / 'console.log').open('wb') as log:
        rc = subprocess.run([str(APP / 'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1280x800', '--data-dir', str(profile),
                             '--exit-after', '180000', str(media)], cwd=APP, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240).returncode
    console = (out / 'console.log').read_text('utf8', errors='replace')
    engine = (out / 'engine.log').read_text('utf8', errors='replace') if (out / 'engine.log').exists() else ''
    lines = [l[l.find('RECTEST'):] for l in console.splitlines() if 'RECTEST' in l]
    recorder = [l[25:400] for l in engine.splitlines() if 'live-recorder' in l or 'nvenc-live' in l or ('mf-encoder' in l and 'ERROR' in l)]
    files = sorted(rec_dir.glob('*.mp4'))
    report = dict(head=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(), exe_sha256=__import__('hashlib').sha256((APP/'veyra_qml_ui.exe').read_bytes()).hexdigest(), case=a.case, exit=rc, seconds=round(time.monotonic() - started, 1), ui=lines, recorder=recorder[:60],
                  files=[analyze(f, f.name) for f in files])
    (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), 'utf8')
    print(json.dumps({k: v for k,v in report.items() if k != 'recorder'}, ensure_ascii=False, indent=1)[:6000])
    assert rc == 0 and 'RECTEST FAIL' not in console and any('RECTEST FINAL' in l for l in lines), out
    if not case.get('noRecord') and not case.get('expectFailure'):
        assert report['files'] and all(f['frames'] > 0 for f in report['files']), out
        if case.get('audio', True): assert all(f['audio'] == 'aac' for f in report['files']), out
        if a.case in ('basic', 'long', 'hevc', 'limited', 'mf', 'amd', 'muted'):
            for f in report['files']:
                offset = f['av_offset_ms']['median']
                assert offset is not None and abs(offset) <= 50, ('A/V gate', offset, out)
        assert not any('failed=true' in l or '\"failed\":true' in l for l in lines), out
    errors = [l for l in engine.splitlines() if '[ERROR]' in l and '[qml] Retrying to obtain clipboard.' not in l]
    assert '[engine-hang]' not in engine and not errors, ('engine error', errors[:3], out)
    if a.case in ('resize', 'resize-fsr'): assert len(report['files']) >= 3, ('resize did not create new segments', out)
    print('LOG', out)


if __name__ == '__main__':
    main()
