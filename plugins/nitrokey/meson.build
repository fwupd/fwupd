if get_option('plugin_nitrokey').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginNitrokey"']

install_data(['nitrokey.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_nitrokey',
  fu_hash,
  sources : [
    'fu-nitrokey-device.c',
    'fu-nitrokey-common.c',
    'fu-plugin-nitrokey.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  e = executable(
    'nitrokey-self-test',
    fu_hash,
    sources : [
      'fu-nitrokey-common.c',
      'fu-self-test.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      valgrind,
    ],
    link_with : [
      fwupdplugin,
    ],
    install : true,
    install_dir : installed_test_bindir,
  )
  test('nitrokey-self-test', e)  # added to installed-tests
endif
endif
