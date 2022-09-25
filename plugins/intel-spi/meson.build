if get_option('plugin_intel_spi')

if not lzma.found() or \
   host_machine.system() != 'linux' or \
   (host_cpu != 'x86' and host_cpu != 'x86_64')
    error('unsupported configuration for intel_spi')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelSpi"']

plugin_quirks += join_paths(meson.current_source_dir(), 'intel-spi.quirk')

shared_module('fu_plugin_intel_spi',
  fu_hash,
  sources: [
    'fu-ifd-device.c',
    'fu-intel-spi-common.c',
    'fu-intel-spi-device.c',
    'fu-pci-device.c',
    'fu-plugin-intel-spi.c',
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
