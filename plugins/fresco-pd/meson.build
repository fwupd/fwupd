cargs = ['-DG_LOG_DOMAIN="FuPluginFrescoPd"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('fresco-pd.quirk')
plugin_builtins += static_library('fu_plugin_fresco_pd',
  sources: [
    'fu-fresco-pd-plugin.c',
    'fu-fresco-pd-common.c',
    'fu-fresco-pd-device.c',
    'fu-fresco-pd-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files('tests/fresco-pd-setup.json')
enumeration_tests += files('tests/fresco-pd.json')
