protobufc.found() or subdir_done()
protoc.found() or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechBulkController"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-bulkcontroller.quirk')

gen = generator(protoc, \
  output: ['@BASENAME@.pb-c.c', '@BASENAME@.pb-c.h'],
  arguments: ['--proto_path=@CURRENT_SOURCE_DIR@', '--c_out=@BUILD_DIR@', '@INPUT@'])
generated = gen.process([
 'device_common.proto', # fuzzing
 'antiflicker.proto', # fuzzing
 'ble_cfg.proto', # fuzzing
 'crash_info.proto', # fuzzing
 'device_attestation.proto', # fuzzing
 'device_info.proto', # fuzzing
 'device_mode.proto', # fuzzing
 'device_request.proto', # fuzzing
 'device_time.proto', # fuzzing
 'firmware_update.proto', # fuzzing
 'rightsight.proto', # fuzzing
 'ota_manifest.proto', # fuzzing
 'usb_msg.proto', # fuzzing
])

plugin_builtins += static_library('fu_plugin_logitech_bulkcontroller',
  rustgen.process(
    'fu-logitech-bulkcontroller.rs', # fuzzing
  ),
  sources: [
    generated,
    'fu-logitech-bulkcontroller-common.c', # fuzzing
    'fu-logitech-bulkcontroller-child.c', # fuzzing
    'fu-logitech-bulkcontroller-device.c', # fuzzing
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
