if get_option('hsi') and \
  host_machine.system() == 'linux' and \
  host_cpu == 'x86_64'
cargs = ['-DG_LOG_DOMAIN="FuPluginPciPsp"']

plugin_quirks += join_paths(meson.current_source_dir(), 'pci-psp.quirk')

shared_module('fu_plugin_pci_psp',
  fu_hash,
  sources: [
    'fu-plugin-pci-psp.c',
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
