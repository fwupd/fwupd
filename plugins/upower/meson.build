if host_machine.system() == 'linux'
plugins += {meson.current_source_dir().split('/')[-1]: true}
cargs = ['-DG_LOG_DOMAIN="FuPluginUpower"']

plugin_builtins += static_library('fu_plugin_upower',
  sources: [
    'fu-upower-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

endif
