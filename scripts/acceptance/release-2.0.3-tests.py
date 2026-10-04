"""Run bounded regressions using the actual vendor payload in owned test copies."""
import json, os, shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra'); TASK = 'release-2.0.3-20261004'
BUILD = BASE / 'build' / TASK; TESTS = BASE / 'tests' / TASK
QT = Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
label = sys.argv[1]; flavor = sys.argv[2]
assert label.replace('-', '').isalnum() and flavor in ('NVIDIA', 'AMD')
out = TESTS / label; out.mkdir(parents=True, exist_ok=False)
logs = BASE / 'logs' / TASK / label; logs.mkdir(parents=True, exist_ok=False)
tmp = BASE / 'tmp' / TASK / label; tmp.mkdir(parents=True, exist_ok=False)
app = TESTS / ('app-' + flavor + ('-shared-fsr' if flavor == 'NVIDIA' else ''))
if not app.exists():
    stage = BASE / 'releases' / TASK / ('Veyra-2.0.3-' + flavor + '-win64-portable')
    manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
    for row in manifest['files']:
        source = stage / row['path']; target = app / row['path']; target.parent.mkdir(parents=True, exist_ok=True)
        # Mutable test/QML/manifest files are private; immutable runtime objects
        # may be hardlinked so test profiles cannot alter the release stage.
        if source.suffix.lower() in {'.exe', '.qml', '.js', '.json', '.txt', '.md'}: shutil.copyfile(source, target)
        else: target.hardlink_to(source)
shutil.copyfile(BUILD / 'veyra_qml_ui.exe', app / 'veyra_qml_ui.exe')
shutil.copytree(ROOT / 'qml/Veyra', app / 'qml/Veyra', dirs_exist_ok=True)
for name in ('veyra_effect_availability_tests', 'veyra_vfg_settings_tests', 'veyra_fsr_dispatch_tests',
             'veyra_fsr_switch_tests', 'veyra_xbox_tests', 'veyra_capture_audio_tests', 'veyra_qml_quick_tests',
             'veyra_lmxxf_nr_tests', 'veyra_ui_i18n_tests', 'veyra_preset_library_tests', 'veyra_export_probe',
             'veyra_vfg_gpu_tests', 'veyra_vfg_export_probe'):
    shutil.copyfile(BUILD / (name + '.exe'), app / (name + '.exe'))
shutil.copytree(BUILD / 'lmxxf-test-runtime', app / 'lmxxf-test-runtime', dirs_exist_ok=True)
shutil.copytree(ROOT / 'tests/qml/quick', app / 'qml-tests', dirs_exist_ok=True)
for name in ('Qt6Test.dll', 'Qt6QuickTest.dll'): shutil.copyfile(QT / 'bin' / name, app / name)
env = os.environ.copy()
env.update(TEMP=str(tmp), TMP=str(tmp), CUDA_CACHE_PATH=str(tmp / 'cuda-cache'),
           QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic',
           QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1')
env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(QT / 'plugins/platforms')
env['PATH'] = os.pathsep.join((env['WINDIR'] + '/System32', env['WINDIR'], env['WINDIR'] + '/System32/Wbem'))
for key in ('VEYRA_VFG_RUNTIME', 'VEYRA_UI_RHI'): env.pop(key, None)
cases = [('hardware-matrix', 'veyra_effect_availability_tests', []),
         ('vfg-settings', 'veyra_vfg_settings_tests', [str(out / 'vfg-settings')]),
         ('qml-software', 'veyra_qml_quick_tests', []),
         ('xbox-loopback', 'veyra_xbox_tests', []),
         ('xbox-float-output', 'veyra_capture_audio_tests', ['--xbox-float-rtp']),
         ('amd-abi-copy', 'veyra_lmxxf_nr_tests', []),
         ('ui-i18n', 'veyra_ui_i18n_tests', []),
         ('nr-preset-persistence', 'veyra_preset_library_tests', []),
         ('fsr-independent', 'veyra_fsr_dispatch_tests', []),
         ('fsr-switch', 'veyra_fsr_switch_tests', [str(out)]),
         ('vfg-native', 'veyra_vfg_gpu_tests', [str(app / 'runtime/nvidia-vfg'), '1280', '720'])]
if len(sys.argv) > 3: cases = [r for r in cases if r[0] in sys.argv[3:]]
assert cases
results = []
for name, binary, args in cases:
    child = env.copy()
    if name in ('fsr-independent', 'fsr-switch', 'vfg-native'):
        child.pop('QT_QPA_PLATFORM', None); child.pop('QT_QUICK_BACKEND', None)
    with (logs / (name + '.log')).open('xb') as log:
        try:
            code = subprocess.run([str(app / (binary + '.exe')), *args], cwd=app, env=child,
                                  stdout=log, stderr=subprocess.STDOUT, timeout=290).returncode
        except subprocess.TimeoutExpired: code = 124
    results.append(dict(test=name, exit=code, gpuPackage=flavor, log=str(logs / (name + '.log'))))
    print(results[-1], flush=True)
    if code: print((logs / (name + '.log')).read_text(encoding='utf8', errors='replace')[-4000:], flush=True)
(logs / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf8')
raise SystemExit(any(r['exit'] for r in results))
