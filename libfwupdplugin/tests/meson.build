subdir('colorhug')
subdir('efi')

installed_firmware_zip = custom_target(
  'installed-firmware-zip',
  input: ['colorhug/firmware.txt'],
  output: 'firmware.zip',
  command: [python3, '-m', 'zipfile', '-c', '@OUTPUT@', '@INPUT@'],
  install: true,
  install_dir: join_paths(installed_test_datadir, 'tests'),
)

install_data(
  ['America/New_York'],
  install_dir: join_paths(installed_test_datadir, 'tests/America'),
)
install_data(
  [
    'acpi-table.builder.json',
    'cpuinfo',
    'cab.builder.json',
    'cab-compressed.builder.json',
    'cfu-offer.builder.json',
    'cfu-payload.builder.json',
    'chassis_type',
    'context.quirk',
    'coswid.builder.json',
    'csv.builder.json',
    'dfu.builder.json',
    'dfuse.builder.json',
    'edid.builder.json',
    'efi-filesystem.builder.json',
    'efi-file.builder.json',
    'efi-load-option.builder.json',
    'efi-load-option-data.builder.json',
    'efi-load-option-hive.builder.json',
    'efi-section.builder.json',
    'efi-signature.builder.json',
    'efi-signature-list.builder.json',
    'efi-signature-list-external.builder.json',
    'efi-signature-list-svn.builder.json',
    'efi-variable-authentication2.builder.json',
    'efi-volume.builder.json',
    'efi-volume-sized.builder.json',
    'efi-vss2-variable-store.builder.json',
    'efi-vss-auth-variable.builder.json',
    'efi-ftw-store.builder.json',
    'elf.builder.json',
    'fdt.builder.json',
    'fit.builder.json',
    'fmap.builder.json',
    'fmap-offset.builder.json',
    'hid-descriptor.builder.json',
    'hid-descriptor2.builder.json',
    'hid-report-item.builder.json',
    'ifd-bios.builder.json',
    'ifd.builder.json',
    'ifd-no-bios.builder.json',
    'ifwi-cpd.builder.json',
    'ifwi-fpt.builder.json',
    'ihex.builder.json',
    'ihex-signed.builder.json',
    'intel-thunderbolt.builder.json',
    'intel-thunderbolt-firmware.builder.json',
    'json.builder.json',
    'linear.builder.json',
    'metadata.xml',
    'oprom.builder.json',
    'pefile.builder.json',
    'srec-addr32.builder.json',
    'sbatlevel.builder.json',
    'srec.builder.json',
    'sys_vendor',
    'tpm-eventlog-v1.builder.json',
    'tpm-eventlog-v2.builder.json',
    'usb-bos-descriptor.builder.json',
    'usb-device.builder.json',
    'uswid.builder.json',
    'uswid-compressed.builder.json',
    'zip.builder.json',
    'zip-compressed.builder.json',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests'),
  follow_symlinks: true,
)

install_data(
  [
    'bkc-fwupd.conf',
    'oui.txt',
    'pci.ids',
    'pnp.ids',
    'usb.ids',
    'uevent',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests'),
)

install_data(
  ['lockdown/locked/lockdown'],
  install_dir: join_paths(installed_test_datadir, 'tests/lockdown/locked'),
)
install_data(
  ['lockdown/none/lockdown'],
  install_dir: join_paths(installed_test_datadir, 'tests/lockdown/none'),
)
install_data(
  ['quirks.d/tests.quirk'],
  install_dir: join_paths(installed_test_datadir, 'tests/quirks.d'),
)
install_symlink(
  'localtime',
  install_dir: join_paths(installed_test_datadir, 'tests'),
  pointing_to: join_paths('America', 'New_York'),
)
