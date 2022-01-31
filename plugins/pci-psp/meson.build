if get_option('hsi') and \
  host_machine.system() == 'linux' and \
  host_cpu == 'x86_64'
cargs = ['-DG_LOG_DOMAIN="FuPluginPciPsp"']

install_data(['pci-psp.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_pci_psp',
  fu_hash,
  sources : [
    'fu-plugin-pci-psp.c',
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
endif
