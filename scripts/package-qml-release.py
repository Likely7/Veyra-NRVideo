"""Assemble a local QML candidate from build outputs and an audited runtime lock.

Never copy a previously used application's directory. Runtime identities are a
publisher check, not a restriction on DLLs the application will load at run time.
This creates a candidate, not a public Release or a complete dependency source offer.
"""
import argparse
import tempfile
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = Path('E:/项目/Veyra')
FORBIDDEN = {'.pdb', '.lib', '.obj', '.h', '.hpp', '.cpp', '.c', '.zip', '.7z',
             '.pth', '.onnx', '.log', '.dmp', '.mp4', '.mkv', '.partial', '.addon64'}
REQUIRED = {'veyra_qml_ui.exe', 'qml/Veyra/Main.qml', 'platforms/qwindows.dll',
            'Qt6Quick.dll', 'Qt6Core.dll', 'qt.conf', 'licenses/FFMPEG-VEYRA-BUILD.json',
            'licenses/qt/LGPL-3.0-only.txt', 'licenses/qt/GPL-3.0-only.txt',
            'licenses/NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf', 'licenses/INTEL_XESS_LICENSE.txt',
            'licenses/AMD-FIDELITYFX-LICENSE.txt', 'licenses/moonlight/LICENSE.txt',
            'licenses/xbox/libdatachannel.txt', 'licenses/magewell/MWCapture-SDK-NOTICE.txt',
            'licenses/DLSSG-Transfusion-MIT.txt', 'licenses/SoundTouch-LGPL-2.1.txt',
            'licenses/SoundTouch-origin.md', 'qml/Veyra/PlaybackRateButton.qml', 'release-runtime-manifest.json',
            'runtime/experimental/release-runtime-manifest.json'}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(name):
    p = PurePosixPath(name)
    if not name or '\\' in name or ':' in name or p.is_absolute() or '..' in p.parts or str(p) != name:
        raise ValueError('Unsafe relative payload path: ' + name)
    if any(part.endswith((' ', '.')) or re.fullmatch(r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(\..*)?', part)
           for part in p.parts):
        raise ValueError('Unsafe Windows payload path: ' + name)
    return p


def validate_payload(name):
    p = relative(name)
    parts = {part.lower() for part in p.parts}
    if p.suffix.lower() in FORBIDDEN or parts & {'logs', 'captures', 'qml-tests', '.git', 'third_party_local'}:
        raise ValueError('Forbidden candidate payload: ' + name)
    if any(part.startswith(('user-data', 'qml-preferences', 'last-applied', 'presets.v1')) for part in parts):
        raise ValueError('User data in candidate: ' + name)
    if p.suffix.lower() == '.exe' and name != 'veyra_qml_ui.exe':
        raise ValueError('Unexpected executable: ' + name)
    if any(token in name.lower() for token in ('qt6test', 'qt6quicktest', 'qml/qttest', 'qmltooling/')):
        raise ValueError('Test/development Qt dependency: ' + name)
    if 'runtime_local' in parts and not name.startswith(('runtime_local/amd/fidelityfx/', 'runtime_local/intel/experimental/')):
        raise ValueError('Unapproved runtime_local directory: ' + name)


def verify_zip(path):
    with zipfile.ZipFile(path) as z:
        for entry in z.infolist():
            relative(entry.filename.rstrip('/'))
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError('ZIP contains a symbolic link')
        names = [e.filename for e in z.infolist() if not e.is_dir()]
        if len(names) != len(set(n.lower() for n in names)):
            raise ValueError('Duplicate ZIP paths')
        manifests = [n for n in names if n.count('/') == 1 and n.endswith('/package-manifest.json')]
        if len(manifests) != 1:
            raise ValueError('Missing unique package manifest')
        prefix = manifests[0].split('/')[0] + '/'
        manifest = json.loads(z.read(manifests[0]))
        records = manifest['files']
        expected = {prefix + r['path'] for r in records} | {manifests[0]}
        if len(expected) != len(records) + 1 or set(names) != expected:
            raise ValueError('ZIP payload differs from manifest')
        for r in records:
            validate_payload(r['path'])
            info = z.getinfo(prefix + r['path'])
            with z.open(info) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if info.file_size != r['size'] or actual != r['sha256']:
                raise ValueError('ZIP identity mismatch: ' + r['path'])
        if REQUIRED - {r['path'] for r in records}:
            raise ValueError('Missing required product/notice files')
    return dict(archive=str(path), sha256=digest(path), verifiedFiles=len(records), status='pass')


def source_snapshot(output, label, app_sha):
    """Veyra-only working files and packaging instructions; no SDK/runtime.

    Explicitly not the complete dependency source distribution required for a
    public release. Do not silently reuse the old 1.4.1 dependency archives.
    """
    archive = output / ('Veyra-' + label + '-veyra-source.zip')
    names = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z',
                                     '--cached', '--others', '--exclude-standard']).decode('utf8').split('\0')
    records = []
    prefix = 'Veyra-' + label + '-veyra-source/'
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in sorted(set(filter(None, names))):
            relative(name)
            if re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|zip|7z|ptx|addon64)$|(^|/)(runtime_local|third_party_local|\.git)/', name, re.I):
                raise ValueError('Forbidden source payload: ' + name)
            f = ROOT / name
            if not f.is_file():
                continue  # a deletion in the working tree is part of the snapshot
            if f.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('Source contains a junction/symlink: ' + name)
            data = f.read_bytes()
            z.writestr(prefix + name, data)
            records.append(dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
        metadata = dict(schema=1, candidate=label, appSha256=app_sha,
                        snapshot='tracked and non-ignored working files',
                        baseCommit=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip(),
                        completeCorrespondingSource=False, files=records,
                        limitation='Dependency sources and exact rebuild materials must be completed before public release.')
        z.writestr(prefix + 'source-snapshot-manifest.json', json.dumps(metadata, indent=2))
    with zipfile.ZipFile(archive) as z:
        for r in records:
            if hashlib.sha256(z.read(prefix + r['path'])).hexdigest() != r['sha256']:
                raise ValueError('Source archive identity mismatch: ' + r['path'])
    archive.with_suffix('.zip.sha256').write_text(digest(archive) + '  ' + archive.name + '\n', encoding='ascii')
    return dict(name=archive.name, sha256=digest(archive), files=len(records), completeCorrespondingSource=False)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--verify', type=Path)
    p.add_argument('--build', type=Path)
    p.add_argument('--runtime-source', type=Path)
    p.add_argument('--original-nr', type=Path, help='Approved original NR DLL, audited against the version runtime lock')
    p.add_argument('--legacy-licenses', type=Path)
    p.add_argument('--qt', type=Path)
    p.add_argument('--qt-licenses', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--label', default='2.0.0')
    p.add_argument('--no-archive', action='store_true')
    p.add_argument('--release', action='store_true', help='Require a clean source tree and include corresponding-source links')
    args = p.parse_args()
    if args.verify:
        print(json.dumps(verify_zip(args.verify), indent=2))
        return
    for field in ('build', 'runtime_source', 'legacy_licenses', 'qt', 'qt_licenses', 'output'):
        if getattr(args, field) is None:
            p.error('Missing --' + field.replace('_', '-'))
    if not re.fullmatch(r'2\.0\.\d+(?:-[a-z0-9.-]+)?', args.label):
        raise ValueError('Expected a 2.0.x version or an explicit test suffix')
    version = args.label.split('-')[0]
    lock_path = ROOT / 'docs' / ('RELEASE_' + version + '_RUNTIME_LOCK.json')
    if not lock_path.is_file():
        lock_path = ROOT / 'docs/RELEASE_2.0.0_RUNTIME_LOCK.json'
    if args.release:
        if subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain']).strip():
            raise ValueError('Public release requires a clean source tree')
        if not (ROOT / 'docs' / ('BUILD_' + version + '.md')).is_file():
            raise ValueError('Public release requires version-specific corresponding-source instructions')
    notes = 'RELEASE_NOTES_' + version + '.md' if (ROOT / 'docs' / ('RELEASE_NOTES_' + version + '.md')).is_file() else 'RELEASE_NOTES_2.0.0.md'
    if not args.output.resolve().is_relative_to(ARTIFACTS.resolve()):
        raise ValueError('Candidate output must stay in the E: artifact tree')
    stage = args.output / ('Veyra-' + args.label + '-win64-portable')
    archive = Path(str(stage) + '.zip')
    if stage.exists() or archive.exists():
        raise FileExistsError('Refusing to reuse a candidate')
    stage.mkdir(parents=True)
    env = os.environ.copy()
    tmp = Path(tempfile.mkdtemp(prefix='package-' + args.label + '-', dir=ARTIFACTS / 'tmp'))
    env.update(TEMP=str(tmp), TMP=str(tmp))
    env['PATH'] = str(args.qt / 'bin') + os.pathsep + env['PATH']

    def copy(source, name):
        validate_payload(name)
        target = stage / name
        if target.exists() and digest(source) != digest(target):
            raise ValueError('Conflicting payload: ' + name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)

    def tree(source, target):
        for f in sorted(source.rglob('*')):
            if f.is_file():
                copy(f, target + '/' + f.relative_to(source).as_posix())

    # Only compiled production assets come from the current build.
    copy(args.build / 'veyra_qml_ui.exe', 'veyra_qml_ui.exe')
    tree(args.build / 'qml/Veyra', 'qml/Veyra')
    for f in sorted((args.build / 'shaders').rglob('*.dxil')):
        copy(f, 'shaders/' + f.relative_to(args.build / 'shaders').as_posix())
    with (args.output / ('windeployqt-' + args.label + '.log')).open('xb') as log:
        subprocess.run([str(args.qt / 'bin/windeployqt.exe'), '--release', '--qmldir', str(ROOT / 'qml'),
                        '--no-compiler-runtime', '--no-translations', '--no-patchqt', '--no-opengl-sw',
                        '--skip-plugin-types', 'qmltooling,generic', str(stage / 'veyra_qml_ui.exe')],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=290)
    (stage / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\nTranslations=translations\n', encoding='utf8')

    cache = (args.build / 'CMakeCache.txt').read_text(encoding='utf8')
    for option in ('VEYRA_BUILD_QML_UI', 'VEYRA_ENABLE_REMOTEPLAY', 'VEYRA_ENABLE_MOONLIGHT', 'VEYRA_ENABLE_XBOX'):
        if not re.search(r'^' + option + r':BOOL=ON$', cache, re.M):
            raise ValueError('Missing full product build option: ' + option)
    ffmpeg = Path(re.search(r'^VEYRA_FFMPEG_ROOT:[^=]+=(.+)$', cache, re.M).group(1))
    ffbuild = json.loads((ffmpeg / 'share/ffmpeg/veyra-local-build.json').read_text(encoding='utf-8-sig'))
    if ffbuild['patchSha256'].lower() != digest(ROOT / 'scripts/ffmpeg/ps5-h264-slices.patch'):
        raise ValueError('PS5 FFmpeg patch provenance mismatch')
    for record in ffbuild['files']:
        source = ffmpeg / 'bin' / record['name']
        if digest(source) != record['sha256'].lower():
            raise ValueError('Patched FFmpeg binary mismatch: ' + record['name'])
        copy(source, record['name'])
    if not (stage / 'dav1d.dll').is_file():
        copy(ffmpeg / 'bin/dav1d.dll', 'dav1d.dll')

    # These are the pinned, already approved runtime identities, including kernels.
    lock = json.loads(lock_path.read_text(encoding='utf8'))
    audit_env = env.copy()
    # A parent PowerShell 7 session may otherwise force Windows PowerShell 5 to
    # import incompatible copies of its built-in Security module.
    audit_env = {k: v for k, v in audit_env.items() if k.upper() != 'PSMODULEPATH'}
    audit_env.update(VEYRA_PACKAGE_RUNTIME=str(args.runtime_source),
                     VEYRA_PACKAGE_LOCK=str(lock_path),
                     VEYRA_PACKAGE_ORIGINAL_NR=str(args.original_nr) if args.original_nr else '')
    signature_check = r"""$ErrorActionPreference='Stop';
$lock=Get-Content -LiteralPath $env:VEYRA_PACKAGE_LOCK -Raw -Encoding UTF8 | ConvertFrom-Json;
foreach($f in $lock.files) {
  if(-not $f.path.EndsWith('.dll')) { continue };
  $p=Join-Path $env:VEYRA_PACKAGE_RUNTIME $f.path;
  if($f.path -eq 'runtime/experimental/nr-original/nvngx_dlssnr.dll' -and $env:VEYRA_PACKAGE_ORIGINAL_NR) { $p=$env:VEYRA_PACKAGE_ORIGINAL_NR };
  $s=Get-AuthenticodeSignature -LiteralPath $p;
  $v=(Get-Item -LiteralPath $p).VersionInfo;
  $version="$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)";
  if([string]$s.Status -ne $f.authenticode -or $version -ne $f.fileVersion) { throw ('Runtime metadata mismatch: '+$f.path) }
}; Write-Output 'Publisher signatures and numeric versions match the approved lock.'"""
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', signature_check],
                   env=audit_env, check=True, timeout=290)
    for record in lock['files']:
        source = args.runtime_source / relative(record['path'])
        if record['path'] == 'runtime/experimental/nr-original/nvngx_dlssnr.dll' and args.original_nr:
            source = args.original_nr
        if source.stat().st_size != record['size'] or digest(source) != record['sha256'].lower():
            raise ValueError('Publisher runtime mismatch: ' + record['path'])
        copy(source, record['path'])
    runtime_manifest = json.dumps(dict(schema=2, package=args.label,
        enforcedAtRuntime=False, files=lock['files']), indent=2)
    (stage / 'release-runtime-manifest.json').write_text(runtime_manifest, encoding='utf8')
    # QmlPlayerBridge reads this path. Include every provider in its component list.
    (stage / 'runtime/experimental/release-runtime-manifest.json').write_text(runtime_manifest, encoding='utf8')
    # Copy only the shared, public NGX configuration; never user settings/presets.
    copy(ROOT / 'runtime/config/ngx-local.json', 'runtime/config/ngx-local.json') if (ROOT / 'runtime/config/ngx-local.json').is_file() else copy(args.runtime_source / 'runtime/config/ngx-local.json', 'runtime/config/ngx-local.json')

    # Runtime notices from the audited 1.4.4 layout, plus the new source notices.
    for f in sorted(args.legacy_licenses.rglob('*')):
        if f.is_file():
            name = f.relative_to(args.legacy_licenses).as_posix()
            if not (ROOT / 'licenses' / name).is_file():
                copy(f, 'licenses/' + name)
    tree(ROOT / 'licenses', 'licenses')
    copy(ARTIFACTS / 'deps/moonlight/moonlight-common-c/LICENSE.txt', 'licenses/moonlight/LICENSE.txt')
    copy(ROOT / 'src/ngx/transfusion/LICENSE.txt', 'licenses/DLSSG-Transfusion-MIT.txt')
    copy(ROOT / 'third_party/soundtouch/COPYING.TXT', 'licenses/SoundTouch-LGPL-2.1.txt')
    copy(ROOT / 'third_party/soundtouch/VEYRA_ORIGIN.md', 'licenses/SoundTouch-origin.md')
    tree(args.qt_licenses, 'licenses/qt')
    tree(args.qt / 'sbom', 'licenses/qt/sbom')
    copy(ffmpeg / 'share/ffmpeg/veyra-local-build.json', 'licenses/FFMPEG-VEYRA-BUILD.json')
    # Use a known Microsoft redistributable installation, not arbitrary PATH DLLs.
    redist = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC')
    crt = sorted((f for f in redist.iterdir() if re.fullmatch(r'\d+\.\d+\.\d+', f.name)),
                 key=lambda f: tuple(map(int, f.name.split('.'))))[-1] / 'x64/Microsoft.VC143.CRT'
    for name in ('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll'):
        copy(crt / name, name)
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        copy(ROOT / name, name)
    copy(ROOT / 'README.md', 'README.md')
    copy(ROOT / 'README_EN.md', 'README_EN.md')
    if (ROOT / 'README_CN.md').is_file():
        copy(ROOT / 'README_CN.md', 'README_CN.md')
    for name in ('veyra-app-icon.png', 'veyra-2.0.0-promo.webp'):
        copy(ROOT / 'assets' / name, 'assets/' + name)
    for name in sorted({'RUNTIME_COMPONENTS_2.0.0.md', 'BUILD.md', 'BUILD_2.0.0.md', 'RELEASE_NOTES_2.0.0.md', notes}):
        copy(ROOT / 'docs' / name, 'docs/' + name)
    copy(ROOT / 'docs' / notes, 'RELEASE_NOTES.md')
    for name in ('BUILD_' + version + '.md', 'RUNTIME_COMPONENTS_' + version + '.md'):
        if (ROOT / 'docs' / name).is_file():
            copy(ROOT / 'docs' / name, 'docs/' + name)

    tree(ROOT / 'docs/images/2.0.0', 'docs/images/2.0.0')
    records = []
    for f in sorted(stage.rglob('*')):
        if f.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Candidate contains a junction/symlink')
        if f.is_file():
            name = f.relative_to(stage).as_posix()
            validate_payload(name)
            records.append(dict(path=name, size=f.stat().st_size, sha256=digest(f)))
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip()
    changed = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain']).decode('utf8')
    manifest = dict(schema=2, version=version, candidate=args.label, baseCommit=commit,
                    worktreeDirty=bool(changed), releaseReady=args.release, files=records)
    if args.release:
        manifest['correspondingSource'] = dict(
            application='https://github.com/Likely7/Veyra-NRVideo/archive/refs/tags/v' + version + '.zip',
            dependencies='https://github.com/Likely7/Veyra-NRVideo/releases/download/v' + version + '/Veyra-' + version + '-dependency-source.zip' if version == '2.0.3' else 'https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.0/Veyra-2.0.0-dependency-source.zip',
            instructions='docs/BUILD_' + version + '.md')
    if not args.no_archive:
        manifest['sourceSnapshot'] = source_snapshot(args.output, args.label, digest(stage / 'veyra_qml_ui.exe'))
    (stage / 'package-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    if args.no_archive:
        print(stage)
        return
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in [r['path'] for r in records] + ['package-manifest.json']:
            z.write(stage / name, stage.name + '/' + name)
    result = verify_zip(archive)
    archive.with_suffix('.zip.sha256').write_text(result['sha256'] + '  ' + archive.name + '\n', encoding='ascii')
    (args.output / 'package-audit.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
