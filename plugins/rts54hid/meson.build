cargs = ['-DG_LOG_DOMAIN="FuPluginRts54Hid"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('rts54hid.quirk')
plugin_builtins += static_library('fu_plugin_rts54hid',
  rustgen.process('fu-rts54hid.rs'),
  sources: [
    'fu-rts54hid-device.c',
    'fu-rts54hid-module.c',
    'fu-rts54hid-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
