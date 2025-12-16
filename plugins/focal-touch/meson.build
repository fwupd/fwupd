host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginFocalTouch"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focal-touch.quirk')
plugin_builtins += static_library('fu_plugin_focal_touch',
  rustgen.process('fu-focal-touch.rs'),
  sources: [
    'fu-focal-touch-plugin.c',
    'fu-focal-touch-firmware.c',
    'fu-focal-touch-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)

device_tests += files('tests/focal-touch.json')
