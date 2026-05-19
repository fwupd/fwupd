
cargs = ['-DG_LOG_DOMAIN="FuPluginStStm32"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('st-stm32.quirk')
plugin_builtin_st_stm32 = static_library(
  'fu_plugin_st_stm32',
  rustgen.process('fu-st-stm32.rs'),
  sources: [
    'fu-st-stm32-common.c',
    'fu-st-stm32-device.c',
    'fu-st-stm32-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_st_stm32

device_tests += files(
  'tests/st-stm32-setup.json',
  'tests/st-stm32.json',
)

if get_option('tests')
  e = executable(
    'st-stm32-self-test',
    rustgen.process('fu-st-stm32.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_st_stm32,
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
    'st-stm32-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
