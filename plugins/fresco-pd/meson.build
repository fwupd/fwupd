cargs = ['-DG_LOG_DOMAIN="FuPluginFrescoPd"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('fresco-pd.quirk')
plugin_builtin_fresco_pd = static_library(
  'fu_plugin_fresco_pd',
  sources: [
    'fu-fresco-pd-plugin.c',
    'fu-fresco-pd-common.c',
    'fu-fresco-pd-device.c',
    'fu-fresco-pd-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_fresco_pd

enumeration_data += files('tests/fresco-pd-setup.json')
device_tests += files(
  'tests/fresco-pd.json',
  'tests/ugreen-cm260.json',
)

if get_option('tests')
  e = executable(
    'fresco-pd-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_fresco_pd,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test(
    'fresco-pd-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
