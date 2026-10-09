cargs = ['-DG_LOG_DOMAIN="FuPluginTelinkDfu"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('telink-dfu.quirk')
plugin_builtin_telink_dfu = static_library(
  'fu_plugin_telink_dfu',
  rustgen.process('fu-telink-dfu.rs'),
  sources: [
    'fu-telink-dfu-archive.c',
    'fu-telink-dfu-common.c',
    'fu-telink-dfu-ble-device.c',
    'fu-telink-dfu-hid-device.c',
    'fu-telink-dfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_telink_dfu

device_tests += files('tests/telink-ryder-dongle.json')

if get_option('tests')
  e = executable(
    'telink-dfu-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_telink_dfu,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'telink-dfu-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif

