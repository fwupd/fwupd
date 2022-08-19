if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginPciMei"']

plugin_quirks += join_paths(meson.current_source_dir(), 'pci-mei.quirk')

shared_module('fu_plugin_pci_mei',
  fu_hash,
  sources: [
    'fu-plugin-pci-mei.c',
    'fu-mei-common.c',
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
