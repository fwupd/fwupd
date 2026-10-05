cargs = ['-DG_LOG_DOMAIN="FuPluginHpiCfu"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('hpi-cfu.quirk')
plugin_builtin_hpi_cfu = static_library(
  'fu_plugin_hpi_cfu',
  rustgen.process('fu-hpi-cfu.rs'),
  sources: [
    'fu-hpi-cfu-device.c',
    'fu-hpi-cfu-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_hpi_cfu

device_tests += files('tests/hp-dock-g6-ultra.json')

if get_option('tests')
  e = executable(
    'hpi-cfu-self-test',
    rustgen.process('fu-hpi-cfu.rs'),
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_hpi_cfu,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: cargs,
  )
  test('hpi-cfu-self-test', e)
endif
