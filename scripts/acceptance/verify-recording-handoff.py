"""Extract this task's ZIPs, verify every manifest entry and cold-start both UIs."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, zipfile

BASE = Path('E:/项目/Veyra')
TASK = 'codex-claude-handoff-20261022'
REPORT = BASE/'reports'/TASK
VERIFY = BASE/'verify'/TASK
receipts = json.loads((REPORT/'packages.json').read_text('utf8'))
parser=argparse.ArgumentParser()
parser.add_argument('--software-existing', action='store_true')
args=parser.parse_args()
results = json.loads((REPORT/'cold-start.json').read_text('utf8')) if args.software_existing else []
if args.software_existing:
    for row in results:
        if row['mode']=='software': row['mode']='software-environment-only'
running = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True, encoding='mbcs', errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()), 'another Veyra process is running'

def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

for receipt in receipts:
    vendor = receipt['vendor']
    root = VERIFY/vendor
    if args.software_existing:
        assert root.is_dir(), root
    else:
        assert not root.exists(), root
        root.mkdir(parents=True)
    archive = Path(receipt['zip'])
    assert sha(archive)==receipt['zipSha256']
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            assert (root/item.filename).resolve().is_relative_to(root.resolve()), item.filename
        if not args.software_existing: z.extractall(root)
    package = root/Path(receipt['package']).name
    manifest = json.loads((package/'package-manifest.json').read_text('utf8'))
    for entry in manifest['files']:
        p = package/entry['path']
        assert p.stat().st_size==entry['size'] and sha(p)==entry['sha256'], p
    for mode in (('software',) if args.software_existing else ('gpu', 'software')):
        suffix=mode+('-confirmed' if args.software_existing else '')
        log = BASE/'logs'/TASK/('cold-'+vendor+'-'+suffix)
        log.mkdir(parents=True)
        profile = root/('profile-'+suffix)
        profile.mkdir()
        (profile/'qml-preferences.v1.json').write_text(json.dumps({'language':'zh-CN','autoResume':False,'prewarmEnhancement':False}),'utf8')
        tmp = BASE/'tmp'/TASK/('cold-'+vendor+'-'+mode)
        tmp.mkdir(parents=True,exist_ok=True)
        env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
        env.update(TEMP=str(tmp), TMP=str(tmp), APPDATA=str(profile/'AppData'), LOCALAPPDATA=str(profile/'LocalAppData'),
                   CUDA_CACHE_PATH=str(tmp/'cuda'), VEYRA_LOG_FILE=str(log/'engine.log'), QT_FORCE_STDERR_LOGGING='1', QML_DISABLE_DISK_CACHE='1')
        if mode=='software':
            env['QT_QUICK_BACKEND']='software'
        cmd = [str(package/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','4500']
        if mode=='software':
            cmd.append('--obs-game-capture')
        with (log/'console.log').open('wb') as f:
            rc = subprocess.run(cmd,cwd=package,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=45).returncode
        text = (log/'console.log').read_text('utf8',errors='replace')
        failures = [l for l in text.splitlines() if any(x in l for x in ('[ERROR]','TypeError:','ReferenceError:','Cannot assign to non-existent','failed to load component')) and 'Retrying to obtain clipboard.' not in l]
        assert rc==0 and not failures, (vendor,mode,rc,failures[:5])
        if mode=='software':
            assert 'software UI:' in text, (vendor, 'software backend not confirmed')
        # Starting must not mutate any shipped program or runtime bytes.
        for entry in manifest['files']:
            assert sha(package/entry['path'])==entry['sha256'], entry['path']
        result=dict(vendor=vendor,mode=mode,exit=rc,manifestFilesVerified=len(manifest['files']),path=str(log))
        results.append(result)
        print(json.dumps(result),flush=True)
(REPORT/'cold-start.json').write_text(json.dumps(results,indent=2),'utf8')
