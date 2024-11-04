cargs = ['-DG_LOG_DOMAIN="FuPluginColorHug"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('colorhug.quirk')
plugin_builtins += static_library('fu_plugin_colorhug',
  rustgen.process('fu-colorhug.rs'),
  sources: [
    'fu-colorhug-device.c',
    'fu-colorhug-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/hughski-colorhug.json',
                      'tests/hughski-colorhug2.json',
                      'tests/hughski-colorhug-plus.json',
)
