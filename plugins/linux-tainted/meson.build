if hsi
  cargs = ['-DG_LOG_DOMAIN="FuPluginLinuxTainted"']

  plugin_builtins += static_library(
    'fu_plugin_linux_tainted',
    sources: [
      'fu-linux-tainted-plugin.c',
    ],
    include_directories: plugin_incdirs,
    link_with: plugin_libs,
    c_args: cargs,
    dependencies: plugin_deps,
  )
endif
