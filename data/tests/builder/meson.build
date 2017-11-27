if get_option('tests')
  tar = find_program('tar')
  builder_test_firmware = custom_target('builder-test-firmware',
    input : [
      'source.bin',
      'startup.sh',
    ],
    output : 'firmware.tar',
    command : [
      tar, '--xform', 's,.*/,,',
           '--absolute-names', '--create', '--file', '@OUTPUT@', '@INPUT@',
    ],
  )
endif
