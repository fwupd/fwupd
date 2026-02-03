cargs = ['-DG_LOG_DOMAIN="FuPluginBlestechtp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('blestechtp.quirk')
plugin_builtins += static_library('fu_plugin_blestechtp',
  rustgen.process('fu-blestechtp.rs'),
  sources: [
    'fu-blestechtp-firmware.c',
    'fu-blestechtp-hid-device.c',
    'fu-blestechtp-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
