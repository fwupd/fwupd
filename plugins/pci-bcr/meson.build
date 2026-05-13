hsi or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginPciBcr"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('pci-bcr.quirk')
plugin_builtins += static_library('fu_plugin_pci_bcr',
  sources: [
    'fu-pci-bcr-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
