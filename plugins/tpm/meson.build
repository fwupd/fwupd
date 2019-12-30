cargs = ['-DG_LOG_DOMAIN="FuPluginTpm"']

install_data([
  'tpm.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_tpm',
  fu_hash,
  sources : [
    'fu-plugin-tpm.c',
    'fu-tpm-device.c',
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
    fwupd,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
    tpm2tss,
  ],
)
