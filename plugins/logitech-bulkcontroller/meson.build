if not (protobufc.found() and protoc.found())
  subdir_done()
endif

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechBulkController"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-bulkcontroller.quirk')

subdir('proto')
plugin_builtins += static_library('fu_plugin_logitech_bulkcontroller',
  rustgen.process('fu-logitech-bulkcontroller.rs'),
  sources: [
    generated,
    'fu-logitech-bulkcontroller-common.c',
    'fu-logitech-bulkcontroller-child.c',
    'fu-logitech-bulkcontroller-device.c',
    'fu-logitech-bulkcontroller-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)

enumeration_data += files(
  'tests/logi-rally-bar-setup.json',
)
device_tests += files(
  'tests/logi-rally-bar.json',
)
