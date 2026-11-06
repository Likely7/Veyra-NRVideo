"""Exercise production NVENC exports and independently check random access.

Requires a built veyra_export_probe, ffmpeg/ffprobe on PATH, and a disposable
output root. All media/logs/TEMP stay under that root; no GUI or real profiles.
Each child process has a 240-second limit. Exit 1 means a failed assertion/run.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time


def sync_samples(path):
    """Read the MP4 stss directly, without a demuxer's inferred key flags."""
    data = path.read_bytes()

    def walk(start, end):
        pos = start
        while pos + 8 <= end:
            size, kind = struct.unpack_from('>I4s', data, pos)
            header = 8
            if size == 1:
                size = struct.unpack_from('>Q', data, pos + 8)[0]
                header = 16
            if size == 0:
                size = end - pos
            assert header <= size <= end - pos, (pos, size, kind)
            if kind == b'stss':
                count = struct.unpack_from('>I', data, pos + header + 4)[0]
                return list(struct.unpack_from('>' + str(count) + 'I', data, pos + header + 8))
            if kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
                found = walk(pos + header, pos + size)
                if found is not None:
                    return found
            pos += size
        return None

    return walk(0, len(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, required=True, help='DLL search directory, read only')
    parser.add_argument('--shader-dir', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--cases', nargs='+', help='Optional names from the default matrix')
    args = parser.parse_args()
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    # A repeat must use a new root; do not overwrite existing export evidence.
    assert not (root / 'export-probe.exe').exists(), 'Use a fresh output root'
    exe = root / 'export-probe.exe'
    shutil.copy2(args.probe, exe)
    shutil.copytree(args.shader_dir, root / 'shaders')
    (root / 'tmp').mkdir()
    env = os.environ.copy()
    env['PATH'] = str(args.runtime_dir.resolve()) + os.pathsep + str(args.probe.resolve().parent) + os.pathsep + env['PATH']
    env['TEMP'] = env['TMP'] = str(root / 'tmp')
    ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
    assert ffmpeg and ffprobe, 'ffmpeg and ffprobe must be on PATH'
    commands = []

    def run(label, command):
        start = time.monotonic()
        result = subprocess.run(list(map(str, command)), cwd=root, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
        (root / (label + '.log')).write_bytes(result.stdout)
        commands.append({'label': label, 'args': list(map(str, command)),
                         'code': result.returncode, 'seconds': time.monotonic() - start})
        assert result.returncode == 0, f'{label}: exit {result.returncode}; see log'
        return result.stdout.decode('utf-8', 'replace')

    cases = [(codec, fps, rc, ext, False) for codec in ('h264', 'hevc')
             for fps in (30, 60) for rc in ('cq',) for ext in ('mp4', 'mkv')]
    cases += [(codec, 30, rc, 'mp4', False) for codec in ('h264', 'hevc') for rc in ('cbr', 'vbr')]
    cases += [('hevc', 30, 'cq', ext, True) for ext in ('mp4', 'mkv')]
    results = []
    sources = {}
    for codec, fps, rc, ext, hdr in cases:
        name = f'{codec}-{fps}-{rc}-{ext}' + ('-hdr10' if hdr else '')
        if args.cases and name not in args.cases:
            continue
        record = {'case': name, 'pass': False}
        try:
            key = (fps, hdr)
            if key not in sources:
                media = root / (f'source-{fps}' + ('-hdr10' if hdr else '') + '.mp4')
                command = [ffmpeg, '-v', 'error', '-y', '-f', 'lavfi', '-i',
                           f'testsrc2=size=640x360:rate={fps}', '-frames:v', '270']
                if hdr:
                    command += ['-c:v', 'libx265', '-preset', 'fast', '-pix_fmt', 'yuv420p10le',
                                '-x265-params', 'pools=2:frame-threads=2:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc', '-color_primaries', 'bt2020',
                                '-color_trc', 'smpte2084', '-colorspace', 'bt2020nc']
                else:
                    command += ['-c:v', 'libx264', '-preset', 'fast', '-pix_fmt', 'yuv420p']
                run(media.stem, command + [media])
                if hdr:
                    source_info = json.loads(run(media.stem + '-metadata', [ffprobe, '-v', 'error',
                        '-select_streams', 'v:0', '-show_streams', '-of', 'json', media]))['streams'][0]
                    assert source_info['pix_fmt'] == 'yuv420p10le', source_info
                    assert source_info.get('color_transfer') == 'smpte2084', source_info
                sources[key] = media
            out = root / (name + '.' + ext)
            command = [exe, sources[key], out, '--max-frames', '270', '--rate-control', rc]
            if codec == 'hevc':
                command += ['--hevc']
            if rc != 'cq':
                command += ['--bitrate-mbps', '8']
            export_log = run(name + '-export', command)
            assert 'encoder=NVIDIA-NVENC' in export_log, 'Fallback cannot verify NVENC'
            record['idr_log'] = re.findall(r'random access[^\r\n]*', export_log)
            info = json.loads(run(name + '-packets', [ffprobe, '-v', 'error', '-select_streams', 'v:0',
                              '-show_packets', '-show_streams', '-of', 'json', out]))
            packets = info['packets']
            record['packets'] = len(packets)
            record['keys'] = [i for i, p in enumerate(packets) if 'K' in p['flags']]
            record['stss'] = sync_samples(out) if ext == 'mp4' else None
            record['stream'] = {k: info['streams'][0].get(k) for k in ('codec_name', 'profile', 'pix_fmt', 'color_transfer')}
            assert len(packets) == 270, record
            assert all(abs(float(p['pts_time']) - i / fps) < .0011 for i, p in enumerate(packets)), 'PTS'
            assert all(p['pts'] == p['dts'] for p in packets), 'Unexpected reordering'
            assert record['keys'] == [0, 120, 240], record
            if ext == 'mp4':
                assert record['stss'] == [1, 121, 241], record
            if hdr:
                assert record['stream']['profile'] == 'Main 10', record
                assert record['stream']['pix_fmt'] == 'yuv420p10le', record
                assert record['stream']['color_transfer'] == 'smpte2084', record
            raw = root / (name + ('.h265' if codec == 'hevc' else '.h264'))
            run(name + '-annexb', [ffmpeg, '-v', 'error', '-y', '-i', out, '-map', '0:v:0', '-c', 'copy',
                                  '-bsf:v', codec + '_mp4toannexb', '-f', codec, raw])
            nal_counts = {}
            for nal in re.split(b'\x00\x00\x00\x01|\x00\x00\x01', raw.read_bytes())[1:]:
                if nal:
                    kind = ((nal[0] >> 1) & 63) if codec == 'hevc' else nal[0] & 31
                    nal_counts[kind] = nal_counts.get(kind, 0) + 1
            record['nal_counts'] = nal_counts
            idrs = sum(nal_counts.get(k, 0) for k in ((19, 20) if codec == 'hevc' else (5,)))
            assert idrs == 3 and (codec != 'hevc' or nal_counts.get(21, 0) == 0), record
            def hashes(text):
                return [line.rsplit(',', 1)[-1].strip() for line in text.splitlines() if line and not line.startswith('#')]
            all_hashes = hashes(run(name + '-decode', [ffmpeg, '-v', 'error', '-xerror', '-i', out,
                                '-map', '0:v:0', '-f', 'framemd5', '-']))
            assert len(all_hashes) == 270, len(all_hashes)
            record['seeks'] = []
            for frame in (150, 255):
                seconds = frame / fps
                seek_packets = json.loads(run(name + f'-index-{frame}', [ffprobe, '-v', 'error',
                    '-read_intervals', f'{seconds}%+#1', '-select_streams', 'v:0', '-show_packets', '-of', 'json', out]))['packets']
                assert seek_packets and 'K' in seek_packets[0]['flags'], seek_packets
                assert abs(float(seek_packets[0]['pts_time']) - (120 if frame == 150 else 240) / fps) < .0011, seek_packets
                seek_hashes = hashes(run(name + f'-seek-{frame}', [ffmpeg, '-v', 'error', '-xerror',
                    '-ss', str(seconds), '-i', out, '-map', '0:v:0', '-frames:v', '1', '-f', 'framemd5', '-']))
                assert seek_hashes == [all_hashes[frame]], (frame, seek_hashes, all_hashes[frame])
                record['seeks'].append({'frame': frame, 'pts': seconds, 'hash': all_hashes[frame],
                                        'index_pts': seek_packets[0]['pts_time']})
            record['sha256'] = hashlib.sha256(out.read_bytes()).hexdigest()
            record['pass'] = True
        except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
            record['error'] = str(error)
        results.append(record)
        print(name, 'PASS' if record['pass'] else 'FAIL', record.get('error', ''), flush=True)
    assert results, 'No matching cases'
    final = {'probe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'results': results, 'commands': commands}
    (root / 'RESULTS.json').write_text(json.dumps(final, ensure_ascii=False, indent=2), encoding='utf-8')
    return 0 if all(row['pass'] for row in results) else 1


if __name__ == '__main__':
    sys.exit(main())
