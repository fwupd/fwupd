if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginPciMei"']

plugin_quirks += files('pci-mei.quirk')
plugin_builtins += static_library('fu_plugin_pci_mei',
  rustgen.process(
    'fu-mei.rs',
  ),
  sources: [
    'fu-pci-mei-plugin.c',
    'fu-mei-common.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
