cargs = ['-DG_LOG_DOMAIN="FuPluginRts54hub"']
plugins += {
  meson.current_source_dir().split('/')[-1]: true,
}

plugin_quirks += files('realtek-rts54hub.quirk')
plugin_builtins += static_library(
  'fu_plugin_realtek_rts54hub',
  rustgen.process('fu-realtek-rts54hub.rs'),
  sources: [
    'fu-realtek-rts54hub-device.c',
    'fu-realtek-rts54hub-rtd21xx-device.c',
    'fu-realtek-rts54hub-rtd21xx-background.c',
    'fu-realtek-rts54hub-rtd21xx-foreground.c',
    'fu-realtek-rts54hub-rtd21xx-mergeinfo.c',
    'fu-realtek-rts54hub-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/realtek-br1u3aa.json',
  'tests/realtek-rts5423.json',
)
