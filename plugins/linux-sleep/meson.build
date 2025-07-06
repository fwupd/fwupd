if host_machine.system() != 'linux'
  subdir_done()
endif
if not hsi
  subdir_done()
endif
if host_cpu not in ['x86', 'x86_64']
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxSleep"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_builtins += static_library('fu_plugin_linux_sleep',
  sources: [
    'fu-linux-sleep-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
