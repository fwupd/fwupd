cargs = ['-DG_LOG_DOMAIN="FuPluginAsusHid"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('asus-hid.quirk')
plugin_builtins += static_library('fu_plugin_asus_hid',
  rustgen.process('fu-asus-hid.rs'),
  sources: [
    'fu-asus-hid-device.c',
    'fu-asus-hid-plugin.c',
    'fu-asus-hid-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
