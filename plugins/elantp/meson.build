if gudev.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginElantp"']

plugin_quirks += files('elantp.quirk')
plugin_builtin_elantp = static_library('fu_plugin_elantp',
  sources: [
    'fu-elantp-plugin.c',
    'fu-elantp-firmware.c',   # fuzzing
    'fu-elantp-hid-device.c',
    'fu-elantp-i2c-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_elantp

if get_option('tests')
  install_data(['tests/elantp.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'elantp-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_elantp,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('elantp-self-test', e, env: env)
endif
endif
