colorhug_test_firmware = custom_target('colorhug-test-firmware',
  input: [
    'firmware.txt',
    'firmware.metainfo.xml',
  ],
  output: 'colorhug-als-3.0.2.cab',
  command: [
    gcab, '--create', '@OUTPUT@', '@INPUT@',
  ],
  install: true,
  install_dir: join_paths(installed_test_datadir, 'tests/colorhug'),
)
