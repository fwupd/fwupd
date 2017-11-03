gcab = find_program('gcab', required : false)
if gcab.found()
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
endif
