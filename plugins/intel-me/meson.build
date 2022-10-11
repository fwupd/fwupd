if get_option('plugin_amt').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginIntelMe"']

plugin_quirks += files('intel-me.quirk')
plugin_builtins += static_library('fu_plugin_intel_me',
  sources: [
    'fu-intel-me-common.c',
    'fu-intel-me-plugin.c',
    'fu-intel-me-heci-device.c',
    'fu-intel-me-mca-device.c',
    'fu-intel-me-mkhi-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
