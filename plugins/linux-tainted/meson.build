if host_machine.system() != 'linux' or not hsi
  subdir_done()
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxTainted"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_linux_tainted',
  sources: [
    'fu-linux-tainted-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
