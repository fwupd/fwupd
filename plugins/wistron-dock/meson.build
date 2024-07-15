plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginWistronDock"']

plugin_quirks += files('wistron-dock.quirk')
plugin_builtins += static_library('fu_plugin_wistron_dock',
  rustgen.process('fu-wistron-dock.rs'),
  sources: [
    'fu-wistron-dock-device.c',
    'fu-wistron-dock-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
