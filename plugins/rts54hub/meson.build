cargs = ['-DG_LOG_DOMAIN="FuPluginRts54hub"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('rts54hub.quirk')
plugin_builtins += static_library('fu_plugin_rts54hub',
  sources: [
    'fu-rts54hub-device.c',
    'fu-rts54hub-rtd21xx-device.c',
    'fu-rts54hub-rtd21xx-background.c',
    'fu-rts54hub-rtd21xx-foreground.c',
    'fu-rts54hub-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/realtek-rts5423.json',
)
