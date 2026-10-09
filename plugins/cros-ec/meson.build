cargs = ['-DG_LOG_DOMAIN="FuPluginCrosEc"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('cros-ec.quirk')
plugin_builtin_cros_ec = static_library(
  'fu_plugin_cros_ec',
  rustgen.process(
    'fu-cros-ec.rs' # fuzzing
  ),
  sources: [
    'fu-cros-ec-plugin.c',
    'fu-cros-ec-usb-device.c',
    'fu-cros-ec-common.c', # fuzzing
    'fu-cros-ec-firmware.c', # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_cros_ec

device_tests += files(
  'tests/acer-d501.json',
  'tests/google-servo-micro.json',
)

if get_option('tests')
  e = executable(
    'cros-ec-self-test',
    rustgen.process('fu-cros-ec.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_cros_ec,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'cros-ec-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
