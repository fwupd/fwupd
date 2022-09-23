if gusb.found() and protobufc.found() and protoc.found()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechBulkController"']

plugin_quirks += files('logitech-bulkcontroller.quirk')

subdir('proto')
plugin_builtins += static_library('fu_plugin_logitech_bulkcontroller',
  sources: [
    generated,
    'fu-logitech-bulkcontroller-common.c',
    'fu-logitech-bulkcontroller-device.c',
    'fu-logitech-bulkcontroller-plugin.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
endif
