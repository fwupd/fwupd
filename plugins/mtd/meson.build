if get_option('plugin_mtd').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginMtd"']

plugin_quirks += files('mtd.quirk')
plugin_builtin_mtd = static_library('fu_plugin_mtd',
  sources: [
    'fu-mtd-plugin.c',
    'fu-mtd-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_mtd

if get_option('tests')
  e = executable(
    'mtd-self-test',
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    c_args: cargs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_mtd,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_dir: installed_test_bindir,
  )
  test('mtd-self-test', e)  # added to installed-tests
endif
endif
