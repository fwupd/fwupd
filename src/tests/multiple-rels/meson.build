multiple_rels_test_firmware = custom_target('multiple-rels-test-firmware',
  input : [
    'firmware-123.bin',
    'firmware-124.bin',
    'firmware.metainfo.xml',
  ],
  output : 'multiple-rels-1.2.4.cab',
  command : [
    gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
  ],
)
