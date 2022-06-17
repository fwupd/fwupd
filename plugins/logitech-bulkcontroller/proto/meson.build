
gen = generator(protoc, \
  output: ['@BASENAME@.pb-c.c', '@BASENAME@.pb-c.h'],
  arguments: ['--proto_path=@CURRENT_SOURCE_DIR@', '--c_out=@BUILD_DIR@', '@INPUT@'])

src = [
       'antiflicker.proto',
       'ble_cfg.proto',
       'crash_info.proto',
       'device_attestation.proto',
       'device_common.proto',
       'device_info.proto',
       'device_mode.proto',
       'device_request.proto',
       'device_time.proto',
       'firmware_update.proto',
       'rightsight.proto',
       'ota_manifest.proto',
       'usb_msg.proto',
       ]
generated = gen.process(src)
