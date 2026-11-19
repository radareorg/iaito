#!/usr/bin/env python3
"""Prepare the isolated radare2 cross builds and qmake's native Xcode project."""

import configparser
import json
import os
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def output(*args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def write_if_changed(path, text):
    path = pathlib.Path(path)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def qmake_command(qt):
    qt = pathlib.Path(qt)
    config = qt / 'bin/target_qt.conf'
    if config.is_file():
        # Qt's cross-kit shell wrapper uses unquoted $*, which loses arguments
        # containing spaces. Invoke the host binary with its target config.
        paths = configparser.ConfigParser(interpolation=None)
        paths.read(config)
        host = (config.parent / paths['Paths']['HostPrefix']).resolve()
        binaries = host / paths['Paths'].get('HostBinaries', 'bin')
        for name in ('qmake6', 'qmake'):
            if (binaries / name).is_file():
                return [binaries / name, '-qtconf', config]
    return [qt / 'bin/qmake']


def check_qt(qt, minimum):
    command = qmake_command(qt)
    if not output(*command, '-query', 'QT_VERSION').startswith('6.'):
        sys.exit('QT_IOS must contain Qt 6')
    if output(*command, '-query', 'QMAKE_XSPEC') != 'macx-ios-clang':
        sys.exit('QT_IOS must target iOS, not desktop macOS')
    config = pathlib.Path(qt, 'mkspecs/qconfig.pri').read_text()
    target = re.search(r'^QMAKE_IOS_DEPLOYMENT_TARGET\s*=\s*([\d.]+)', config, re.M)
    if target:
        def version(value):
            parts = value.split('.')
            return tuple(map(int, parts)) + (0,) * (3 - len(parts))
        if version(minimum) < version(target[1]):
            sys.exit(f'IOS_MIN_VERSION must be at least {target[1]} for this Qt kit')


def qmake(qt, *args):
    run(*qmake_command(qt), *args)


def prepare_radare2(source):
    source = pathlib.Path(source)
    marker = source / '.iaito-ipad-prepared'
    if marker.exists():
        return
    meson = source / 'meson.build'
    text = meson.read_text()
    old = "sdb_exe = libsdb_sp.get_variable('sdb_exe')"
    if text.count(old) != 1:
        sys.exit('Unsupported radare2 Meson layout: cannot select the native SDB generator')
    text = text.replace(old, "sdb_exe = libsdb_sp.get_variable('sdb_build_exe')")
    # radare2 assumes SDB has a shared library even in a static-only build.
    old = "libsdb_sp.get_variable('libsdb').get_shared_lib()"
    if text.count(old) != 1:
        sys.exit('Unsupported radare2 Meson layout: cannot select static SDB')
    text = text.replace(old, 'libsdb_static')
    text, count = re.subn(r"libsdb_install = custom_target\('libr2sdb',.*?"
                         r"install_dir\s*:\s*get_option\('libdir'\)\)",
                         'libsdb_install = libsdb_static', text, flags=re.S)
    if count != 1:
        sys.exit('Unsupported radare2 Meson layout: cannot remove the shared SDB install')
    sdb_meson = source / 'subprojects/sdb/meson.build'
    sdb_text = sdb_meson.read_text() + '''
# Run database generation on the Mac, never on the cross-compiled target.
sdb_build_exe = executable('sdb-build', ['src/entry.c'] + libsdb_sources,
  native: true, include_directories: sdb_inc, install: false)
'''
    sdb_meson.write_text(sdb_text)
    meson.write_text(text)
    marker.touch()


def meson_string(value):
    return "'" + str(value).replace('\\', '\\\\').replace("'", "\\'") + "'"


def prepare_zlib(source):
    # otezip's partial zlib API overrides Apple's zlib in a static Qt app.
    source = pathlib.Path(source)
    marker = source / '.iaito-ipad-system-zlib'
    if marker.exists():
        return
    otezip = source / 'subprojects/otezip'
    replacements = {
        source / 'meson.build': [
            ("sys_zlib = dependency('zlib', required: false)",
             "sys_zlib = cc.find_library('z', required: true)"),
        ],
        source / 'shlr/zip/meson.build': [
            ('zlib_dep = declare_dependency()  # otezip has bundled zlib',
             'zlib_dep = sys_zlib'),
        ],
        otezip / 'meson.build': [
            ('include_directories: otezip_inc,',
             "include_directories: otezip_inc,\n"
             "  dependencies: meson.get_compiler('c').find_library('z'),"),
        ],
        otezip / 'src/lib/otezip.c': [
            ('#define MDEFLATE_IMPLEMENTATION\n#include "deflate.inc.c"',
             '#include "../include/otezip/zlib.h"'),
        ],
    }
    prepared = {}
    for path, changes in replacements.items():
        text = path.read_text()
        for old, new in changes:
            if old not in text:
                sys.exit(f'Unsupported radare2 zlib layout: {path}')
            text = text.replace(old, new)
        prepared[path] = text
    for path, text in prepared.items():
        write_if_changed(path, text)
    # Keep otezip's include directory for zip.h, but use SDK zlib types and API.
    (otezip / 'src/include/otezip/zlib.h').write_text('#include_next <zlib.h>\n')
    (otezip / 'src/include/otezip/zstream.h').write_text('#include "zlib.h"\n')
    marker.touch()


def prepare_sandbox(source):
    # Some upstream subprocess paths ignore use_fork, and HAVE_SYSTEM is redefined.
    source = pathlib.Path(source)
    marker = source / '.iaito-ipad-sandbox-v2'
    if marker.exists():
        return
    replacements = {
        'libr/include/r_types.h': [(
            '#if __IPHONE_8_0 && TARGET_OS_IPHONE && !defined(MAC_OS_VERSION_11_0)',
            '#if TARGET_OS_IPHONE')],
        'shlr/bochs/src/libbochs.c': [
            ('#if __wasi__', '#if __wasi__ || !LIBC_HAVE_FORK'),
            ('#elif __wasi__', '#elif __wasi__ || !LIBC_HAVE_FORK'),
        ],
        'libr/main/r2pm.c': [(
            '#if WANT_QJS && R2__UNIX__ && !defined(__wasi__)',
            '#if WANT_QJS && R2__UNIX__ && LIBC_HAVE_FORK && !defined(__wasi__)')],
        'libr/socket/run.c': [
            ('#if R2__UNIX__ && !__wasi__\n\t\t\tclose (0);',
             '#if R2__UNIX__ && LIBC_HAVE_FORK && !__wasi__\n\t\t\tclose (0);'),
            ('#if !LIBC_HAVE_FORK\n\t\t\texit (execv',
             '#if !LIBC_HAVE_FORK && !TARGET_OS_IPHONE\n\t\t\texit (execv'),
        ],
    }
    prepared = {}
    for relative, changes in replacements.items():
        path = source / relative
        text = path.read_text()
        for old, new in changes:
            if new in text:
                continue
            if old not in text:
                sys.exit(f'Unsupported radare2 sandbox layout: {path}')
            text = text.replace(old, new)
        prepared[path] = text
    for path, text in prepared.items():
        write_if_changed(path, text)
    marker.touch()


def cross_file(build, sdk, arch, minimum):
    build = pathlib.Path(build)
    build.mkdir(parents=True, exist_ok=True)
    sdk_path = output('xcrun', '--sdk', sdk, '--show-sdk-path')
    triple = f'{arch}-apple-ios{minimum}' + ('-simulator' if sdk == 'iphonesimulator' else '')
    config = {'sdk': sdk_path, 'triple': triple}
    stamp = build / 'toolchain.json'
    if stamp.exists() and json.loads(stamp.read_text()) != config:
        sys.exit(f'Xcode SDK or deployment target changed; use a fresh BUILD_DIR: {build}')
    write_if_changed(stamp, json.dumps(config, indent=2) + '\n')
    tools = {key: output('xcrun', '--sdk', sdk, '--find', tool)
             for key, tool in [('c', 'clang'), ('cpp', 'clang++'), ('ar', 'ar'), ('strip', 'strip')]}
    flags = ['-target', triple, '-isysroot', sdk_path, '-DHAVE_SYSTEM=0',
             '-include', 'TargetConditionals.h']
    args = '[' + ', '.join(map(meson_string, flags)) + ']'
    binaries = '\n'.join(f'{key} = {meson_string(path)}' for key, path in tools.items())
    cross = build / 'ios.ini'
    write_if_changed(cross, f'''[binaries]
{binaries}
pkg-config = 'false'
cmake = 'false'

[host_machine]
system = 'darwin'
cpu_family = '{'aarch64' if arch == 'arm64' else 'x86_64'}'
cpu = '{arch}'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = {args}
cpp_args = {args}
c_link_args = {args}
cpp_link_args = {args}
''')
    return cross


def libraries(source, build, commit, minimum, simulator_archs, jobs):
    source, build = pathlib.Path(source), pathlib.Path(build)
    prepare_sandbox(source)
    prepare_zlib(source)
    stage = build / 'stage'
    slices = [('iphoneos', 'arm64')] + [('iphonesimulator', a) for a in simulator_archs.split()]
    for sdk, arch in slices:
        target = build / f'{sdk}-{arch}'
        cross = cross_file(target, sdk, arch, minimum)
        prefix = target / 'prefix'
        # Meson caches cross-file options; pass them explicitly when reconfiguring.
        config = configparser.ConfigParser(interpolation=None)
        config.read(cross)
        compiler_options = [f'-D{k}={v}' for k, v in config['built-in options'].items()]
        reconfigure = ['--reconfigure'] if (target / 'meson-private/coredata.dat').exists() else []
        run('meson', 'setup', *reconfigure, target, source, *compiler_options,
            '--cross-file', cross, '--prefix', prefix,
            '--libdir', 'lib', '--buildtype', 'release', '-Ddefault_library=static',
            '-Dcli=disabled', '-Denable_tests=false', '-Denable_r2r=false', '-Duse_webui=false',
            '-Ddebugger=false', '-Duse_fork=false', '-Duse_dylink=false',
            '-Dwant_ptrace_wrap=false', '-Dstatic_themes=true', '-Duse_sys_zlib=true',
            '-Dr2_gittip=' + commit)
        run('meson', 'compile', '-C', target, '-j' + jobs)
        run('meson', 'install', '-C', target, '--no-rebuild')
        # Meson does not install all the private static dependencies (Capstone,
        # QuickJS, zip, SDB, ...). Combine its actual static targets into one
        # archive so Xcode never needs host pkg-config or a fragile library list.
        targets = json.loads(output('meson', 'introspect', '--targets', target))
        archives = set()
        for item in targets:
            if item['type'] == 'static library':
                archives.update(pathlib.Path(p) for p in item['filename'] if p.endswith('.a'))
        if not any(p.name == 'libr_core.a' for p in archives):
            sys.exit('radare2 did not produce its core static library')
        # SDB's both_libraries and explicit static target contain the same objects.
        if any(p.name == 'liblibsdb_static.a' for p in archives):
            archives = {p for p in archives if p.name != 'libsdb.a'}
        combined = target / 'libradare2.a'
        run('xcrun', 'libtool', '-static', '-o', combined, *sorted(archives))
        verify_zlib(combined)
        verify_sandbox(combined)

    for sdk in ('iphoneos', 'iphonesimulator'):
        sdk_slices = [build / f'{s}-{a}' for s, a in slices if s == sdk]
        destination = stage / sdk
        (destination / 'lib').mkdir(parents=True, exist_ok=True)
        headers = destination / 'include'
        if headers.exists():
            shutil.rmtree(headers)
        shutil.copytree(sdk_slices[0] / 'prefix/include', headers)
        run('xcrun', 'lipo', '-create', *(p / 'libradare2.a' for p in sdk_slices),
            '-output', destination / 'lib/libradare2.a')

    framework = stage / 'Radare2.xcframework'
    if framework.exists():
        shutil.rmtree(framework)
    run('xcodebuild', '-create-xcframework',
        '-library', stage / 'iphoneos/lib/libradare2.a', '-headers', stage / 'iphoneos/include',
        '-library', stage / 'iphonesimulator/lib/libradare2.a',
        '-headers', stage / 'iphonesimulator/include', '-output', framework)
    share = stage / 'radare2/share/radare2'
    if share.exists():
        shutil.rmtree(share)
    shutil.copytree(build / 'iphoneos-arm64/prefix/share/radare2', share)
    if not list(share.rglob('*.sdb')):
        sys.exit('radare2 runtime databases were not installed')
    (stage / 'source.json').write_text(json.dumps({'commit': commit, 'ios': minimum}, indent=2) + '\n')


def qmake_quote(value):
    # qmake expands dollar signs even inside quoted strings.
    if any(c in str(value) for c in '$\n\r'):
        sys.exit('Build paths and settings must not contain dollar signs or newlines')
    return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'


def project(root, build, r2_build, qt, minimum, bundle, number, team):
    root, build = pathlib.Path(root), pathlib.Path(build)
    config = {
        'IAITO_IPAD_R2': pathlib.Path(r2_build) / 'stage',
        'IAITO_IPAD_BUNDLE_ID': bundle,
        'IAITO_IPAD_BUILD_NUMBER': number,
        'QMAKE_IOS_DEPLOYMENT_TARGET': minimum,
        'QMAKE_DEVELOPMENT_TEAM': team,
        'QMAKE_APPLE_SIMULATOR_ARCHS': os.environ.get('SIMULATOR_ARCHS', 'x86_64'),
        'IAITO_ENABLE_DEBUGGER': 'false',
        'IAITO_ENABLE_CRASH_REPORTS': 'false',
    }
    write_if_changed(build / 'ipad.pri', ''.join(f'{k} = {qmake_quote(v)}\n'
                                              for k, v in config.items()))
    (build / 'package.json').write_text(json.dumps({
        'stage': str(pathlib.Path(r2_build) / 'stage'), 'bundle': bundle, 'number': number,
    }, indent=2) + '\n')


def verify_app(app, stage, sdk, bundle, number):
    app, stage = pathlib.Path(app), pathlib.Path(stage)
    with (app / 'Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if info.get('UIDeviceFamily') != [2] or info.get('CFBundleIdentifier') != bundle:
        sys.exit('The app has incorrect iPad targeting or bundle identifier')
    if info.get('CFBundleVersion') != number:
        sys.exit('The app has an incorrect build number')
    launch = info.get('UILaunchStoryboardName')
    if not launch or not (app / (launch + '.storyboardc')).is_dir():
        sys.exit('The app has no configured launch storyboard')
    for name in ('Assets.car', 'PrivacyInfo.xcprivacy'):
        if not (app / name).is_file():
            sys.exit(f'The app is missing {name}')
    share = stage / 'radare2/share/radare2'
    if not list(share.rglob('*.sdb')):
        sys.exit('The staging directory has no radare2 databases')
    for path in share.rglob('*'):
        if path.is_file():
            packaged = app / 'radare2/share/radare2' / path.relative_to(share)
            if not packaged.is_file() or path.read_bytes() != packaged.read_bytes():
                sys.exit(f'The app has missing or stale radare2 data: {packaged}')
    binary = app / info['CFBundleExecutable']
    platform = 'IOSSIMULATOR' if sdk == 'iphonesimulator' else 'IOS'
    if not re.search(r'platform\s+' + platform + r'\b', output('xcrun', 'vtool', '-show-build', binary)):
        sys.exit('The app executable was built for the wrong platform')
    dependencies = output('xcrun', 'otool', '-L', binary).splitlines()[1:]
    for dependency in dependencies:
        name = dependency.strip().split(' (', 1)[0]
        if not name.startswith(('/System/Library/', '/usr/lib/')):
            sys.exit(f'The app depends on an unbundled library: {name}')
    verify_zlib(binary)
    verify_sandbox(binary)
    print(f'Verified iPad bundle: {app}', flush=True)


def verify_zlib(binary):
    # Hidden/local definitions can still satisfy Qt's zlib references at link time.
    symbols = set(output('xcrun', 'nm', '-Uj', binary).splitlines())
    collisions = symbols.intersection({'_inflate', '_inflateInit_', '_inflateInit2_',
                                      '_inflateEnd', '_deflate', '_deflateInit_',
                                      '_deflateInit2_', '_deflateEnd'})
    if collisions:
        sys.exit(f'The iPad build overrides system zlib: {", ".join(sorted(collisions))}')


def verify_sandbox(binary):
    symbols = set(output('xcrun', 'nm', '-uj', binary).splitlines())
    forbidden = symbols.intersection({'_fork', '_vfork', '_system', '_popen', '_ptrace',
                                      '_execl', '_execle', '_execlp', '_execv', '_execve',
                                      '_execvp', '_execvpe', '_posix_spawn', '_posix_spawnp'})
    if forbidden:
        sys.exit(f'The iPad build imports subprocess/debugging APIs: '
                 f'{", ".join(sorted(forbidden))}')


def build_app(root, build, mode, jobs):
    root, build = pathlib.Path(root), pathlib.Path(build)
    products = build / 'products'
    products.mkdir(parents=True, exist_ok=True)
    archive = products / 'iaito.xcarchive'
    args = ['xcodebuild', '-project', root / 'dist/ipad/iaito.xcodeproj', '-scheme', 'iaito',
            '-configuration', 'Release', '-jobs', jobs, '-derivedDataPath', build / 'DerivedData']
    if mode == 'simulator':
        args += ['-sdk', 'iphonesimulator', '-destination', 'generic/platform=iOS Simulator']
    else:
        args += ['-sdk', 'iphoneos', '-destination', 'generic/platform=iOS']
    team = os.environ.get('DEVELOPMENT_TEAM')
    if team and mode != 'simulator':
        args += ['DEVELOPMENT_TEAM=' + team, '-allowProvisioningUpdates']
    else:
        args += ['CODE_SIGNING_ALLOWED=NO']
    if mode in ('archive', 'export'):
        run(*args, '-archivePath', archive, 'archive')
        app = archive / 'Products/Applications/iaito.app'
    else:
        run(*args, 'CONFIGURATION_BUILD_DIR=' + str(products / mode), 'build')
        app = products / mode / 'iaito.app'
    package = json.loads((build / 'package.json').read_text())
    verify_app(app, package['stage'], 'iphonesimulator' if mode == 'simulator' else 'iphoneos',
               package['bundle'], package['number'])
    if mode == 'export':
        options = os.environ.get('EXPORT_OPTIONS_PLIST')
        if not options:
            options = build / 'ExportOptions.plist'
            with open(options, 'wb') as stream:
                plistlib.dump({'method': 'app-store-connect', 'teamID': team,
                               'signingStyle': 'automatic', 'destination': 'export'}, stream)
        run('xcodebuild', '-exportArchive', '-archivePath', archive, '-exportOptionsPlist', options,
            '-exportPath', products / 'export', '-allowProvisioningUpdates')


if __name__ == '__main__':
    commands = {'check-qt': check_qt, 'qmake': qmake,
                'radare2': prepare_radare2, 'libraries': libraries,
                'project': project, 'build': build_app}
    try:
        commands[sys.argv[1]](*sys.argv[2:])
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
