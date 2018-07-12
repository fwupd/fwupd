colorhug_test_firmware = custom_target('colorhug-test-firmware',
  input : [
    'firmware.bin',
    'firmware.bin.asc',
    'firmware.metainfo.xml',
  ],
  output : 'colorhug-als-3.0.2.cab',
  command : [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
)

if get_option('pkcs7')
  # generate self-signed detached signature
  colorhug_pkcs7_signature = custom_target('firmware.bin.p7c',
    input: 'firmware.bin',
    output: 'firmware.bin.p7c',
    command: [certtool, '--p7-detached-sign',
                        '--p7-time',
                        '--load-privkey', pkcs7_privkey,
                        '--load-certificate', pkcs7_certificate,
                        '--infile', '@INPUT@',
                        '--outfile', '@OUTPUT@'],
  )
endif
