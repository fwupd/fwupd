if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginPciBcr"']

plugin_quirks += join_paths(meson.current_source_dir(), 'pci-bcr.quirk')

shared_module('fu_plugin_pci_bcr',
  fu_hash,
  sources: [
    'fu-plugin-pci-bcr.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)
endif
