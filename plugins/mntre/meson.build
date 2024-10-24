cargs = ['-DG_LOG_DOMAIN="FuPluginMntre"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('mntre.quirk')
plugin_builtins += static_library('fu_plugin_mntre',
  sources: [
    'fu-mntre-device.c',
    'fu-mntre-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
