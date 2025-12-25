host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginFocaltouch"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('focaltouch.quirk')
plugin_builtins += static_library('fu_plugin_focaltouch',
  rustgen.process('fu-focaltouch.rs'),
  sources: [
    'fu-focaltouch-plugin.c',
    'fu-focaltouch-firmware.c',
    'fu-focaltouch-hid-device.c',
  ],
  include_directories: plugin_incdirs,
  c_args: [
    cargs,
  ],
  link_with: plugin_libs,
  dependencies: plugin_deps,
)
