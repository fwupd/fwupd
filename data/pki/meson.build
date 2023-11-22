# only install files that are going to be used
libjcat_dep = dependency('jcat', version: '>= 0.1.9', required: false)
if libjcat_dep.found()
  supported_gpg = libjcat_dep.get_variable(pkgconfig: 'supported_gpg', default_value: '0') == '1'
  supported_pkcs7 = libjcat_dep.get_variable(pkgconfig: 'supported_pkcs7', default_value: '0') == '1'
else
  supported_gpg = host_machine.system() != 'windows'
  supported_pkcs7 = true
endif

if supported_gpg
install_data([
    'GPG-KEY-Linux-Foundation-Firmware',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'GPG-KEY-Linux-Foundation-Metadata',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif

if supported_pkcs7
install_data([
    'LVFS-CA.pem',
  ],
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'LVFS-CA.pem',
  ],
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif
