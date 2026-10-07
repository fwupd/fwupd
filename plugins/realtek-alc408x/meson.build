cargs = ['-DG_LOG_DOMAIN="FuPluginRealtekAlc408x"']

plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}
plugin_quirks += files('realtek-alc408x.quirk')
plugin_builtin_realtek_alc408x = static_library(
  'fu_plugin_realtek_alc408x',
  rustgen.process(
    'fu-realtek-alc408x.rs' # fuzzing
  ),
  sources: [
    'fu-realtek-alc408x-common.c', # fuzzing
    'fu-realtek-alc408x-device.c',
    'fu-realtek-alc408x-firmware.c', # fuzzing
    'fu-realtek-alc408x-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_realtek_alc408x

if get_option('tests')
  install_data(
    ['tests/realtek-alc408x.builder.xml'],
    install_dir: join_paths(installed_test_datadir, 'tests'),
  )
  e = executable(
    'realtek-alc408x-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_realtek_alc408x,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test(
    'realtek-alc408x-self-test',
    e,
    env: {
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
