hwid_test_firmware = custom_target('hwid-test-firmware',
  input : [
    'firmware.bin',
    'firmware.metainfo.xml',
  ],
  output : 'hwid-1.2.3.cab',
  command : [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
)
noreqs_test_firmware = custom_target('noreqs-test-firmware',
  input : [
    'firmware.bin',
    'firmware2.metainfo.xml',
  ],
  output : 'noreqs-1.2.3.cab',
  command : [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
)
