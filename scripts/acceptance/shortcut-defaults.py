"""Screenshot F7 / record F8 defaults on a private profile (claude-capture-fps-audio-20261027).

Two runs of this tree's build in the staged copy that screen-capture-rate.py made: a
fresh profile (F7 / F8), and a profile whose fullscreen key was already F8 (fullscreen
keeps it, record stays unbound until set). Opens on the settings page; no key input
is sent to any window.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/shortcut-defaults.py
"""
from pathlib import Path
import json, os, shutil, subprocess

B = Path('E:/项目/Veyra'); T = 'claude-capture-fps-audio-20261027'; R = Path(__file__).resolve().parents[2]
D = B/'build/claude-screen-recording-20261022'; A = B/'tests'/T/'app'; L = B/'logs'/T/'shortcuts'; TMP = B/'tmp'/T/'shortcuts'
assert (A/'veyra_qml_ui.exe').exists(), 'run screen-capture-rate.py --exe build first (it stages the app)'
for p in [L, TMP]: p.mkdir(parents=True, exist_ok=True)


def replace(p, data):
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists(): p.unlink()
    p.write_bytes(data)


replace(A/'veyra_qml_ui.exe', (D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml', 'i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file(): replace(A/folder/p.relative_to(R/folder), p.read_bytes())
cases = {'fresh': ({}, {'screenshot': 'F7', 'record': 'F8', 'fullscreen': 'F'}),
         'f8-taken': ({'fullscreen': 'F8'}, {'screenshot': 'F7', 'record': '', 'fullscreen': 'F8'})}
results = {}
main_src = R/'qml/Veyra/Main.qml'; main = main_src.read_text('utf8'); pos = main.rfind('}')
for name, (custom, expect) in cases.items():
    P = B/'tests'/T/('profile-shortcuts-' + name)
    if P.exists(): shutil.rmtree(P)
    P.mkdir(parents=True)
    prefs = dict(language='zh-CN', autoResume=False, prewarmEnhancement=False)
    if custom: prefs['shortcuts'] = custom
    (P/'qml-preferences.v1.json').write_text(json.dumps(prefs), 'utf8')
    loader = ('\nLoader { anchors.fill: parent; source: ' + json.dumps((R/'scripts/acceptance/shortcut-defaults.qml').as_uri()) +
              '; onLoaded: item.expect=' + json.dumps(expect) + ' }\n')
    replace(A/'qml/Veyra/Main.qml', (main[:pos] + loader + main[pos:]).encode('utf8'))
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
    log_file = L/f'{name}.log'; console = L/f'{name}-console.log'
    env.update(TEMP=str(TMP), TMP=str(TMP), APPDATA=str(P/'AppData'), LOCALAPPDATA=str(P/'LocalAppData'),
               QML_DISABLE_DISK_CACHE='1', QT_FORCE_STDERR_LOGGING='1', VEYRA_LOG_FILE=str(log_file))
    try:
        with console.open('wb') as f:
            run = subprocess.run([str(A/'veyra_qml_ui.exe'), '--page', 'set', '--size', '1100x700', '--data-dir', str(P), '--exit-after', '30000'],
                                 cwd=A, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=60)
    finally:
        replace(A/'qml/Veyra/Main.qml', main_src.read_bytes())
    text = console.read_text('utf8', errors='replace')
    results[name] = dict(exit=run.returncode, passed='SC_PASS' in text and 'SC_FAIL' not in text,
                         line=next((l for l in text.splitlines() if 'SC_' in l), ''),
                         qmlErrors=[l for l in text.splitlines() if any(s in l for s in ['ReferenceError:', 'TypeError:', 'Binding loop detected', 'ambiguous'])][:5])
(B/'reports'/T/'shortcuts.json').write_text(json.dumps(results, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(results, ensure_ascii=False, indent=1))
