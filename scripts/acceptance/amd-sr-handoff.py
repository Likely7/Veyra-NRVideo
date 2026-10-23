"""AMD SR toggle reproduction on this PC's AMD Radeon iGPU (the engine adapter is
chosen with VEYRA_COMPUTE_ADAPTER_LUID, so the real non-NVIDIA paths run).

  before: the released 2.1.0 AMD package
  after:  the same package with this task's veyra_qml_ui.exe

Each app is a hard-linked copy of the package; files this script changes are
replaced (unlink + new file), never written through a link.

usage: py -3.11 -X utf8 -I run_sr_toggle.py before|after
"""
from pathlib import Path
import json, os, re, shutil, subprocess, sys, time

B = Path('E:/项目/Veyra'); T = 'codex-claude-handoff-20261022'
PKG = B / 'releases/release-2.1.0-20261019/runtime-base/AMD'
HERE = Path(__file__).resolve().parent
os.environ['PATH'] = str(next((Path(os.environ['LOCALAPPDATA'])/'Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe').glob('*/bin'))) + os.pathsep + os.environ['PATH']
which = sys.argv[1]; assert which in ('before', 'after')
APP = B / 'tests' / T / ('app-' + which)
MEDIA = B / 'tests' / T / 'media' / 'sr-clip1080.mp4'
stamp = time.strftime('%H%M%S')
OUT = B / 'logs' / T / ('sr-toggle-' + which + '-' + stamp); OUT.mkdir(parents=True)
running = [l for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, encoding='mbcs', errors='replace').splitlines() if l.lower().startswith('"veyra')]
assert not running, 'another Veyra process is running: ' + repr(running)


def replace(path, data):
    if path.exists(): path.unlink()
    path.write_bytes(data)


if not APP.exists():
    for src in PKG.rglob('*'):
        dst = APP / src.relative_to(PKG)
        if src.is_dir(): dst.mkdir(parents=True, exist_ok=True)
        else: dst.parent.mkdir(parents=True, exist_ok=True); os.link(src, dst)
if which == 'after':
    replace(APP / 'veyra_qml_ui.exe', (B / 'build/claude-screen-recording-20261022/veyra_qml_ui.exe').read_bytes())
main = APP / 'qml/Veyra/Main.qml'
original = (HERE.parents[1] / 'qml/Veyra/Main.qml').read_text('utf8')
if which == 'after':
    for source in (HERE.parents[1]/'qml/Veyra').rglob('*'):
        if source.is_file():
            target=APP/'qml/Veyra'/source.relative_to(HERE.parents[1]/'qml/Veyra');target.parent.mkdir(parents=True,exist_ok=True);replace(target,source.read_bytes())
at = original.rfind('}')
replace(main, (original[:at] + '\nLoader { source: ' + json.dumps((HERE / 'amd-sr-handoff.qml').as_uri()) + ' }\n' + original[at:]).encode('utf8'))

if not MEDIA.exists():
    MEDIA.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=1920x1080:rate=30', '-t', '40',
                    '-c:v', 'libx264', '-preset', 'veryfast', '-pix_fmt', 'yuv420p', str(MEDIA)], check=True, timeout=120)

adapters = subprocess.check_output([sys.executable, '-X', 'utf8', '-I', str(HERE / 'dxgi-adapters-handoff.py')], text=True)
amd = [re.search(r'luid=([0-9A-F]{16})', l).group(1) for l in adapters.splitlines() if 'vendor=0x1002' in l]
assert amd, adapters
profile = B / 'tests' / T / 'profiles' / (which + '-' + stamp); profile.mkdir(parents=True)
(profile / 'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN', autoResume=False, prewarmEnhancement=False, monitorGpu='1002:13c0:88771043', computeOnMonitorGpu=True)), 'utf8')
tmp = B / 'tmp' / T / ('run-' + which + '-' + stamp); tmp.mkdir(parents=True)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(tmp), TMP=str(tmp), VEYRA_LOG_FILE=str(OUT / 'engine.log'), VEYRA_COMPUTE_ADAPTER_LUID=amd[0],
           QT_FORCE_STDERR_LOGGING='1', QML_DISABLE_DISK_CACHE='1')
env.update(APPDATA=str(profile/'AppData'),LOCALAPPDATA=str(profile/'LocalAppData'),CUDA_CACHE_PATH=str(tmp/'cuda'))
with (OUT / 'console.log').open('wb') as log:
    rc = subprocess.run([str(APP / 'veyra_qml_ui.exe'), '--page', 'pro', '--size', '1280x800', '--data-dir', str(profile),
                         '--exit-after', '85000', str(MEDIA)], cwd=APP, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=150).returncode
console = (OUT / 'console.log').read_text('utf8', errors='replace')
engine = (OUT / 'engine.log').read_text('utf8', errors='replace') if (OUT / 'engine.log').exists() else ''
print('exit', rc, 'adapter', amd[0], OUT)
for line in console.splitlines():
    if 'SRTEST' in line: print(' |', line[line.find('SRTEST'):][:400])
for line in engine.splitlines():
    if re.search(r'\[resolution\] source=|\[capability\]|Applied revision|device context initialized', line): print(' >', line[25:330])

assert rc==0 and "SRTEST DONE" in console and "SRTEST FAIL" not in console
if which=="after":
    assert re.search(r"SRTEST after-on srEnabled=true srActive=true",console), OUT
    assert re.search(r"SRTEST after-off srEnabled=false srActive=false",console), OUT
    graphs=re.findall(r"initialized src=1920x1080 work=(\d+x\d+) sr=(\d)",engine)
    assert ("3840x2160","1") in graphs and graphs[-1]==("1920x1080","0"),graphs
assert "[engine-hang]" not in engine
