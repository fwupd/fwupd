allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiDb"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('uefi-db.quirk')
plugin_builtin_uefi_db = static_library('fu_plugin_uefi_db',
  sources: [
    'fu-uefi-db-device.c',
    'fu-uefi-db-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_db

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('G_DEBUG', 'fatal-criticals')
  e = executable(
    'uefi-db-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_db,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('uefi-db-self-test', e, env: env)
endif
