cargs = ['-DG_LOG_DOMAIN="FuPluginHailuck"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('hailuck.quirk')
plugin_builtins += static_library('fu_plugin_hailuck',
  rustgen.process('fu-hailuck.rs'),
  sources: [
    'fu-hailuck-bl-device.c',
    'fu-hailuck-kbd-device.c',
    'fu-hailuck-kbd-firmware.c',  # fuzzing
    'fu-hailuck-tp-device.c',
    'fu-hailuck-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/hailuck-setup.json')
enumeration_tests += files('tests/hailuck.json')
