"""Strict existing eight-page smoke with a fresh, explicitly scoped QML stage."""
from pathlib import Path
import importlib.util, json, os, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
spec = importlib.util.spec_from_file_location('matrix', ROOT/'scripts/perf/nr-matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)
variant, label = sys.argv[1:3]
assert label.replace('-', '').isalnum()
matrix.assert_gpu_tests_idle()
app = BASE/'tests'/TASK/(label+'-app')
assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable', app,
                copy_function=matrix.copy_dependency, ignore=shutil.ignore_patterns('*.exe', '*.log', '*.dmp'))
build = BASE/'build'/TASK/variant
for name in ('veyra_qml_ui', 'veyra_qml_data_tests', 'veyra_qml_easing_tests', 'veyra_qml_quick_tests'):
    shutil.copy2(build/(name+'.exe'), app/(name+'.exe'))
shutil.copytree(ROOT/'qml', app/'qml', dirs_exist_ok=True)
shutil.copytree(ROOT/'tests/qml/quick', app/'qml-tests')
qt = Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
for dll in ('Qt6QuickTest.dll', 'Qt6Test.dll'):
    shutil.copy2(qt/'bin'/dll, app/dll)
shutil.copytree(qt/'qml/QtTest', app/'qml/QtTest', copy_function=matrix.copy_dependency)
shutil.copy2(qt/'plugins/platforms/qoffscreen.dll', app/'platforms/qoffscreen.dll')
fixtures = BASE/'tests'/TASK/(label+'-fixtures')
fixtures.mkdir(exist_ok=False)
os.link(matrix.SOURCES['M1'], fixtures/'test_av_1080p.mp4')
logs, tmp = BASE/'logs'/TASK/label, BASE/'tmp'/TASK/label
logs.mkdir(exist_ok=False)
tmp.mkdir(exist_ok=False)
payload = {p.relative_to(app).as_posix(): matrix.digest(p) for p in app.rglob('*')
           if p.is_file() and p.suffix.lower() in ('.exe', '.dll', '.qml')}
(logs/'artifacts.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND'))}
env.update(TEMP=str(tmp), TMP=str(tmp))
command = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
           str(ROOT/'scripts/acceptance/qml-ui-smoke.ps1'), '-Root', str(ROOT), '-UiTarget', 'qml',
           '-PlayerExe', str(app/'veyra_qml_ui.exe'), '-BuildDirectory', str(build),
           '-StagingDirectory', str(app), '-OutputDirectory', str(logs), '-TempDirectory', str(tmp),
           '-FixtureRoot', str(fixtures), '-ExitAfterMs', '3500']
# The desktop's inherited PSModulePath belongs to its bundled PowerShell.
# Explicitly import Windows PowerShell's own modules in this child only; do
# not modify the read-only acceptance script or global environment.
quote = lambda value: "'" + str(value).replace("'", "''") + "'"
wrapper = tmp/'smoke-entry.ps1'
wrapper.write_text(
    "$ErrorActionPreference = 'Stop'\n"
    "Import-Module ($PSHOME + '/Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')\n"
    "Import-Module ($PSHOME + '/Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1')\n"
    + '$smokeArgs = @{\n' + '\n'.join(arg.lstrip('-')+' = '+quote(value) for arg, value in zip(command[6::2], command[7::2]))
    + '\n}\n& ' + quote(command[5]) + ' @smokeArgs\nexit $LASTEXITCODE\n', encoding='utf-8-sig')
command = command[:5] + [str(wrapper)]
with (logs/'driver.log').open('xb') as stream:
    rc = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=250).returncode
assert all(matrix.digest(app/name) == sha for name, sha in payload.items())
results = list(logs.glob('run-*/result.json'))
assert len(results) == 1
result = json.loads(results[0].read_text(encoding='utf-8-sig'))
assert rc == 0 and result['passed'] and result['skipped'] == 0 and len(result['cases']) == 8, result
matrix.assert_gpu_tests_idle()
print('R0_QML_SMOKE_COMPLETE', rc, len(result['cases']), str(results[0]), flush=True)
