cargs = ['-DG_LOG_DOMAIN="FuPluginMm"']

install_data(['modem-manager.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_modem_manager',
  fu_hash,
  sources : [
    'fu-plugin-modem-manager.c',
    'fu-mm-device.c',
    'fu-qmi-pdc-updater.c',
    'fu-mm-utils.c'
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
    libmm_glib,
    libqmi_glib,
  ],
)
