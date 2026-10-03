"""Scope and source/runtime isolation for the explicitly authorized VFG integration."""
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BASE = '1802f43565e07f3c3040d3fe745ca2e939aab00f'
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode('utf8').strip()
if git('branch', '--show-current') != 'codex/vfg-integration-20261003' or git('rev-parse', 'checkpoint/pre-vfg-integration-20261003^{commit}') != BASE:
    raise SystemExit('VFG SCOPE FAIL: branch/archive')
allowed = {'AGENTS.md','CMakeLists.txt','THIRD_PARTY_NOTICES.md','i18n/catalog.json',
           'src/pipeline/EnhanceGraph.cpp','include/veyra/pipeline/EnhanceGraph.h',
           'src/pipeline/VfgBackend.cpp','include/veyra/pipeline/VfgBackend.h',
           'include/veyra/pipeline/FrameBatch.h','include/veyra/diagnostics/FrameMetrics.h',
           'include/veyra/engine/EnhancementSettings.h','include/veyra/engine/GraphDescription.h',
           'src/engine/EngineController.cpp','src/engine/VideoExportJob.cpp','src/engine/VideoPresenter.cpp',
           'include/veyra/engine/ExportWorkerProtocol.h','src/engine/SettingsJson.cpp',
           'src/engine/PresetStore.cpp','src/engine/PresetLibrary.cpp',
           'include/veyra/engine/PresetLibrary.h','include/veyra/engine/EffectChain.h','src/engine/EffectChain.cpp',
           'src/engine/ExportJobManager.cpp',
           'include/veyra/ui/QmlPlayerBridge.h','src/ui/QmlPlayerBridge.cpp',
           'include/veyra/ui/PlayerUiFacade.h','src/ui/PlayerUiFacade.cpp',
           'src/ui/QmlSessionStore.cpp','src/ui/EffectGraphModel.cpp'}
changed = set(git('diff','--name-only',BASE).splitlines()) | set(git('ls-files','--others','--exclude-standard').splitlines())
extra = {p for p in changed if p not in allowed and not p.startswith(('docs/','scripts/acceptance/vfg-','tests/','qml/Veyra/'))}
binary = {p for p in changed if Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.zip','.whl','.f16','.f32','.onnx','.hsaco','.cso','.addon64'}}
if extra or binary:
    raise SystemExit('VFG SCOPE FAIL:\n' + '\n'.join(sorted(extra|binary)))
print('VFG SCOPE PASS:',len(changed),'paths; no SDK/runtime/model assets')
