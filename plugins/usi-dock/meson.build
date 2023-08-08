if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginUsiDock"']

plugin_quirks += files('usi-dock.quirk')
plugin_builtins += static_library('fu_plugin_usi_dock',
  rustgen.process('fu-usi-dock.rs'),
  sources: [
    'fu-usi-dock-child-device.c',
    'fu-usi-dock-dmc-device.c',
    'fu-usi-dock-mcu-device.c',
    'fu-usi-dock-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
