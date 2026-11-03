"""CPU-only DLL contract for the RX7000 selected-context NVAPI LUID repair.

Compile tests/integration/Rdna3NvapiFakeCuda.cpp into nvcuda.dll and, with
VEYRA_RDNA3_NR_CALLER, into dlssnr-contract.dll. These are private fixtures,
never product components. Run once with the old and once with the repaired
zluda_nvapi.dll, passing the unchanged packaged nvapi64.dll as --forwarder.
"""
import argparse
import ctypes as c
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def check(directory):
    directory = directory.resolve()
    os.add_dll_directory(str(directory))
    # Clear optional architecture overrides so the original caller-sensitive
    # identity is tested, without changing the parent/system environment.
    for key in ('ZLUDA_NVAPI_GPU_ARCH', 'ZLUDA_NVAPI_GPU_ARCH_NEURAL'):
        os.environ.pop(key, None)
    cuda = c.CDLL(str(directory / 'nvcuda.dll'))
    cuda.testState.argtypes = [c.c_int] * 4
    cuda.testState.restype = None
    nvapi = c.CDLL(str(directory / 'nvapi64.dll'))
    query = nvapi.nvapi_QueryInterface
    query.argtypes, query.restype = [c.c_uint32], c.c_void_p

    class Logical(c.Structure):
        _fields_ = [('version', c.c_uint32), ('luid', c.c_void_p), ('count', c.c_uint32),
                    ('handles', c.c_void_p * 64), ('reserved', c.c_uint32 * 8)]

    info = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Logical))(query(0x842b066e))
    rows = []

    def request(count, ordinal, status=0, luid_error=0):
        cuda.testState(count, ordinal, status, luid_error)
        luid = c.c_uint64(0xdeadbeefdeadbeef)
        data = Logical()
        data.version = c.sizeof(Logical) | (1 << 16)
        data.luid = c.addressof(luid)
        result = info(1, c.byref(data))
        return result, luid.value, data.count

    cases = [('iGPU first, selected dGPU', 2, 1, 0, 0, 0x10100),
             ('selected first adapter', 2, 0, 0, 0, 0x12004),
             ('single device, no context', 1, 0, 201, 0, 0x12004),
             ('multiple devices, no context rejected', 2, 0, 201, 0, None),
             ('context failure rejected with one device', 1, 0, 999, 0, None),
             ('context failure rejected with multiple devices', 2, 0, 999, 0, None),
             ('no devices rejected', 0, 0, 201, 0, None),
             ('LUID failure reported', 2, 1, 0, 999, None),
             ('invalid context device rejected', 2, 5, 0, 0, None)]
    for name, count, ordinal, status, failure, expected in cases:
        actual = request(count, ordinal, status, failure)
        ok = (actual == (0, expected, 1)) if expected is not None else (actual[0] != 0 and actual[1] == 0xdeadbeefdeadbeef)
        rows.append({'name': name, 'pass': ok, 'actual': actual})

    def threaded(ordinal):
        expected = 0x12004 if ordinal == 0 else 0x10100
        return all(request(2, ordinal) == (0, expected, 1) for _ in range(32))

    with ThreadPoolExecutor(max_workers=2) as pool:
        threaded_results = list(pool.map(threaded, (0, 1)))
    rows.append({'name': 'two concurrent selected contexts, 64 queries', 'pass': all(threaded_results), 'actual': threaded_results})
    caller = c.CDLL(str(directory / 'dlssnr-contract.dll'))
    caller.testNeuralArchitecture.argtypes = [c.c_void_p, c.POINTER(c.c_uint32)]
    caller.testNeuralArchitecture.restype = c.c_int
    architecture = c.c_uint32()
    result = caller.testNeuralArchitecture(c.cast(query, c.c_void_p), c.byref(architecture))
    rows.append({'name': 'NR caller retains upstream Blackwell identity', 'pass': result == 0 and architecture.value == 0x1b0,
                 'actual': [result, architecture.value]})
    rows.append({'name': 'null data remains rejected', 'pass': info(1, None) != 0})
    # A caller may omit the optional adapter LUID buffer.
    empty = Logical()
    empty.version = c.sizeof(Logical) | (1 << 16)
    rows.append({'name': 'optional LUID pointer remains supported', 'pass': info(1, c.byref(empty)) == 0 and empty.count == 1})
    for row in rows:
        print(('PASS ' if row['pass'] else 'FAIL ') + row['name'] + ' ' + str(row.get('actual', '')), flush=True)
    result = {'mode': 'CPU fixture, not Radeon inference', 'pass': all(row['pass'] for row in rows), 'checks': rows,
              'files': [{'path': p.name, 'size': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                        for p in directory.glob('*.dll')]}
    (directory / 'RESULTS.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return 0 if result['pass'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--nvapi', type=Path)
    parser.add_argument('--forwarder', type=Path)
    parser.add_argument('--cuda-fixture', type=Path)
    parser.add_argument('--nr-caller', type=Path)
    parser.add_argument('--child', action='store_true')
    args = parser.parse_args()
    if args.child:
        return check(args.directory)
    args.directory.mkdir(parents=True, exist_ok=True)
    assert not (args.directory / 'RESULTS.json').exists(), 'Use a fresh evidence directory'
    for source, name in ((args.nvapi, 'zluda_nvapi.dll'), (args.forwarder, 'nvapi64.dll'),
                         (args.cuda_fixture, 'nvcuda.dll'), (args.nr_caller, 'dlssnr-contract.dll')):
        assert source and source.is_file(), source
        shutil.copyfile(source, args.directory / name)
    env = os.environ.copy()
    (args.directory / 'tmp').mkdir(exist_ok=True)
    env['TEMP'] = env['TMP'] = str((args.directory / 'tmp').resolve())
    result = subprocess.run([sys.executable, '-X', 'utf8', '-B', str(Path(__file__).resolve()),
                             '--child', '--directory', str(args.directory.resolve())], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (args.directory / 'console.log').write_bytes(result.stdout)
    print(result.stdout.decode('utf-8', 'replace'))
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
