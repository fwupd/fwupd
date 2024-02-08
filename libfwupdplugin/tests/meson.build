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
  install_dir: join_paths(installed_test_datadir, 'tests'),
)

install_data([
    'America/New_York',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/America'),
)
install_data([
    'cab.bin',
    'cab.builder.xml',
    'cab-compressed.bin',
    'cab-compressed.builder.xml',
    'cfu-offer.builder.xml',
    'cfu-payload.builder.xml',
    'chassis_type',
    'csv.bin',
    'csv.builder.xml',
    'dfuse.bin',
    'dfuse.builder.xml',
    'edid.bin',
    'edid.builder.xml',
    'efi-filesystem.builder.xml',
    'efi-file.builder.xml',
    'efi-filesystem.bin',
    'efi-load-option.bin',
    'efi-load-option.builder.xml',
    'efi-lz77-legacy.bin',
    'efi-lz77-tiano.bin',
    'efi-section.builder.xml',
    'efi-volume.bin',
    'efi-volume.builder.xml',
    'fdt.bin',
    'fdt.builder.xml',
    'firmware.bin',
    'firmware.dfu',
    'firmware.dfuse',
    'firmware.hex',
    'firmware.shex',
    'firmware.srec',
    'fit.bin',
    'fit.builder.xml',
    'fmap.bin',
    'fmap.builder.xml',
    'fmap-offset.bin',
    'hid-descriptor2.bin',
    'hid-descriptor.bin',
    'hid-descriptor.builder.xml',
    'hid-report-item.builder.xml',
    'ifd.bin',
    'ifd-bios.bin',
    'ifd-no-bios.bin',
    'ifd.builder.xml',
    'ifwi-cpd.bin',
    'ifwi-cpd.builder.xml',
    'ifwi-fpt.bin',
    'ifwi-fpt.builder.xml',
    'ihex.bin',
    'ihex.builder.xml',
    'intel-thunderbolt.bin',
    'intel-thunderbolt.builder.xml',
    'linear.bin',
    'linear.builder.xml',
    'metadata.xml',
    'oprom.bin',
    'oprom.builder.xml',
    'srec-addr32.bin',
    'srec.bin',
    'srec.builder.xml',
    'sys_vendor',
    'uswid.bin',
    'uswid.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests')
)
install_data([
    'dmi/tables/DMI',
    'dmi/tables/smbios_entry_point',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/dmi/tables'),
)
install_data([
    'dmi/tables64/DMI',
    'dmi/tables64/smbios_entry_point',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/dmi/tables64'),
)
install_data([
    'lockdown/locked/lockdown',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/lockdown/locked'),
)
install_data([
    'lockdown/none/lockdown',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/lockdown/none'),
)
install_data([
    'quirks.d/tests.quirk',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/quirks.d'),
)
install_symlink('localtime',
  install_dir: join_paths(installed_test_datadir, 'tests'),
  pointing_to: join_paths('America', 'New_York'),
)
