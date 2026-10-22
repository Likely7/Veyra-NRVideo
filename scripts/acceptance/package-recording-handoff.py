"""Stage local handoff packages, audit unchanged donor runtimes and optionally ZIP.

No existing output is overwritten. All replacements unlink hard links first.
usage: python -X utf8 -B scripts/acceptance/package-recording-handoff.py [--zip]
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time, zipfile

BASE = Path('E:/项目/Veyra')
TASK = 'codex-claude-handoff-20261022'
ROOT = Path(__file__).resolve().parents[2]
BUILD = BASE / 'build/claude-screen-recording-20261022'
OUT = BASE / 'test-packages' / TASK
REPORT = BASE / 'reports' / TASK
parser = argparse.ArgumentParser()
parser.add_argument('--zip', action='store_true')
args = parser.parse_args()
OUT.mkdir(parents=True, exist_ok=True)
REPORT.mkdir(parents=True, exist_ok=True)
head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()

def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def replace(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    path.write_bytes(data)

note = '''# Veyra 2.1.0 录制修复测试版（rec2）

完整解压后运行 veyra_qml_ui.exe。软件内仍显示 2.1.0-rec；这是本地测试构建。

- AMD 超分关闭修复已包含。
- 专业模式截图旁录制按钮：3 秒倒计时，可取消；录制中显示时间，点击停止。
- 设置 → 播放 → 录制：保存位置、自动/指定码率、帧率、H.264/HEVC、录制声音。
- 录制处理后的画面和播放器的声音，播放器音量/静音不影响录音；采集卡需开启音频监听。
- 改效果继续录制，输出尺寸或 HDR 模式变化时接着写 -2、-3 等新文件。
- 暂停保留真实时间间隔，播放器停止或换片源会保存录制。
- XeSS 插件内部生成帧不在引擎纹理池里，不能宣称录到这些生成帧。
- NVIDIA 录制使用 D3D12 NVENC。AMD/Intel 使用系统硬件编码器，像素回读和编码在录制工作线程上；不把这个后端称为零拷贝。
- HDR 录制仅 NVIDIA HEVC 10bit；本轮未做 HDR 录制验收。

本机通过：AMD 核显超分开关、1080p60 带声录制、约62秒连续录制、4倍补帧、FSR切分辨率、暂停恢复、4K文件录制、H.264/HEVC、限帧/静音/关闭音轨、连续启停和错误目录。
详见 docs/CLAUDE_HANDOFF_REPORT_2026-10-22.md。未做长时间稳定性、真实采集卡/主机串流、其他显卡和屏幕光学延迟验收。
隔离环境中切到 DLSS SR 有 CreateFeature 卡住的问题，不录制对照也会出现；此项单列，不能说已经修复。
Lecram 按用户要求暂停，不包含在此包。未合并 main、未推送或公开发布。
'''
receipts = []
for vendor in ('NVIDIA', 'AMD'):
    donor = BASE / 'test-packages/bilibili-2.1.0-20261019' / f'Veyra-2.1.0-test-{vendor}-win64-portable'
    package = OUT / f'Veyra-2.1.0-rec2-{vendor}-win64-portable'
    assert donor.is_dir() and not package.exists(), package
    for source in donor.rglob('*'):
        target = package / source.relative_to(donor)
        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif source.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            os.link(source, target)
    replace(package / 'veyra_qml_ui.exe', (BUILD / 'veyra_qml_ui.exe').read_bytes())
    for folder in ('qml', 'i18n'):
        for source in (ROOT / folder).rglob('*'):
            if source.is_file():
                target = package / folder / source.relative_to(ROOT / folder)
                if not target.exists() or target.read_bytes() != source.read_bytes():
                    replace(target, source.read_bytes())
    for name in ('CLAUDE_HANDOFF_PLAN_2026-10-22.md', 'CLAUDE_HANDOFF_REPORT_2026-10-22.md'):
        replace(package / 'docs' / name, (ROOT / 'docs' / name).read_bytes())
    replace(package / 'README-TEST.zh-CN.md', note.encode('utf8'))
    # Every publisher-supplied runtime, model and shader must stay byte-identical.
    original = json.loads((donor / 'package-manifest.json').read_text('utf8'))
    unchanged = []
    for entry in original['files']:
        rel = entry['path']
        if rel.lower().endswith(('.dll', '.onnx', '.tflite', '.dxil', '.hsaco')):
            actual = sha(package / rel)
            assert actual == entry['sha256'].lower() == sha(donor / rel), rel
            unchanged.append(rel)
    files = [{'path': p.relative_to(package).as_posix(), 'size': p.stat().st_size, 'sha256': sha(p)}
             for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'package-manifest.json']
    manifest = dict(original)
    manifest.update(candidate='2.1.0-rec2-local', displayVersion='2.1.0-rec', localOnly=True,
                    releaseReady=False, sourceArchiveCommit=head, mainCommit=subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "main"], text=True).strip(), productSourceCommit=head,
                    validationRecord='docs/CLAUDE_HANDOFF_REPORT_2026-10-22.md',
                    exeSha256=sha(package / 'veyra_qml_ui.exe'), files=files)
    replace(package / 'package-manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf8'))
    result = dict(vendor=vendor, package=str(package), source=head, exeSha256=manifest['exeSha256'],
                  files=len(files), unchangedRuntimeModelShaderFiles=len(unchanged))
    if args.zip:
        archive = package.with_suffix('.zip')
        assert not archive.exists(), archive
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as z:
            for p in package.rglob('*'):
                if p.is_file():
                    z.write(p, package.name + '/' + p.relative_to(package).as_posix())
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
        result.update(zip=str(archive), zipBytes=archive.stat().st_size, zipSha256=sha(archive))
    receipts.append(result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
(REPORT / 'packages.json').write_text(json.dumps(receipts, ensure_ascii=False, indent=2), 'utf8')
