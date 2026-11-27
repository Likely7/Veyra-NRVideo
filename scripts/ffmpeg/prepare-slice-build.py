"""Generate a matching FFmpeg rebuild script; does not install/replace DLLs.

Run in a VS x64 developer environment, then invoke the emitted script with
the supplied MSYS bash. The source must already have ps5-h264-slices.patch.
"""
import argparse
import ctypes
import os
from pathlib import Path
import shlex

p = argparse.ArgumentParser()
for name in ('reference-bin', 'source', 'build', 'prefix', 'msys', 'msvc-bin', 'nasm', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--zlib-prefix', type=Path,
               help='Enable the existing static zlib dependency for compressed Matroska subtitles')
a = p.parse_args()
if '#define MAX_SLICES 256' not in (a.source / 'libavcodec/h264dec.h').read_text():
    raise SystemExit('Apply ps5-h264-slices.patch to the matching private FFmpeg source tree first')
with os.add_dll_directory(str(a.reference_bin.resolve())):
    lib = ctypes.CDLL(str((a.reference_bin / 'avcodec-63.dll').resolve()))
    lib.avcodec_configuration.restype = ctypes.c_char_p
    lib.avcodec_license.restype = ctypes.c_char_p
    if lib.avcodec_license().decode() != 'LGPL version 2.1 or later':
        raise SystemExit('Unexpected reference license')
    config = lib.avcodec_configuration().decode()
def posix(path):
    # Preserve an explicitly supplied junction alias. Some MinGW pkg-config
    # builds corrupt non-ASCII paths before MSVC receives the include flags.
    value = path.absolute().as_posix()
    return '/' + value[0].lower() + value[2:] if len(value) > 2 and value[1] == ':' else value
args = shlex.split(config)
ar = a.msys / 'usr/share/automake-1.18/ar-lib'
args = [('--prefix=' + a.prefix.absolute().as_posix() if x.startswith('--prefix=') else
         '--ar=' + shlex.quote(posix(ar)) + ' lib.exe' if x.startswith('--ar=') else x) for x in args]
extra_environment = ''
if a.zlib_prefix:
    pc = a.zlib_prefix.absolute() / 'lib/pkgconfig/zlib.pc'
    if not pc.is_file() or not (a.zlib_prefix / 'include/zlib.h').is_file():
        raise SystemExit('zlib-prefix must contain matching headers and pkg-config metadata')
    args = ['--enable-zlib' if x == '--disable-zlib' else x for x in args]
    if '--enable-zlib' not in args:
        args.append('--enable-zlib')
    # The recorded pkg-config is a MinGW executable: its path-list separator
    # is ';'. Keep any existing path (dav1d) and do not modify global settings.
    extra_environment = ('export PKG_CONFIG_PATH=' + shlex.quote(pc.parent.as_posix())
                         + '${PKG_CONFIG_PATH:+;"$PKG_CONFIG_PATH"}\n')
a.build.mkdir(parents=True, exist_ok=True)
a.output.parent.mkdir(parents=True, exist_ok=True)
search = ':'.join(posix(x) for x in (a.msvc_bin, a.msys / 'usr/bin', a.nasm))
script = ('#!/bin/bash\nset -eu\nexport PATH=' + shlex.quote(search) + ':"$PATH"\n'
          + extra_environment
          + 'cd ' + shlex.quote(posix(a.build)) + '\n'
          + shlex.quote(posix(a.source / 'configure')) + ' ' + shlex.join(args)
          + '\nmake -j8\nmake install\n')
a.output.write_text(script, encoding='utf-8', newline='\n')
a.output.with_suffix('.reference-config.txt').write_text(config, encoding='utf-8')
print(a.output)
