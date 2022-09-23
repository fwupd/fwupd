if get_option('plugin_ep963x').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginEp963x"']

plugin_quirks += files('ep963x.quirk')
plugin_builtins += static_library('fu_plugin_ep963x',
  sources: [
    'fu-ep963x-common.c',
    'fu-ep963x-device.c',
    'fu-ep963x-firmware.c',
    'fu-ep963x-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
