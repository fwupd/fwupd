if host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginPixartRf"']

install_data(['pixart-rf.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_pixart_rf',
  fu_hash,
  sources : [
    'fu-plugin-pixart-rf.c',
    'fu-pxi-common.c',
    'fu-pxi-ble-device.c',
    'fu-pxi-receiver-device.c',
    'fu-pxi-wireless-device.c',
    'fu-pxi-firmware.c',      # fuzzing
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
)
endif

if get_option('tests')
  e = executable(
    'pxi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-pxi-firmware.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    install : true,
    install_dir : installed_test_bindir,
  )
  test('pxi-self-test', e)
endif
