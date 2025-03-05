cargs = ['-DG_LOG_DOMAIN="FuPluginHughskiColorhug"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('hughski-colorhug.quirk')
plugin_builtins += static_library('fu_plugin_hughski_colorhug',
  rustgen.process('fu-hughski-colorhug.rs'),
  sources: [
    'fu-hughski-colorhug-device.c',
    'fu-hughski-colorhug-plugin.c',
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
