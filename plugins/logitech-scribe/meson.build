if get_option('plugin_logitech_scribe').require(enable_udev,
    error_message: 'usb and udev are needed for plugin_logitech_scribe').disable_auto_if(host_machine.system() != 'linux').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechScribe"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

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
