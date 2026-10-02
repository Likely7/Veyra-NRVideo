"""Exercise actual settings signals, modal buttons, persistence and process restart.
Only the isolated baseline-app copy gets temporary QML instrumentation.
"""
from pathlib import Path
import json, os, shutil, subprocess, time
import psutil

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
OUT = BASE / 'tests/release-2.0.0-20261002/obs-restart-v3'
OUT.mkdir(exist_ok=False)
APP = BASE / 'tests/release-2.0.0-20261002/baseline-app'
BUILD = BASE / 'build/release-2.0.0-20261002'
shutil.copy2(BUILD / 'veyra_qml_ui.exe', APP / 'veyra_qml_ui.exe')
shutil.copytree(ROOT / 'qml/Veyra', APP / 'qml/Veyra', dirs_exist_ok=True)
main = APP / 'qml/Veyra/Main.qml'
original = main.read_bytes()
results = []
for case in ('no', 'enable', 'disable'):
    run = OUT / case
    profile = run / 'profile'
    profile.mkdir(parents=True)
    if case == 'disable':
        (profile / 'qml-preferences.v1.json').write_text('{"obsGameCapture":true}')
    desired = case != 'disable'
    code = r'''
    function obsFind(item, name) {
        if (item.objectName === name) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = obsFind(children[i], name)
            if (found) return found
        }
        return null
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            const desired = DESIRED
            if (CASE !== "no" && veyra.obsGameCaptureActive === desired) {
                veyra.logUi("restart-test", "CHILD_PASS active=" + desired)
                Qt.quit()
                return
            }
            const sw = root.obsFind(root.contentItem, "set-obs-game-capture")
            if (!sw) { veyra.logUi("restart-test", "FAIL switch missing"); Qt.quit(); return }
            sw.checked = desired
            sw.toggled(desired)
            const dialog = root.obsFind(root.contentItem, "obs-restart-confirm")
            if (!dialog || !dialog.shown || !!veyra.preferences.obsGameCapture !== desired) {
                veyra.logUi("restart-test", "FAIL modal/persistence"); Qt.quit(); return
            }
            veyra.logUi("restart-test", "MODAL_PASS saved=" + desired)
            const button = root.obsFind(root.contentItem, "obs-restart-confirm-" + (CASE === "no" ? "reject" : "accept"))
            button.clicked()
            if (CASE === "no") {
                if (dialog.shown || veyra.obsGameCaptureActive) veyra.logUi("restart-test", "FAIL rejection")
                else veyra.logUi("restart-test", "NO_PASS")
                Qt.quit()
            }
        }
    }
'''.replace('DESIRED', str(desired).lower()).replace('CASE', json.dumps(case))
    main.write_text(original.decode('utf8').rstrip()[:-1] + code + '\n}\n', encoding='utf8')
    env = os.environ.copy()
    for k in ('VEYRA_UI_RHI', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND', 'QT_QPA_PLATFORM'):
        env.pop(k, None)
    env.update(TEMP=str(BASE / 'tmp/release-2.0.0-20261002'), TMP=str(BASE / 'tmp/release-2.0.0-20261002'),
               VEYRA_LOG_FILE=str(run / 'app.log'), QML_DISABLE_DISK_CACHE='1')
    try:
        with (run / 'console.log').open('wb') as f:
            p = subprocess.Popen([str(APP / 'veyra_qml_ui.exe'), '--data-dir', str(profile), '--page', 'set',
                                  '--exit-after', '20000'] + (['--obs-game-capture'] if case == 'disable' else []),
                                 cwd=APP, env=env, stdout=f, stderr=f)
            rc = p.wait(timeout=35)
        end = time.monotonic() + 25
        marker = 'NO_PASS' if case == 'no' else 'CHILD_PASS'
        while time.monotonic() < end:
            log = (run / 'console.log').read_text(encoding='utf8', errors='replace')
            owned = [q for q in psutil.process_iter(['exe', 'cmdline']) if
                     q.info['exe'] and Path(q.info['exe']) == APP / 'veyra_qml_ui.exe' and str(profile) in (q.info['cmdline'] or [])]
            if marker in log and not owned: break
            time.sleep(0.3)
        saved = json.loads((profile / 'qml-preferences.v1.json').read_text(encoding='utf8'))
        result = dict(case=case, exitCode=rc, saved=saved.get('obsGameCapture'),
                      pass_=rc == 0 and marker in log and 'FAIL ' not in log and not owned and saved.get('obsGameCapture') == desired)
        results.append(result)
        print(json.dumps(result), flush=True)
        assert result['pass_'], log[-3000:]
    finally:
        main.write_bytes(original)
(OUT / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf8')
