if get_option('tests')
  uefi_insyde_blob = custom_target('UEFI-insyde',
    output: 'UEFI',
    command: [
      python3.full_path(),
      files('build-uefi-insyde.py'),
      '@OUTPUT@',
    ],
    install: true,
    install_dir: installed_test_datadir,
  )
  fwupdx64_efi_signed = custom_target('fwupdx64.efi.signed',
    output: 'fwupdx64.efi.signed',
    command: [
      python3.full_path(),
      files('build-fwupdx64-efi-signed.py'),
      '@OUTPUT@',
    ],
    install: true,
    install_dir: installed_test_datadir,
  )
  install_data([
      'grub2/grub.cfg',
      'test.quirk',
      'uefi-update-info.builder.xml',
    ],
    install_dir: installed_test_datadir,
  )
endif
