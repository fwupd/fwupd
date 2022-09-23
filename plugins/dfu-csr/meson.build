if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDfuCsr"']

plugin_quirks += files('dfu-csr.quirk')
plugin_builtins += static_library('fu_plugin_dfu_csr',
  sources: [
    'fu-dfu-csr-device.c',
    'fu-dfu-csr-plugin.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
    plugindfu_incdir,
  ],
  c_args: cargs,
  dependencies: plugin_deps,
  link_with: [
    fwupdplugin,
    plugin_builtin_dfu,
  ],
)
endif
