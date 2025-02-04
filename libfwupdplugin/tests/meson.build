subdir('colorhug')
subdir('efi')

installed_firmware_zip = custom_target('installed-firmware-zip',
  input: [
    'colorhug/firmware.txt',
    'colorhug/firmware.txt.asc',
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
    'cab.builder.xml',
    'cab-compressed.builder.xml',
    'cfu-offer.builder.xml',
    'cfu-payload.builder.xml',
    'chassis_type',
    'csv.builder.xml',
    'dfu.builder.xml',
    'dfuse.builder.xml',
    'edid.builder.xml',
    'efi-filesystem.builder.xml',
    'efi-file.builder.xml',
    'efi-load-option.builder.xml',
    'efi-load-option-hive.builder.xml',
    'efi-lz77-legacy.bin',
    'efi-lz77-tiano.bin',
    'efi-section.builder.xml',
    'efi-signature.builder.xml',
    'efi-signature-list.builder.xml',
    'efi-volume.builder.xml',
    'elf.builder.xml',
    'fdt.builder.xml',
    'fit.builder.xml',
    'fmap.builder.xml',
    'fmap-offset.builder.xml',
    'hid-descriptor.builder.xml',
    'hid-descriptor2.builder.xml',
    'hid-report-item.builder.xml',
    'ifd.builder.xml',
    'ifd-no-bios.builder.xml',
    'ifd.builder.xml',
    'ifwi-cpd.builder.xml',
    'ifwi-fpt.builder.xml',
    'ihex.builder.xml',
    'ihex-signed.builder.xml',
    'intel-thunderbolt.builder.xml',
    'linear.builder.xml',
    'metadata.xml',
    'oprom.builder.xml',
    'pefile.builder.xml',
    'srec-addr32.builder.xml',
    'sbatlevel.builder.xml',
    'srec.builder.xml',
    'sys_vendor',
    'usb-bos-descriptor.builder.xml',
    'uswid.builder.xml',
    'uswid-compressed.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests')
)

if sqlite.found()
  install_data([
      'oui.txt',
      'pci.ids',
      'pnp.ids',
      'usb.ids',
    ],
    install_dir: join_paths(installed_test_datadir, 'tests')
  )
endif

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

install_data([
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/pending_reboot',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes'),
)
install_data([
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/current_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/default_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/dell_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/dell_value_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/display_name',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/display_name_language_code',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/possible_values',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense/type',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/WlanAutoSense'),
)

install_data([
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/current_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/default_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/dell_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/display_name',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/display_name_language_code',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/min_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/max_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/scalar_increment',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop/type',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/CustomChargeStop'),
)

install_data([
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/current_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/default_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/dell_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/display_name',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/display_name_language_code',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/type',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/min_length',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset/max_length',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/Asset'),
)

install_data([

    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/current_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/default_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/dell_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/dell_value_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/display_name',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/display_name_language_code',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/possible_values',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd/type',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/BiosRcvrFrmHdd'),
)

install_data([
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/current_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/default_value',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/dell_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/dell_value_modifier',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/display_name',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/display_name_language_code',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/possible_values',
    'bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot/type',
  ],
  install_dir: join_paths(installed_test_datadir, 'tests/bios-attrs/dell-xps13-9310/dell-wmi-sysman/attributes/SecureBoot'),
)
