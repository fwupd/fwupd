if get_option('plugin_mtd').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginMtd"']

plugin_quirks += join_paths(meson.current_source_dir(), 'mtd.quirk')

shared_module('fu_plugin_mtd',
  fu_hash,
  sources: [
    'fu-plugin-mtd.c',
    'fu-mtd-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'mtd-self-test',
    sources: [
      'fu-self-test.c',
      'fu-mtd-device.c',
    ],
    include_directories: [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    c_args: cargs,
    dependencies: [
      plugin_deps,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
    ],
    install: true,
    install_dir: installed_test_bindir,
  )
  test('mtd-self-test', e)  # added to installed-tests
endif
endif
