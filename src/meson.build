project('iaito', 'cpp',
  default_options: 'cpp_std=c++20',
  meson_version: '>=0.50.0')

#  default_options : [
#        'cpp_std=c++latest',
#        'default_library=static',
#        'optimization=3',
#        'buildtype=debugoptimized'])

if get_option('with_qt6')
  qt_mod = import('qt6')
else
  qt_mod = import('qt5')
endif

py3_exe = import('python').find_installation('python3')

qt_modules = []
feature_define_args = []
# if get_option('enable_python')
#   message('Python is enabled')
#   feature_define_args += ['-DIAITO_ENABLE_PYTHON']
# 
#   if get_option('enable_python_bindings')
#     message('Python Bindings are enabled')
#     feature_define_args += ['-DIAITO_ENABLE_PYTHON_BINDINGS']
#   endif
# endif

add_project_arguments(feature_define_args, language: 'cpp')
add_project_arguments('-DIAITO_SOURCE_BUILD', language: 'cpp')

parse_cmd = [py3_exe, join_paths(meson.current_source_dir(), '../scripts/meson_parse_qmake.py')]
configure_qmake_cmd = [py3_exe, join_paths(meson.current_source_dir(), '../scripts/meson_configure_qmake_in.py')]

qt_modules += run_command(parse_cmd + ['QT'], check: true).stdout().split(';')
sources = run_command(parse_cmd + ['SOURCES'], check: true).stdout().split(';')
headers = run_command(parse_cmd + ['HEADERS'], check: true).stdout().split(';')
ui_files = run_command(parse_cmd + ['FORMS'], check: true).stdout().split(';')
qresources = run_command(parse_cmd + ['RESOURCES'], check: true).stdout().split(';')
version_major = run_command(parse_cmd + ['IAITO_VERSION_MAJOR'], check: true).stdout()
version_minor = run_command(parse_cmd + ['IAITO_VERSION_MINOR'], check: true).stdout()
version_patch = run_command(parse_cmd + ['IAITO_VERSION_PATCH'], check: true).stdout()

conf_json = '''{"IAITO_VERSION_MAJOR":@0@,
                "IAITO_VERSION_MINOR":@1@,
                "IAITO_VERSION_PATCH":@2@}'''.format(
                version_major, version_minor, version_patch)
configure_file(input: 'IaitoConfig.h.in',
               output: 'IaitoConfig.h',
               command: configure_qmake_cmd + ['@INPUT@', '@OUTPUT0@', conf_json])
conf_inc = include_directories('.')

sp_dir = join_paths(meson.source_root(), 'subprojects')
sp_r2_dir = join_paths(sp_dir, 'radare2')
exists_cmd = '__import__("sys").exit(__import__("os").path.exists("@0@"))'.format(sp_r2_dir)
if run_command(py3_exe, '-c', exists_cmd).returncode() == 0
  r2_src_dir = join_paths(meson.source_root(), '..', 'radare2')
  if host_machine.system() == 'windows'
    sp_dir = '\\'.join(sp_dir.split('/'))
    sp_r2_dir = '\\'.join(sp_r2_dir.split('/'))
    r2_src_dir = '\\'.join(r2_src_dir.split('/'))
    link_cmd = ['CMD', '/C', 'MKDIR', sp_dir, '&', 'MKLINK', '/D', sp_r2_dir, r2_src_dir]
  else
    link_cmd = ['sh', '-c', 'mkdir @0@ ; ln -s @1@ @2@'.format(sp_dir, r2_src_dir, sp_r2_dir)]
  endif
  run_command(link_cmd, check: true)
endif

incdirs = ['.']
if host_machine.system() == 'windows'
  windows = import('windows')
  iaito_res = windows.compile_resources('iaito_resource.rc')
  sources += iaito_res
  incdirs += [
    '../radare2/include/libr',
    '../radare2/include/libr/sdb',
  ]
  add_global_arguments('-D_WINDOWS', language:'cpp')
  add_global_arguments('/std:c++20', language:'cpp')
  r2libdir = [
    meson.current_source_dir() + '\\..\\radare2\\lib',
    'C:\\radare2\\lib'
  ]
  cpp = meson.get_compiler('cpp')
  r_core = [
    cpp.find_library('r_core', dirs: r2libdir),
    cpp.find_library('r_io', dirs: r2libdir),
    cpp.find_library('r_debug', dirs: r2libdir),
    cpp.find_library('r_bp', dirs: r2libdir),
    cpp.find_library('r_crypto', dirs: r2libdir),
    cpp.find_library('r_fs', dirs: r2libdir),
    cpp.find_library('r_egg', dirs: r2libdir),
    cpp.find_library('r_asm', dirs: r2libdir),
    cpp.find_library('r_esil', dirs: r2libdir),
    cpp.find_library('r_search', dirs: r2libdir),
    cpp.find_library('r_arch', dirs: r2libdir),
    cpp.find_library('r_magic', dirs: r2libdir),
    cpp.find_library('r_syscall', dirs: r2libdir),
    cpp.find_library('r_bin', dirs: r2libdir),
    cpp.find_library('r_cons', dirs: r2libdir),
    cpp.find_library('r_flag', dirs: r2libdir),
    cpp.find_library('r_reg', dirs: r2libdir),
    cpp.find_library('r_util', dirs: r2libdir),
    cpp.find_library('r_anal', dirs: r2libdir),
    cpp.find_library('r_config', dirs: r2libdir),
  ]
else
  r_core = dependency('r_core',
    default_options : ['enable_tests=false'])
endif

if get_option('with_qt6')
  qt_modules += 'SvgWidgets'
  qtdep = dependency('qt6', modules: qt_modules, main: true)
else
  qtdep = dependency('qt5', modules: qt_modules, main: true)
endif

deps = [r_core, qtdep]
# if get_option('enable_python')
#   py3_dep = dependency('python3')
#   deps += [py3_dep]
#   if get_option('enable_python_bindings')
#     shiboken_dep = dependency('shiboken2', method: 'pkg-config')
#     pyside_dep = dependency('pyside2', method: 'pkg-config')
#     pyside_inc_dep = declare_dependency(include_directories: include_directories(join_paths(pyside_dep.get_pkgconfig_variable('includedir'), 'QtCore'),
#                                                                                  join_paths(pyside_dep.get_pkgconfig_variable('includedir'), 'QtGui'),
#                                                                                  join_paths(pyside_dep.get_pkgconfig_variable('includedir'), 'QtWidgets')))
#     deps += [shiboken_dep, pyside_dep, pyside_inc_dep]
#     shiboken_exe = shiboken_dep.get_pkgconfig_variable('generator_location')
#     qt5core_dep = dependency('Qt5Core', method: 'pkg-config')
#     bindings_generate_inc = [meson.current_source_dir(),
#                              join_paths(meson.current_source_dir(), 'core'),
#                              join_paths(meson.current_source_dir(), 'common'),
#                              join_paths(meson.current_source_dir(), 'widgets'),
#                              join_paths(meson.current_source_dir(), 'plugins'),
#                              join_paths(meson.current_source_dir(), 'menus'),
#                              join_paths(meson.current_source_dir(), 'subprojects/radare2/libr/include'),
#                              join_paths(meson.current_build_dir(), 'subprojects/radare2'),
#                              qt5core_dep.get_pkgconfig_variable('includedir'),
#                              join_paths(qt5core_dep.get_pkgconfig_variable('includedir'), 'QtCore'),
#                              join_paths(qt5core_dep.get_pkgconfig_variable('includedir'), 'QtGui'),
#                              join_paths(qt5core_dep.get_pkgconfig_variable('includedir'), 'QtWidgets')]
#     message('bindings_inc: @0@'.format(bindings_generate_inc))
#     subdir('bindings')
#     sources += bindings_target
#   endif
# endif

moc_files = qt_mod.preprocess(
  moc_headers: headers,
  ui_files: ui_files,
  qresources: qresources,
  moc_extra_arguments: feature_define_args
)

cpp = meson.get_compiler('cpp')

if host_machine.system() == 'windows'
  add_project_arguments('-D_CRT_NONSTDC_NO_DEPRECATE', language: 'cpp')
  add_project_arguments('-D_CRT_SECURE_NO_WARNINGS', language: 'cpp')
  add_project_link_arguments(join_paths(py3_exe.get_variable('BINDIR'), 'libs', 'python3.lib'), language: 'cpp')
endif

iaito_exe = executable(
  'iaito',
  moc_files,
  win_subsystem: 'windows',
  install: true,
  sources: sources,
  include_directories: [
    incdirs,
    include_directories('core', 'common', 'widgets', 'plugins'),
    conf_inc
  ],
  dependencies: deps,
)

install_man('iaito.1')
