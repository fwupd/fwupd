cargs = ['-DG_LOG_DOMAIN="FuPluginWchCh347"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('wch-ch347.quirk')
plugin_builtins += static_library('fu_plugin_wch_ch347',
  rustgen.process('fu-wch-ch347.rs'),
  sources: [
    'fu-wch-ch347-cfi-device.c',
    'fu-wch-ch347-device.c',
    'fu-wch-ch347-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/wch-ch347-setup.json')
device_tests += files('tests/wch-ch347.json')
