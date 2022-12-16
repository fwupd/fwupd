subdir('colorhug')
subdir('efi')

installed_firmware_zip = custom_target('installed-firmware-zip',
  input: [
    'colorhug/firmware.bin',
    'colorhug/firmware.bin.asc',
  ],
  output: 'firmware.zip',
  command: [
    python3, '-m', 'zipfile', '-c', '@OUTPUT@', '@INPUT@',
  ],
  install: true,
  install_dir: installed_test_datadir,
)
