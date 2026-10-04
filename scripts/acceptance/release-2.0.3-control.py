"""Scope and source/binary separation for the authorized 2.0.3 release."""
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BASE = 'cb9b89e4259d1c430f40f976997f6155886bff46'
allowed = {'AGENTS.md', 'CMakeLists.txt', 'README.md', 'README_EN.md', 'README_CN.md', 'THIRD_PARTY_NOTICES.md',
           'src/ui/QmlPlayerBridge.cpp', 'include/veyra/ui/QmlPlayerBridge.h',
           'include/veyra/ui/EffectAvailability.h', 'src/gfx/FsrSrBackend.cpp', 'src/gfx/FsrFgPresenter.cpp',
           'tests/unit/EffectAvailabilityTests.cpp', 'scripts/package-qml-release.py', 'scripts/amd-nr/build-lmxxf-runtime.py',
           'scripts/package-vendor-release.py', 'scripts/package-2.0.3-dependency-source.py'}
paths = set(subprocess.check_output(['git', 'diff', '--name-only', BASE], cwd=ROOT).decode().splitlines())
paths.update(subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT).decode().splitlines())
bad = [p for p in paths if p not in allowed and not p.startswith(('docs/','qml/Veyra/','i18n/','scripts/acceptance/release-2.0.3-'))]
assert not bad, bad
assets = [p for p in paths if Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.onnx','.f16','.f32','.i32','.hsaco','.ptx','.whl','.addon64'}]
assert not assets, assets
print('2.0.3 scope PASS:',len(paths),'paths; no SDK/runtime/model assets')
