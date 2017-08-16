tar = find_program('tar', required : false)
if tar.found()
  raspberrypi_firmware_files = [
    'README',
    'start.elf',
    'overlays/test.dtb',
  ]
  raspberrypi_firmware_bin = custom_target('raspberrypi-firmware-tar',
    input : raspberrypi_firmware_files,
    output : 'firmware.bin',
    command : [
      tar, '--directory=' + meson.current_source_dir(),
      '-cf', '@OUTPUT@', raspberrypi_firmware_files,
    ],
  )
endif

gcab = find_program('gcab', required : false)
if gcab.found()
  raspberrypi_firmware_cab = custom_target('raspberrypi-firmware-cab',
    input : [
      'firmware.bin',
      'firmware.metainfo.xml',
    ],
    output : 'rpi-firmware-20150805.cab',
    command : [
      gcab, '--create', '--nopath', '@OUTPUT@', '@INPUT@',
    ],
  )
endif
