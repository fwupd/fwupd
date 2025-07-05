cargs = ['-DG_LOG_DOMAIN="FuPluginCrosEc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('cros-ec.quirk')
plugin_builtins += static_library('fu_plugin_cros_ec',
  rustgen.process(
    'fu-cros-ec.rs',            # fuzzing
  ),
  sources: [
    'fu-cros-ec-plugin.c',
    'fu-cros-ec-usb-device.c',
    'fu-cros-ec-hammer-touchpad.c',
    'fu-cros-ec-common.c',      # fuzzing
    'fu-cros-ec-firmware.c',    # fuzzing
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

#TODO: Add test for hammer
device_tests += files(
  'tests/acer-d501.json',
  'tests/google-servo-micro.json',
)
