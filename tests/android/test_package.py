#!/usr/bin/env python3
"""Test Android packaging without an SDK or network access."""

import importlib.util
import pathlib
import tempfile
import unittest
import zipfile

HELPER = pathlib.Path(__file__).resolve().parents[2] / 'scripts/android/prepare.py'
spec = importlib.util.spec_from_file_location('prepare_android', HELPER)
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PackagingTest(unittest.TestCase):
    def test_assets_and_apk_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            database = build / 'prefix/share/radare2/test/opcodes/x86.sdb'
            database.parent.mkdir(parents=True)
            database.write_bytes(b'database\0payload')
            prepare.assets(build)
            assets = build / 'package/assets/radare2'
            first_id = (assets / 'bundle-id').read_text()
            self.assertEqual((assets / 'files.txt').read_text(),
                             'share/radare2/test/opcodes/x86.sdb\n')
            obsolete = assets / 'share/radare2/obsolete.sdb'
            obsolete.write_bytes(b'old')
            database.write_bytes(b'updated database')
            prepare.assets(build)
            self.assertFalse(obsolete.exists())
            self.assertNotEqual(first_id, (assets / 'bundle-id').read_text())

            library = build / 'prefix/lib/libr_core.so'
            library.parent.mkdir()
            # Minimal ELF headers sufficient for the ABI check.
            arm64 = b'\x7fELF' + b'\0' * 14 + (183).to_bytes(2, 'little')
            library.write_bytes(arm64)
            apk = build / 'iaito.apk'

            def package(lib=arm64, omit=None, stale=False):
                with zipfile.ZipFile(apk, 'w') as archive:
                    if lib is not None:
                        archive.writestr('lib/arm64-v8a/libr_core.so', lib)
                    for path in assets.rglob('*'):
                        if path.is_file() and path.name != omit:
                            content = b'stale' if stale else path.read_bytes()
                            archive.writestr(path.relative_to(build / 'package').as_posix(), content)

            package()
            prepare.verify_apk(build, apk, 'arm64-v8a')
            for options, message in [
                ({'lib': None}, 'missing'),
                ({'lib': arm64[:18] + (62).to_bytes(2, 'little')}, 'wrong architecture'),
                ({'omit': 'x86.sdb'}, 'missing'),
                ({'stale': True}, 'stale'),
            ]:
                package(**options)
                with self.assertRaisesRegex(SystemExit, message):
                    prepare.verify_apk(build, apk, 'arm64-v8a')

    def test_toolchain_change_requires_new_build(self):
        with tempfile.TemporaryDirectory() as directory:
            args = [directory, '/ndk/bin', 'aarch64-linux-android', '28',
                    'aarch64', 'aarch64', 'repository', 'HEAD']
            prepare.cross_file(args)
            prepare.cross_file(args)
            args[3] = '29'
            with self.assertRaisesRegex(SystemExit, 'fresh BUILD_DIR'):
                prepare.cross_file(args)


if __name__ == '__main__':
    unittest.main()
