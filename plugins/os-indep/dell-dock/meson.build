cargs = ['-DG_LOG_DOMAIN="FuPluginDellDock"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('dell-dock.quirk')
plugin_builtins += static_library('fu_plugin_dell_dock',
  sources: [
    'fu-dell-dock-plugin.c',
    'fu-dell-dock-common.c',
    'fu-dell-dock-hid.c',
    'fu-dell-dock-status.c',
    'fu-dell-dock-ec.c',
    'fu-dell-dock-hub.c',
    'fu-dell-dock-tbt.c',
    'fu-dell-dock-mst.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
