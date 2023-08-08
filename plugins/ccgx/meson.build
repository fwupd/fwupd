if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCcgx"']

plugin_quirks += files([
  'ccgx-ids.quirk',
  'ccgx.quirk',
  ])
plugin_builtin_ccgx = static_library('fu_plugin_ccgx',
  rustgen.process(
    'fu-ccgx.rs',           # fuzzing
  ),
  sources: [
    'fu-ccgx-plugin.c',
    'fu-ccgx-common.c',         # fuzzing
    'fu-ccgx-firmware.c',       # fuzzing
    'fu-ccgx-hid-device.c',
    'fu-ccgx-hpi-device.c',
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
  install_data(['tests/ccgx.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'))
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'ccgx-self-test',
    rustgen.process('fu-ccgx.rs'),
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
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('ccgx-self-test', e, env: env)
endif
endif
