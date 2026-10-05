"""Refresh only source/verification records after a harness fix; product bytes stay fixed."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004';LABEL='2.0.3-smoothfix'
OUT=BASE/'test-packages'/TASK;TMP=BASE/'tmp'/TASK
delivery=json.loads((OUT/'DELIVERY.json').read_text(encoding='utf8'))
APP=Path(delivery['app']).parent
assert APP.resolve().is_relative_to(OUT.resolve())
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()
allowed={'docs/CURRENT_STATUS.md','docs/WORKLOG.md','docs/PLAYBACK_SMOOTHNESS_PLAN_2026-10-04.md',
         'scripts/acceptance/playback-smoothness-package-smoke.py','scripts/acceptance/playback-smoothness-refresh.py'}
changed=set(subprocess.check_output(['git','diff','--name-only',delivery['sourceCommit'],commit],cwd=ROOT).decode().splitlines())
assert changed<=allowed,changed
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert digest(APP/'veyra_qml_ui.exe')==delivery['appSHA256']
assert all(r['passed'] for r in json.loads((OUT/'package-smoke.json').read_text(encoding='utf8')))
manifest_path=APP/'package-manifest.json'
manifest=json.loads(manifest_path.read_text(encoding='utf8'))
for row in manifest['files']:
    assert digest(APP/row['path'])==row['sha256'],row['path']
oldsource=Path(delivery['source']).resolve()
assert oldsource.parent==OUT.resolve() and digest(oldsource)==delivery['sourceSHA256']
backup=TMP/'first-source-snapshot';backup.mkdir(exist_ok=False)
for src in (oldsource,oldsource.with_suffix('.zip.sha256')):
    assert src.is_file() and src.parent==OUT.resolve()
    dst=backup/src.name
    assert dst.resolve().is_relative_to(TMP.resolve()) and not dst.exists()
    src.rename(dst)
spec=importlib.util.spec_from_file_location('smooth_source_package',ROOT/'scripts/package-qml-release.py')
pkg=importlib.util.module_from_spec(spec);spec.loader.exec_module(pkg)
source=pkg.source_snapshot(OUT,LABEL,delivery['appSHA256'])
manifest.update(productCodeCommit=delivery['sourceCommit'],baseCommit=commit,sourceArchiveCommit=commit)
manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
delivery.update(productCodeCommit=delivery['sourceCommit'],sourceCommit=commit,sourceSHA256=source['sha256'],
                packageManifestSHA256=digest(manifest_path),sealedPackageSmokePassed=True)
(OUT/'DELIVERY.json').write_text(json.dumps(delivery,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
(BASE/'archives'/TASK/'repair-source.patch').write_bytes(subprocess.check_output(['git','diff','--binary',
    '354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff',commit],cwd=ROOT))
subprocess.run([sys.executable,'-B',str(TMP/'smooth-package-audit.py'),str(APP),'NVIDIA','local-package-audit-final'],check=True,timeout=150)
print('FINAL SOURCE / PAYLOAD AUDIT PASS',commit,source['sha256'],delivery['appSHA256'])
