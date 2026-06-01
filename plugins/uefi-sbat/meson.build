allow_uefi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginUefiSbat"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_builtin_uefi_sbat = static_library('fu_plugin_uefi_sbat',
  sources: [
    'fu-uefi-sbat-device.c',
    'fu-uefi-sbat-firmware.c',
    'fu-uefi-sbat-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_uefi_sbat

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'uefi-sbat-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_uefi_sbat,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('uefi-sbat-self-test', e, env: env)
endif
