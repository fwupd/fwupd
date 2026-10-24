cargs = ['-DG_LOG_DOMAIN="FuPluginXyKbms"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('xy-kbms.quirk')
plugin_rustgen_output = rustgen.process('fu-xy-kbms.rs')
plugin_builtin_xy_kbms = static_library(
  'fu_plugin_xy_kbms',
  plugin_rustgen_output,
  sources: [
    'fu-xy-kbms-common.c',
    'fu-xy-kbms-config.c',
    'fu-xy-kbms-device.c',
    'fu-xy-kbms-firmware.c',
    'fu-xy-kbms-pinmap.c',
    'fu-xy-kbms-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_xy_kbms

device_tests += files('tests/xy-kbms.json')

if get_option('tests')
  install_data(
    'tests' / 'xykbms_pin_usb_default_uk.cfg',
    install_dir: installed_test_datadir / 'tests',
  )
  test(
    'xy-kbms-self-test',
    executable(
      'xy-kbms-self-test',
      plugin_rustgen_output,
      sources: ['fu-self-test.c'],
      include_directories: plugin_incdirs,
      dependencies: plugin_deps,
      link_with: [
        plugin_builtin_xy_kbms,
        plugin_libs,
      ],
      install: true,
      install_tag: 'tests',
      install_dir: installed_test_bindir,
      install_rpath: libdir_pkg,
      c_args: ['-DSRCDIR="@0@"'.format(meson.current_source_dir())],
    ),
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
