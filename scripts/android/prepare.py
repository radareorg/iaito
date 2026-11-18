#!/usr/bin/env python3
"""Prepare isolated Meson cross builds and Android assets for android.sh."""

import hashlib
import json
import pathlib
import re
import shutil
import sys
import zipfile


def cross_file(args):
    build, toolchain, triple, api, family, cpu, repository, ref = args
    build = pathlib.Path(build)
    config = json.dumps(args[1:], indent=2) + '\n'
    stamp = build / 'toolchain.json'
    if stamp.exists() and stamp.read_text() != config:
        sys.exit('Android build configuration changed; use a fresh BUILD_DIR')
    stamp.write_text(config)

    def tool(name):
        # Meson uses single-quoted strings, including for paths with spaces.
        path = str(pathlib.Path(toolchain, name))
        return "'" + path.replace('\\', '\\\\').replace("'", "\\'") + "'"

    (build / 'android.ini').write_text(f'''[binaries]
c = {tool(triple + api + '-clang')}
cpp = {tool(triple + api + '-clang++')}
ar = {tool('llvm-ar')}
strip = {tool('llvm-strip')}
pkg-config = 'false'
cmake = 'false'

[host_machine]
system = 'android'
cpu_family = '{family}'
cpu = '{cpu}'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-Wl,-z,max-page-size=16384']
''')


def prepare_radare2(source):
    source = pathlib.Path(source)
    header = source / 'libr/include/r_io.h'
    if not (source / 'libr/io/p/io_content.c').is_file() or 'RIOContent' not in header.read_text():
        sys.exit('Android requires radare2 with the content:// app resolver. '
                 'Use current radare2 Git and a fresh BUILD_DIR to replace a cached older snapshot.')
    marker = source / '.iaito-android-prepared'
    if marker.exists():
        return
    meson = source / 'meson.build'
    text = meson.read_text()
    old = "sdb_exe = libsdb_sp.get_variable('sdb_exe')"
    if text.count(old) != 1:
        sys.exit('Unsupported radare2 Meson layout: cannot select the build-machine SDB tool')
    # r2 runs SDB directly and from Python. An exe_wrapper is insufficient.
    text = text.replace(old, "sdb_exe = libsdb_sp.get_variable('sdb_build_exe')")
    sdb_meson = source / 'subprojects/sdb/meson.build'
    sdb_text = sdb_meson.read_text()
    sdb_text += '''
# Build-machine tool for radare2's generated runtime databases.
sdb_build_exe = executable('sdb-build', ['src/entry.c'] + libsdb_sources,
  native: true,
  include_directories: sdb_inc,
  install: false)
'''
    sdb_meson.write_text(sdb_text)
    meson.write_text(text)
    marker.touch()


def assets(build):
    build = pathlib.Path(build)
    source = build / 'prefix/share/radare2'
    if not source.is_dir() or not list(source.rglob('*.sdb')):
        sys.exit('radare2 runtime databases are missing from the staging prefix')
    target = build / 'package/assets/radare2'
    target.mkdir(parents=True, exist_ok=True)
    # Recreate only the generated asset tree, so removed files cannot linger.
    share = target / 'share'
    if share.exists():
        shutil.rmtree(share)
    shutil.copytree(source, share / 'radare2')
    files = sorted(p for p in share.rglob('*') if p.is_file())
    digest = hashlib.sha256()
    names = []
    for path in files:
        name = path.relative_to(target).as_posix()
        names.append(name)
        digest.update(name.encode() + b'\0' + path.read_bytes())
    (target / 'files.txt').write_text('\n'.join(names) + '\n')
    (target / 'bundle-id').write_text(digest.hexdigest())


def package(build, qt):
    # Retain the template belonging to the selected Qt version (Java metadata,
    # providers and permissions change across releases).
    template = pathlib.Path(qt, 'src/android/templates/AndroidManifest.xml')
    text = template.read_text()
    text, count = re.subn(r'\bpackage="[^"]*"', 'package="org.radare.iaito"', text, count=1)
    if count != 1:
        sys.exit('Qt Android manifest template has no package attribute')
    text, count = re.subn(r'\bandroid:icon="[^"]*"', 'android:icon="@mipmap/iaito"',
                         text, count=1)
    if count != 1:
        sys.exit('Qt Android manifest template has no application icon attribute')
    target = pathlib.Path(build, 'package')
    target.mkdir(parents=True, exist_ok=True)
    shutil.copytree(pathlib.Path(__file__).with_name('res'), target / 'res', dirs_exist_ok=True)
    (target / 'AndroidManifest.xml').write_text(text)


def verify_apk(build, apk, abi):
    build = pathlib.Path(build)
    libraries = list((build / 'prefix/lib').glob('libr_*.so'))
    if not libraries or not any(p.name == 'libr_core.so' for p in libraries):
        sys.exit('No Android radare2 libraries to verify')
    machine = {'arm64-v8a': 183, 'armeabi-v7a': 40, 'x86_64': 62, 'x86': 3}[abi]
    with zipfile.ZipFile(apk) as archive:
        for lib in libraries:
            name = f'lib/{abi}/{lib.name}'
            try:
                header = archive.read(name)[:20]
            except KeyError:
                sys.exit(f'APK is missing {name}')
            if header[:4] != b'\x7fELF' or int.from_bytes(header[18:20], 'little') != machine:
                sys.exit(f'APK has the wrong architecture for {name}')
        for path in (build / 'package/assets/radare2').rglob('*'):
            if path.is_file():
                name = path.relative_to(build / 'package').as_posix()
                try:
                    packaged = archive.read(name)
                except KeyError:
                    sys.exit(f'APK is missing {name}')
                if packaged != path.read_bytes():
                    sys.exit(f'APK has stale radare2 data: {name}')


if __name__ == '__main__':
    if sys.argv[1] == '--radare2':
        prepare_radare2(sys.argv[2])
    elif sys.argv[1] == '--assets':
        assets(sys.argv[2])
    elif sys.argv[1] == '--package':
        package(*sys.argv[2:])
    elif sys.argv[1] == '--verify-apk':
        verify_apk(*sys.argv[2:])
    else:
        cross_file(sys.argv[1:])
