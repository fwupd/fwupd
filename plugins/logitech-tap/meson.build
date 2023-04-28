if get_option('plugin_logitech_tap').require(gusb.found(),
    error_message: 'usb is needed for plugin_logitech_tap').disable_auto_if(host_machine.system() != 'linux').allowed()    
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechTap"']

plugin_quirks += files('logitech-tap.quirk')

plugin_builtins += static_library('fu_plugin_logitech_tap',
  sources: [
    'fu-logitech-tap-plugin.c',
    'fu-logitech-tap-device.c',
    'fu-logitech-tap-hdmi-device.c',
    'fu-logitech-tap-sensor-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
