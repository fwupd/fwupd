cargs = ['-DG_LOG_DOMAIN="FuPluginFastboot"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('fastboot.quirk')
plugin_builtin_fastboot = static_library(
  'fu_plugin_fastboot',
  sources: [
    'fu-fastboot-plugin.c',
    'fu-fastboot-device.c',
    'fu-fastboot-rolling-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_fastboot

if not supported_build
  plugin_quirks += files('ci.quirk')
  enumeration_data += files('tests/fastboot-google-sargo-setup.json')
  device_tests += files('tests/fastboot-google-sargo.json')
endif

if get_option('tests')
  e = executable(
    'fastboot-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_fastboot,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'fastboot-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
