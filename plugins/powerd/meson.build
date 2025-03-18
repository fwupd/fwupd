cargs = ['-DG_LOG_DOMAIN="FuPluginPowerd"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_powerd',
  sources: [
    'fu-powerd-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
