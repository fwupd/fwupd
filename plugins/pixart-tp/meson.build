cargs = ['-DG_LOG_DOMAIN="FuPluginPixartTp"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('pixart-tp.quirk')

plugin_builtin_pixart_tp = static_library('fu_plugin_pixart_tp',
  rustgen.process('fu-pixart-tp.rs'),
  sources: [
    'fu-pixart-tp-haptic-device.c',
    'fu-pixart-tp-device.c',
    'fu-pixart-tp-section.c',
    'fu-pixart-tp-firmware.c',
    'fu-pixart-tp-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_pixart_tp

device_tests += files('tests/pixart-tp.json')

if get_option('tests')
  install_data(
    ['tests/pixart-tp.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'),
    install_tag: 'tests',
  )

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  e = executable(
    'pixart-tp-self-test',
    rustgen.process('fu-pixart-tp.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_pixart_tp,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    install_tag: 'tests',
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )

  test('pixart-tp-self-test', e, env: env)
endif
