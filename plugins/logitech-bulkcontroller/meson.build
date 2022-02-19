if gusb.found() and protobufc.found() and protoc.found()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechBulkController"']

install_data(['logitech-bulkcontroller.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)
subdir('proto')
shared_module('fu_plugin_logitech_bulkcontroller',
  fu_hash,
  sources : [
    generated,
    'fu-logitech-bulkcontroller-common.c',
    'fu-logitech-bulkcontroller-device.c',
    'fu-plugin-logitech-bulkcontroller.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
endif
