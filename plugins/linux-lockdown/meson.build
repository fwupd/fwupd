if hsi
cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxLockdown"']

plugin_builtins += static_library('fu_plugin_linux_lockdown',
  sources: [
    'fu-linux-lockdown-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
