host_machine.system() in ['linux', 'android'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginTcgOpal"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_builtin_tcg_opal = static_library(
  'fu_plugin_tcg_opal',
  rustgen.process('fu-tcg-opal.rs'),
  sources: [
    'fu-tcg-opal-plugin.c',
    'fu-tcg-opal-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_tcg_opal

if get_option('tests')
  e = executable(
    'tcg-opal-self-test',
    rustgen.process('fu-tcg-opal.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_tcg_opal,
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
    'tcg-opal-self-test',
    e,
    env: {
      'FWUPD_DATADIR_QUIRKS': meson.current_source_dir(),
      'G_TEST_BUILDDIR': meson.current_build_dir(),
      'G_TEST_SRCDIR': meson.current_source_dir(),
    },
  )
endif
