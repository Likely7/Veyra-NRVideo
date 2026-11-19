"""Generate public synthetic source fixtures; no captured media is required."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--ffmpeg', default='ffmpeg')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for name, extra in [('single', ['-frames:v', '1', '-tune', 'zerolatency']),
                    ('reorder', ['-frames:v', '12', '-bf', '2'])]:
    subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                    '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=60',
                    '-c:v', 'libx264', '-preset', 'fast', '-color_primaries', 'bt709',
                    '-color_trc', 'bt709', '-colorspace', 'bt709', *extra,
                    '-f', 'h264', str(args.output / (name + '.h264'))],
                   check=True, timeout=60)
data = (args.output / 'single.h264').read_bytes()
starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', data))
config, picture = bytearray(), bytearray()
for i, match in enumerate(starts):
    end = starts[i + 1].start() if i + 1 < len(starts) else len(data)
    chunk = data[match.start():end]
    (config if data[match.end()] & 31 in (7, 8) else picture).extend(chunk)
if not config or not picture:
    raise RuntimeError('Encoder did not emit required SPS/PPS and picture NALs')
(args.output / 'single-config.bin').write_bytes(config)
(args.output / 'single-au.bin').write_bytes(picture)
print('Generated synthetic H.264 configuration, first AU and reordered stream')

for name, depth, primaries, transfer, matrix in [
    ('hevc', 'yuv420p', 'bt709', 'bt709', 'bt709'),
    ('hdr10', 'yuv420p10le', 'bt2020', 'smpte2084', 'bt2020nc'),
]:
    subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y',
                    '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=60',
                    '-c:v', 'libx265', '-preset', 'ultrafast', '-tune', 'zerolatency', '-frames:v', '1',
                    '-pix_fmt', depth, '-color_primaries', primaries, '-color_trc', transfer,
                    '-colorspace', matrix, '-x265-params', 'log-level=error:pools=1:frame-threads=1',
                    '-f', 'hevc', str(args.output / (name + '.h265'))], check=True, timeout=60)
    data = (args.output / (name + '.h265')).read_bytes()
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', data))
    config, picture = bytearray(), bytearray()
    for i, match in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(data)
        chunk = data[match.start():end]
        (config if (data[match.end()] >> 1) & 63 in (32, 33, 34) else picture).extend(chunk)
    if not config or not picture:
        raise RuntimeError('Missing HEVC configuration or picture NALs')
    (args.output / (name + '-config.bin')).write_bytes(config)
    (args.output / (name + '-au.bin')).write_bytes(picture)
    print('Generated synthetic', name, depth, transfer, 'VPS/SPS/PPS and first AU')
