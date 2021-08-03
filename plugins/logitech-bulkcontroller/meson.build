if get_option('plugin_logitech_bulkcontroller')
cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechBulkController"']

install_data(['logitech-bulkcontroller.quirk'],
  install_dir: join_paths(get_option('datadir'), 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_logitech_bulkcontroller',
  fu_hash,
  sources : [
    'antiflicker.pb-c.c',
    'ble_cfg.pb-c.c',
    'crash_info.pb-c.c',
    'device_attestation.pb-c.c',
    'device_common.pb-c.c',
    'device_info.pb-c.c',
    'device_mode.pb-c.c',
    'device_time.pb-c.c',
    'device_request.pb-c.c',
    'firmware_update.pb-c.c',
    'ota_manifest.pb-c.c',
    'rightsight.pb-c.c',
    'usb_msg.pb-c.c',
    'bulk_controller.c',
    'proto_manager.c',
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

