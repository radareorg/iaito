#!/usr/bin/env python3
"""Offline regression checks for the iOS cross-build and universal bundle verifier."""

import importlib.util
import json
import os
import pathlib
import plistlib
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepare_ipad', ROOT / 'dist/ios/prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class IpadPackagingTest(unittest.TestCase):
    def test_qt_checks_target_and_minimum_version(self):
        with tempfile.TemporaryDirectory() as directory:
            qt = pathlib.Path(directory)
            config = qt / 'mkspecs/qconfig.pri'
            config.parent.mkdir()
            config.write_text('QMAKE_IOS_DEPLOYMENT_TARGET = 17\n')
            with patch.object(prepare, 'output', side_effect=['6.10.3', 'macx-ios-clang']):
                prepare.check_qt(qt, '17.0')
            with patch.object(prepare, 'output', side_effect=['6.10.3', 'macx-ios-clang']):
                with self.assertRaisesRegex(SystemExit, 'at least 17'):
                    prepare.check_qt(qt, '16.0')
            with patch.object(prepare, 'output', side_effect=['6.10.3', 'macx-clang']):
                with self.assertRaisesRegex(SystemExit, 'not desktop'):
                    prepare.check_qt(qt, '17.0')

    def test_cross_qmake_uses_host_binary_and_preserves_argument_boundaries(self):
        with tempfile.TemporaryDirectory(prefix='qt with spaces ') as directory:
            root = pathlib.Path(directory)
            qt = root / 'ios'
            (qt / 'bin').mkdir(parents=True)
            (root / 'macos/bin').mkdir(parents=True)
            (root / 'macos/bin/qmake6').touch()
            config = qt / 'bin/target_qt.conf'
            config.write_text('[Paths]\nHostPrefix=../../macos\nHostBinaries=bin\n')
            self.assertEqual(prepare.qmake_command(qt),
                             [(root / 'macos/bin/qmake6').resolve(), '-qtconf', config])

    def test_static_sdb_patch_preserves_upstream_conditionals(self):
        for guarded in (False, True):
            with self.subTest(guarded=guarded), tempfile.TemporaryDirectory() as directory:
                source = pathlib.Path(directory)
                sdb = source / 'subprojects/sdb/meson.build'
                sdb.parent.mkdir(parents=True)
                sdb.write_text('# original SDB build\n')
                target = """libsdb_install = custom_target('libr2sdb',
  input : libsdb_dynamic,
  output : 'libsdb' + lib_extension,
  command : copyinout,
  install : true,
  install_dir : get_option('libdir'))
"""
                if guarded:
                    target = "if get_option('default_library') != 'static'\n" + target + 'endif\n'
                meson = source / 'meson.build'
                meson.write_text("libsdb_dynamic = libsdb_sp.get_variable('libsdb').get_shared_lib()\n"
                                 "sdb_exe = libsdb_sp.get_variable('sdb_exe')\n" + target
                                 + "sdb_gen_cmd_cgen = [sdb_exe]\n")
                prepare.prepare_radare2(source)
                first = meson.read_text()
                self.assertIn("get_variable('sdb_build_exe')", first)
                self.assertNotIn('get_shared_lib()', first)
                self.assertIn('libsdb_install = libsdb_static', first)
                self.assertEqual(first.count('endif'), int(guarded))
                prepare.prepare_radare2(source)
                self.assertEqual(meson.read_text(), first)
                self.assertEqual(sdb.read_text().count('native: true'), 1)

    def test_cross_file_is_stable_and_rejects_reused_toolchains(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            with patch.object(prepare, 'output', return_value='/Xcode/toolchain'):
                cross = prepare.cross_file(build, 'iphoneos', 'arm64', '17.0')
                # An unchanged cross file must not retrigger Meson configuration
                # and regenerate all of radare2's headers on every invocation.
                os.utime(cross, ns=(1, 1))
                prepare.cross_file(build, 'iphoneos', 'arm64', '17.0')
                self.assertEqual(cross.stat().st_mtime_ns, 1)
                with self.assertRaisesRegex(SystemExit, 'fresh BUILD_DIR'):
                    prepare.cross_file(build, 'iphoneos', 'arm64', '18.0')

    def test_bundle_verifier_rejects_stale_and_missing_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            app, stage = root / 'iaito.app', root / 'stage'
            app.mkdir()
            info = {'UIDeviceFamily': [1, 2], 'CFBundleIdentifier': 'org.radare.iaito',
                    'CFBundleVersion': '3', 'CFBundleExecutable': 'iaito',
                    'UILaunchStoryboardName': 'LaunchScreen'}
            (app / 'Info.plist').write_bytes(plistlib.dumps(info))
            (app / 'LaunchScreen.storyboardc').mkdir()
            for name in ('Assets.car', 'PrivacyInfo.xcprivacy', 'iaito'):
                (app / name).touch()
            relative = pathlib.Path('radare2/share/radare2/test/opcodes/arm.sdb')
            for base in (app, stage):
                path = base / relative
                path.parent.mkdir(parents=True)
                path.write_bytes(b'database')

            def output(*args):
                return ('platform IOS\n' if 'vtool' in args else
                        'iaito:\n\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0)')

            with patch.object(prepare, 'output', side_effect=output):
                prepare.verify_app(app, stage, 'iphoneos', 'org.radare.iaito', '3')
                for family in (None, [1], [2], [1, 2, 3]):
                    with self.subTest(family=family):
                        info.pop('UIDeviceFamily', None)
                        if family is not None:
                            info['UIDeviceFamily'] = family
                        (app / 'Info.plist').write_bytes(plistlib.dumps(info))
                        with self.assertRaisesRegex(SystemExit, 'both iPhone and iPad'):
                            prepare.verify_app(app, stage, 'iphoneos', 'org.radare.iaito', '3')
                info['UIDeviceFamily'] = [1, 2]
                (app / 'Info.plist').write_bytes(plistlib.dumps(info))
                with self.assertRaisesRegex(SystemExit, 'bundle identifier'):
                    prepare.verify_app(app, stage, 'iphoneos', 'org.example.iaito', '3')
                with self.assertRaisesRegex(SystemExit, 'wrong platform'):
                    prepare.verify_app(app, stage, 'iphonesimulator', 'org.radare.iaito', '3')
                (app / relative).write_bytes(b'stale')
                with self.assertRaisesRegex(SystemExit, 'stale radare2 data'):
                    prepare.verify_app(app, stage, 'iphoneos', 'org.radare.iaito', '3')
                (app / relative).write_bytes(b'database')
                with patch.object(prepare, 'output', side_effect=[
                    'platform IOS\n', 'iaito:\n\t/opt/homebrew/lib/libr_core.dylib (version 1.0)',
                ]):
                    with self.assertRaisesRegex(SystemExit, 'unbundled library'):
                        prepare.verify_app(app, stage, 'iphoneos', 'org.radare.iaito', '3')
                (app / 'Assets.car').unlink()
                with self.assertRaisesRegex(SystemExit, 'missing Assets.car'):
                    prepare.verify_app(app, stage, 'iphoneos', 'org.radare.iaito', '3')

    def test_qmake_values_preserve_spaces_and_reject_expansion(self):
        self.assertEqual(prepare.qmake_quote('/a path/Qt'), '"/a path/Qt"')
        for value in ('$(shell command)', 'a\nb'):
            with self.assertRaises(SystemExit):
                prepare.qmake_quote(value)

    def test_zlib_verifier_rejects_bundled_implementations(self):
        with patch.object(prepare, 'output', return_value='_inflate\n_deflateEnd\n'):
            with self.assertRaisesRegex(SystemExit, 'overrides system zlib'):
                prepare.verify_zlib('iaito')
        with patch.object(prepare, 'output', return_value='_r_inflate\n_inflatew\n'):
            prepare.verify_zlib('iaito')

    def test_sandbox_verifier_rejects_process_creation(self):
        with patch.object(prepare, 'output', return_value='_fork\n_popen\n_execve\n'):
            with self.assertRaisesRegex(SystemExit, 'imports subprocess/debugging APIs'):
                prepare.verify_sandbox('iaito')
        with patch.object(prepare, 'output', return_value='_socket\n_open\n'):
            prepare.verify_sandbox('iaito')

    def test_archive_export_uses_team_and_exports_without_uploading(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            (build / 'package.json').write_text(json.dumps({
                'stage': str(build / 'stage'), 'bundle': 'org.example.iaito', 'number': '7',
            }))
            with patch.dict(os.environ, {'DEVELOPMENT_TEAM': 'TESTTEAM',
                                         'EXPORT_OPTIONS_PLIST': ''}), \
                    patch.object(prepare, 'run') as run, \
                    patch.object(prepare, 'verify_app'):
                prepare.build_app(ROOT, build, 'export', '8')
            archive, export = [call.args for call in run.call_args_list]
            self.assertIn('DEVELOPMENT_TEAM=TESTTEAM', archive)
            self.assertEqual(archive[-1], 'archive')
            self.assertEqual(export[1], '-exportArchive')
            options = plistlib.loads((build / 'ExportOptions.plist').read_bytes())
            self.assertEqual(options['method'], 'app-store-connect')
            self.assertEqual(options['destination'], 'export')
            self.assertEqual(options['teamID'], 'TESTTEAM')

    def test_unsigned_build_does_not_request_provisioning(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            (build / 'package.json').write_text(json.dumps({
                'stage': str(build / 'stage'), 'bundle': 'org.radare.iaito', 'number': '1',
            }))
            with patch.dict(os.environ, {'DEVELOPMENT_TEAM': ''}), \
                    patch.object(prepare, 'run') as run, \
                    patch.object(prepare, 'verify_app'):
                prepare.build_app(ROOT, build, 'build', '8')
            args = run.call_args.args
            self.assertIn('CODE_SIGNING_ALLOWED=NO', args)
            self.assertNotIn('-allowProvisioningUpdates', args)


if __name__ == '__main__':
    unittest.main()
