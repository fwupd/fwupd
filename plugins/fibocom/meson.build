if gusb.found()
cargs = ['-DG_LOG_DOMAIN="FuPluginFibocom"']

plugin_builtins += static_library('fu_plugin_fibocom',
  sources: [
    'fu-fibocom-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
