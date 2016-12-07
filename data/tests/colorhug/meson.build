# fixme

gcab = find_program('gcab', required : false)
if gcab.found()
  custom_target('colorhug-test-firmware',
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
