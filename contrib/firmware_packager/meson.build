if get_option('firmware-packager')
  install_data('firmware_packager.py',
               install_dir: 'share/fwupd')
  install_data('add_capsule_header.py',
               install_dir: 'share/fwupd')
  install_data('install_dell_bios_exe.py',
               install_dir: 'share/fwupd')
  con2 = configuration_data()
  con2.set('FWUPD_VERSION', fwupd_version)
  configure_file(
    input: 'simple_client.py',
    output: 'simple_client.py',
    configuration: con2,
    install: true,
    install_dir: 'share/fwupd',
  )
endif
