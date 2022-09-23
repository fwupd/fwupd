if get_option('plugin_amt').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginAmt"']

plugin_quirks += files('amt.quirk')
plugin_builtins += static_library('fu_plugin_amt',
  sources: [
    'fu-amt-plugin.c',
    'fu-amt-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
