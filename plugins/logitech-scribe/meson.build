if host_machine.system() == 'linux'
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechScribe"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-scribe.quirk')

plugin_builtins += static_library('fu_plugin_logitech_scribe',
  rustgen.process('fu-logitech-scribe.rs'),
  sources: [
    'fu-logitech-scribe-device.c',
    'fu-logitech-scribe-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files(
  'tests/logi-scribe-setup.json',
)
device_tests += files(
  'tests/logi-scribe.json',
)

endif
