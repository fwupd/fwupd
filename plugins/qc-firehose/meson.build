cargs = ['-DG_LOG_DOMAIN="FuPluginQcFirehose"']

plugins += {meson.current_source_dir().split('/')[-1]: true}
plugin_quirks += files('qc-firehose.quirk')
plugin_builtins += static_library('fu_plugin_qc_firehose',
  rustgen.process('fu-qc-firehose.rs'),
  sources: [
    'fu-qc-firehose-impl.c',
    'fu-qc-firehose-sahara-impl.c',
    'fu-qc-firehose-usb-device.c',
    'fu-qc-firehose-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

device_tests += files(
  'tests/qc-ec25au.json',
  'tests/qc-eg25ggc.json',
)
enumeration_data += files(
  'tests/qc-ec25au-setup.json',
)
