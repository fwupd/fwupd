cargs = ['-DG_LOG_DOMAIN="FuPluginIntelMkhi"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('intel-mkhi.quirk')
plugin_builtins += static_library('fu_plugin_intel_mkhi',
  sources: [
    'fu-intel-mkhi-plugin.c',
    'fu-intel-mkhi-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
