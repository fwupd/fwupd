allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDbx"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('uefi-dbx.quirk')
plugin_builtin_uefi_dbx = static_library('fu_plugin_uefi_dbx',
  sources: [
    'fu-uefi-dbx-plugin.c',
    'fu-uefi-dbx-common.c',
    'fu-uefi-dbx-device.c',
    'fu-uefi-dbx-snapd-notifier.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ],
)
plugin_builtins += plugin_builtin_uefi_dbx
device_tests += files('tests/uefi-dbx.json')

if get_option('tests')
  install_data(['tests/snapd.py'],
    install_dir: join_paths(installed_test_datadir, 'tests'))

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'uefi-dbx-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libcurl,
    ],
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_dbx,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('uefi-dbx-self-test', e, env: env)  # added to installed-tests
endif

dbxtool = executable(
  'dbxtool',
  sources: [
    'fu-dbxtool.c',
  ],
  include_directories: plugin_incdirs,
  dependencies: plugin_deps,
  link_with: [
    plugin_libs,
    plugin_builtin_uefi_dbx,
  ],
  install: true,
  install_dir: bindir,
  install_rpath: libdir_pkg,
  c_args: cargs,
)

if get_option('man')
  custom_target('dbxtool.1',
    input: 'dbxtool.md',
    output: 'dbxtool.1',
    command: [
      generate_man, '@INPUT@', '-o', '@OUTPUT@',
      '--replace', 'PACKAGE_VERSION', fwupd_version,
    ],
    install: true,
    install_tag: 'man',
    install_dir: join_paths(mandir, 'man1'),
  )
endif
if build_docs
  md_targets += custom_target('dbxtool.md',
    input: 'dbxtool.md',
    output: 'dbxtool.md',
    command: [
      generate_man, '@INPUT@', '-o', '@OUTPUT@',
      '--replace', 'PACKAGE_VERSION', fwupd_version,
      '--md',
    ],
  )
  man_md += ['"dbxtool.md"']
endif
