if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCcgx"']

plugin_quirks += files([
  'ccgx-ids.quirk',
  'ccgx.quirk',
  ])
plugin_builtin_ccgx = static_library('fu_plugin_ccgx',
  sources: [
    'fu-ccgx-plugin.c',
    'fu-ccgx-common.c',         # fuzzing
    'fu-ccgx-firmware.c',       # fuzzing
    'fu-ccgx-hid-device.c',
    'fu-ccgx-hpi-common.c',
    'fu-ccgx-hpi-device.c',
    'fu-ccgx-dmc-device.c',
    'fu-ccgx-dmc-devx-device.c',
    'fu-ccgx-dmc-firmware.c',   # fuzzing
    'fu-ccgx-dmc-common.c',     # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    gudev,
  ],
)
plugin_builtins += plugin_builtin_ccgx

if get_option('tests')
  install_data(['tests/ccgx.builder.xml', 'tests/ccgx-dmc.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'ccgx-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_ccgx,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('ccgx-self-test', e, env: env)
endif
endif
