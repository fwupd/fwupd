cargs = ['-DG_LOG_DOMAIN="FuPluginJabra"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('jabra.quirk')
plugin_builtins += static_library('fu_plugin_jabra',
  sources: [
    'fu-jabra-plugin.c',
    'fu-jabra-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
