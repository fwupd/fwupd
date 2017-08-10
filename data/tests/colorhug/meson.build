gcab = find_program('gcab', required : false)
if gcab.found()
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
endif

# generate detached signature
colorhug_pkcs7_signature = custom_target('firmware.bin.p7b',
  input: 'firmware.bin',
  output: 'firmware.bin.p7b',
  command: [certtool, '--p7-detached-sign',
                      '--load-privkey', pkcs7_privkey,
                      '--load-certificate', pkcs7_certificate,
                      '--infile', '@INPUT@',
                      '--outfile', '@OUTPUT@'],
)
