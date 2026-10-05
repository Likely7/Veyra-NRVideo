"""Bound this local repair to playback rate, subtitles and measured smoothness."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = 'fb8e5001a7ba8424aeddda002d9ca19a79e68f2f'
MAIN = '354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff'
allowed = {
    'src/ui/QmlPlayerBridge.cpp', 'include/veyra/ui/QmlPlayerBridge.h',
    'include/veyra/ui/PlaybackPowerGuard.h', 'include/veyra/engine/PlaybackRate.h',
    'src/engine/EngineController.cpp', 'src/engine/Subtitles.cpp',
    'src/sink/WasapiAudioSink.cpp', 'apps/veyra/ui/SubtitleOverlay.cpp',
    'apps/veyra-qml/main.cpp', 'qml/Veyra/PlaybackRateButton.qml',
    'qml/Veyra/PlaybackRateDialog.qml', 'qml/Veyra/qmldir',
    'qml/Veyra/CineBar.qml', 'qml/Veyra/ProPage.qml', 'qml/Veyra/Main.qml',
    'tests/integration/AudioPlaybackRateTests.cpp',
    'tests/integration/SubtitleTextTests.cpp', 'tests/integration/SubtitleOverlayTests.cpp',
    'CMakeLists.txt', 'i18n/catalog.json', 'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md',
    'docs/PLAYBACK_SMOOTHNESS_PLAN_2026-10-04.md', 'docs/SUBTITLE_ENGINE_2026-09-16.md',
}
git = ['git', '-c', 'core.safecrlf=false']
paths = set(subprocess.check_output([*git, 'diff', '--name-only', BASE], cwd=ROOT).decode().splitlines())
paths.update(subprocess.check_output([*git, 'ls-files', '--others', '--exclude-standard'], cwd=ROOT).decode().splitlines())
assert not [p for p in paths if p not in allowed and not p.startswith('scripts/acceptance/playback-smoothness-')], paths
assert not [p for p in paths if Path(p).suffix.lower() in {'.dll', '.exe', '.lib', '.pdb', '.hsaco', '.ptx', '.onnx', '.bin', '.f16'}]
assert subprocess.check_output(['git', 'rev-parse', 'main'], cwd=ROOT).decode().strip() == MAIN
subprocess.run([*git, 'diff', '--check'], cwd=ROOT, check=True)
print('Playback repair scope PASS:', len(paths), 'source paths; main unchanged')
