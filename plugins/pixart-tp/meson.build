cargs = ['-DG_LOG_DOMAIN="FuPluginPxiTp"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('pixart-tp.quirk')

plugin_builtin_pxi_tp = static_library('fu_plugin_pxi_tp',
  rustgen.process('fu-pxi-tp.rs'),
  sources: [
    'fu-pxi-tp-fw-struct.c',
    'fu-pxi-tp-haptic-device.c',
    'fu-pxi-tp-device.c',
    'fu-pxi-tp-section.c',
    'fu-pxi-tp-firmware.c',
    'fu-pxi-tp-plugin.c',
    'fu-pxi-tp-register.c',
    'fu-pxi-tp-tf-communication.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_pxi_tp

enumeration_data += files('tests/pxi-tp-setup.json')
device_tests += files('tests/pxi-tp.json')

if get_option('tests')
  install_data(
    ['tests/pxi-tp.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'),
    install_tag: 'tests',
  )

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  e = executable(
    'pxi-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_pxi_tp,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
    install_tag: 'tests',
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )

  test('pxi-self-test', e, env: env)
endif
