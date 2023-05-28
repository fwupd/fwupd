if hsi and (host_cpu == 'x86' or host_cpu == 'x86_64')
  cargs = ['-DG_LOG_DOMAIN="FuPluginPciPsp"']

  plugin_quirks += files('pci-psp.quirk')
  plugin_builtins += static_library(
    'fu_plugin_pci_psp',
    sources: [
      'fu-pci-psp-plugin.c',
      'fu-pci-psp-device.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif
