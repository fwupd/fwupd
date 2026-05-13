cargs = ['-DG_LOG_DOMAIN="FuPluginWchCh341a"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('wch-ch341a.quirk')
plugin_builtins += static_library('fu_plugin_wch_ch341a',
  rustgen.process('fu-wch-ch341a.rs'),
  sources: [
    'fu-wch-ch341a-cfi-device.c',
    'fu-wch-ch341a-device.c',
    'fu-wch-ch341a-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files('tests/wch-ch341a.json')
