if libsystemd.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginLogind"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_logind',
  sources: [
    'fu-logind-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
