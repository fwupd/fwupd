if host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginIommu"']

install_data([
  'iommu.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_iommu',
  fu_hash,
  sources : [
    'fu-plugin-iommu.c',
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
  ],
)
endif
