cargs = ['-DG_LOG_DOMAIN="FuPluginUefiRecovery"']

install_data(['uefi-recovery.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_uefi_recovery',
  fu_hash,
  sources : [
    'fu-plugin-uefi-recovery.c',
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
  ],
  dependencies : [
    plugin_deps,
  ],
)
