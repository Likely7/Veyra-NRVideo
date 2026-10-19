"""Shared helpers for the #48 network tests: start/stop slow_server.py, run the C++ test program,
parse its RESULT line. Standard library only."""
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

BASE = Path('E:/项目/Veyra')
TASK = 'claude-network-buffer-48-20261017'
APP = BASE / 'tests' / TASK / 'app'
MEDIA = BASE / 'tests' / TASK / 'media'
EXE = APP / 'veyra_network_buffer_tests.exe'
SERVER = Path(__file__).resolve().parent / 'slow_server.py'
FFMPEG = Path('C:/Users/123/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.1.1-full_build/bin/ffmpeg.exe')


def clean_env(extra=None):
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
    tmp = BASE / 'tmp' / TASK / 'run'
    tmp.mkdir(parents=True, exist_ok=True)
    env.update(TEMP=str(tmp), TMP=str(tmp))
    if extra:
        env.update({k: str(v) for k, v in extra.items()})
    return env


class Server:
    """slow_server.py as a child process; port picked by the OS and read back from a file."""

    def __init__(self, logdir, root=MEDIA, **flags):
        self.logdir = Path(logdir)
        self.logdir.mkdir(parents=True, exist_ok=True)
        self.root = root
        self.flags = flags
        self.proc = None
        self.port = 0

    def __enter__(self):
        portfile = self.logdir / 'server.port'
        if portfile.exists():
            portfile.unlink()
        cmd = [sys.executable, '-I', str(SERVER), '--root', str(self.root), '--port-file', str(portfile),
               '--log', str(self.logdir / 'server.jsonl'), '--ffmpeg', str(FFMPEG)]
        for key, value in self.flags.items():
            flag = '--' + key.replace('_', '-')
            if isinstance(value, list):
                for item in value:
                    cmd += [flag, str(item)]
            elif value is True:
                cmd.append(flag)
            elif value is not None and value is not False:
                cmd += [flag, str(value)]
        self.out = (self.logdir / 'server.out').open('wb')
        self.proc = subprocess.Popen(cmd, stdout=self.out, stderr=subprocess.STDOUT, creationflags=0x08000000)
        deadline = time.time() + 20
        while time.time() < deadline:
            if portfile.exists() and portfile.read_text().strip():
                self.port = int(portfile.read_text().strip())
                self.control = int(Path(str(portfile) + '.control').read_text().strip())
                break
            if self.proc.poll() is not None:
                raise RuntimeError('server exited: ' + (self.logdir / 'server.out').read_text(errors='replace'))
            time.sleep(0.05)
        if not self.port:
            raise RuntimeError('server did not start')
        return self

    def __exit__(self, *exc):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.out.close()

    def url(self, name):
        return f'http://127.0.0.1:{self.port}/{name}'

    def stats(self):
        with urllib.request.urlopen(f'http://127.0.0.1:{self.control}/', timeout=5) as r:
            return json.loads(r.read())


def run_exe(mode, target, outdir, options=None, env=None, timeout=280):
    """Runs veyra_network_buffer_tests; returns (returncode, RESULT json or None, seconds)."""
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    cmd = [str(EXE), mode, str(target), str(outdir)] + [f'{k}={v}' for k, v in (options or {}).items()]
    start = time.time()
    with (outdir / 'stdout.txt').open('wb') as out:
        try:
            p = subprocess.run(cmd, cwd=str(APP), env=clean_env(env), stdout=out, stderr=subprocess.STDOUT,
                               timeout=timeout, creationflags=0x08000000)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = 124
    seconds = time.time() - start
    result = None
    for line in (outdir / 'stdout.txt').read_text(encoding='utf8', errors='replace').splitlines():
        if line.startswith('RESULT '):
            result = json.loads(line[7:])
    return rc, result, seconds
