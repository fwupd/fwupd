if get_option('plugin_kinetic_mst')
if not get_option('gudev')
  error('gudev is required for plugin_kinetic_mst')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginKineticMST"']

#install_data(['kinetic-mst.quirk'],
#  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
#)

shared_module('fu_plugin_kinetic_mst',
  fu_hash,
  sources : [
    'fu-plugin-kinetic-mst.c',
    'fu-kinetic-mst-common.c',
    'fu-kinetic-mst-connection.c',
    'fu-kinetic-mst-device.c',
    'fu-kinetic-mst-firmware.c',      # fuzzing
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
  ],
)
endif
