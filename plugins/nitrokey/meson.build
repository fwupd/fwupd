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
  )
  test('nitrokey-self-test', e)
endif
