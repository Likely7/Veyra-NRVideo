"""Negative archive tests: corrupted, unlisted, private and unsafe payloads."""
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('package', Path(__file__).resolve().parents[1] / 'package-qml-release.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)
OUT = Path('E:/项目/Veyra/tests/release-2.0.0-20261002/package-validator')
OUT.mkdir(parents=True, exist_ok=False)

class PackageTests(unittest.TestCase):
    def archive(self, *, extra=None, mutate=None, omit=None, symlink=False):
        payload = {name: b'test bytes' for name in PACKAGE.REQUIRED if name != omit}
        records = [dict(path=n, size=len(b), sha256=hashlib.sha256(b).hexdigest()) for n, b in payload.items()]
        if mutate:
            mutate(records)
        path = OUT / (self.id().split('.')[-1] + '.zip')
        with zipfile.ZipFile(path, 'x') as z:
            for name, data in payload.items():
                z.writestr('Veyra/' + name, data)
            z.writestr('Veyra/package-manifest.json', json.dumps(dict(files=records)))
            if extra:
                z.writestr(extra, b'unlisted')
            if symlink:
                info = zipfile.ZipInfo('Veyra/link')
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(info, '../outside')
        return path

    def test_valid(self):
        self.assertEqual(PACKAGE.verify_zip(self.archive())['status'], 'pass')

    def test_corrupt_hash(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(mutate=lambda r: r[0].update(sha256='0'*64)))

    def test_wrong_size(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(mutate=lambda r: r[0].update(size=999)))

    def test_missing_license(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(omit='licenses/qt/LGPL-3.0-only.txt'))

    def test_unlisted(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(extra='Veyra/secret.txt'))

    def test_case_collision(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(extra='Veyra/QT6CORE.DLL'))

    def test_symlink(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(symlink=True))

    def test_traversal(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(extra='Veyra/../outside'))

    def test_self_reference(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_zip(self.archive(mutate=lambda r: r.append(dict(path='package-manifest.json',size=0,sha256='0'*64))))

    def test_private_and_development_files(self):
        for name in ('runtime_local/user-data-2.0.0/secret.json', 'logs/auth.log',
                     'Qt6Test.dll', 'probe.exe', 'models/a.onnx', 'CON.txt', 'foo. /bar', '../x'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                PACKAGE.validate_payload(name)

if __name__ == '__main__':
    unittest.main(verbosity=2)
