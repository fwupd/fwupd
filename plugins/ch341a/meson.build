cargs = ['-DG_LOG_DOMAIN="FuPluginCh341a"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('ch341a.quirk')
plugin_builtins += static_library('fu_plugin_ch341a',
  rustgen.process('fu-ch341a.rs'),
  sources: [
    'fu-ch341a-cfi-device.c',
    'fu-ch341a-device.c',
    'fu-ch341a-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/ch341a-setup.json')
device_tests += files('tests/ch341a.json')
