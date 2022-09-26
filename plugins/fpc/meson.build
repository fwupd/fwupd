if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginFpc"']

plugin_quirks += join_paths(meson.current_source_dir(), 'fpc.quirk')

shared_module('fu_plugin_fpc',
  fu_hash,
  sources: [
    'fu-fpc-device.c',
    'fu-plugin-fpc.c',
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
