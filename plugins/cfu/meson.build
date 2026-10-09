cargs = ['-DG_LOG_DOMAIN="FuPluginCfu"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('cfu.quirk')
plugin_builtin_cfu = static_library(
  'fu_plugin_cfu',
  sources: [
    'fu-cfu-device.c',
    'fu-cfu-firmware.c',
    'fu-cfu-module.c',
    'fu-cfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_cfu

enumeration_data += files('tests/microsoft-usbc-travel-hub-setup.json')
device_tests += files('tests/microsoft-usbc-travel-hub.json')

if get_option('tests')
  e = executable(
    'cfu-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_cfu,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'cfu-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
