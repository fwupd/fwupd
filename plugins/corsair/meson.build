if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginCorsair"']

plugin_quirks += join_paths(meson.current_source_dir(), 'corsair.quirk')

shared_module('fu_plugin_corsair',
  fu_hash,
  sources: [
    'fu-plugin-corsair.c',
    'fu-corsair-common.c',
    'fu-corsair-device.c',
    'fu-corsair-bp.c',
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
