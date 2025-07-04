cargs = ['-DG_LOG_DOMAIN="FuPluginQsiDock"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('qsi-dock.quirk')
plugin_builtins += static_library('fu_plugin_qsi_dock',
  sources: [
    'fu-qsi-dock-child-device.c',
    'fu-qsi-dock-mcu-device.c',
    'fu-qsi-dock-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
