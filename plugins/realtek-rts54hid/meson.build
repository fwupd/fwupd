cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hid"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('realtek-rts54hid.quirk')
plugin_builtins += static_library(
  'fu_plugin_realtek_rts54hid',
  rustgen.process('fu-realtek-rts54hid.rs'),
  sources: [
    'fu-realtek-rts54hid-device.c',
    'fu-realtek-rts54hid-hub-device.c',
    'fu-realtek-rts54hid-pd-device.c',
    'fu-realtek-rts54hid-module.c',
    'fu-realtek-rts54hid-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
