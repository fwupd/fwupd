host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechTap"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-tap.quirk')

plugin_builtins += static_library('fu_plugin_logitech_tap',
  rustgen.process('fu-logitech-tap.rs'),
  sources: [
    'fu-logitech-tap-plugin.c',
    'fu-logitech-tap-hdmi-device.c',
    'fu-logitech-tap-sensor-device.c',
    'fu-logitech-tap-touch-device.c',
    'fu-logitech-tap-touch-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
