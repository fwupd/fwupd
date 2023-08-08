if get_option('plugin_intel_spi')

if not lzma.found() or \
   host_machine.system() != 'linux' or \
   (host_cpu != 'x86' and host_cpu != 'x86_64')
    error('unsupported configuration for intel_spi')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelSpi"']

plugin_quirks += files('intel-spi.quirk')
plugin_builtins += static_library('fu_plugin_intel_spi',
  rustgen.process('fu-intel-spi.rs'),
  sources: [
    'fu-ifd-device.c',
    'fu-intel-spi-common.c',
    'fu-intel-spi-device.c',
    'fu-pci-device.c',
    'fu-intel-spi-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
