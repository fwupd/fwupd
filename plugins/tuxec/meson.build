if get_option('plugin_tuxec')
cargs = ['-DG_LOG_DOMAIN="FuPluginTuxec"']

install_data(['tuxec.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_tuxec',
  fu_hash,
  sources : [
    'fu-tuxec-device.c',
    'fu-tuxec-internal-device.c',
    'fu-plugin-tuxec.c',
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
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies : [
    plugin_deps,
    libflashrom,
  ],
)
endif
