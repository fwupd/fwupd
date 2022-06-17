if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginUsiDock"']

plugin_quirks += join_paths(meson.current_source_dir(), 'usi-dock.quirk')

shared_module('fu_plugin_usi_dock',
  fu_hash,
  sources: [
    'fu-usi-dock-common.c',
    'fu-usi-dock-child-device.c',
    'fu-usi-dock-dmc-device.c',
    'fu-usi-dock-mcu-device.c',
    'fu-plugin-usi-dock.c',
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
