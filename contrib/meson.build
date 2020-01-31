subdir('firmware_packager')

if host_machine.system() == 'windows'
  con2 = configuration_data()
  con2.set('FWUPD_VERSION', fwupd_version)

  # replace @FWUPD_VERSION@
  configure_file(
    input : 'setup-win32.nsi',
    output : 'setup-win32.nsi',
    configuration : con2,
  )
endif
