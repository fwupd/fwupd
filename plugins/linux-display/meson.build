if host_machine.system() != 'linux'
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxDisplay"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_linux_display',
  sources: [
    'fu-linux-display-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
