if get_option('plugin_logitech_scribe').require(gusb.found(),
    error_message: 'usb is needed for plugin_logitech_scribe').disable_auto_if(host_machine.system() != 'linux').allowed()    
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechScribe"']

plugin_quirks += files('logitech-scribe.quirk')

plugin_builtins += static_library('fu_plugin_logitech_scribe',
  sources: [
    'fu-logitech-scribe-device.c',
    'fu-logitech-scribe-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif