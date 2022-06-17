if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginDfuCsr"']

plugin_quirks += join_paths(meson.current_source_dir(), 'dfu-csr.quirk')

shared_module('fu_plugin_dfu_csr',
  fu_hash,
  sources: [
    'fu-dfu-csr-device.c',
    'fu-plugin-dfu-csr.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
    plugindfu_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
  link_with: [
    fwupdplugin,
    dfu,
  ],
)
endif
