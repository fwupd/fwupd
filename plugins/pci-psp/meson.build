hsi or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginPciPsp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('pci-psp.quirk')
plugin_builtins += static_library('fu_plugin_pci_psp',
  sources: [
    'fu-pci-psp-plugin.c',
    'fu-pci-psp-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
umockdev_tests += files('pci_psp_test.py')
device_tests += files(
  'tests/pci-psp-strix-enumerate.json',
)
enumeration_data += files(
  'tests/pci-psp-strix.json',
)
