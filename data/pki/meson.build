# only install files that are going to be used
supported_gpg = libjcat.get_variable(pkgconfig: 'supported_gpg', default_value: '1') == '1'
supported_pkcs7 = libjcat.get_variable(pkgconfig: 'supported_pkcs7', default_value: '1') == '1' or host_machine.system() == 'windows'

if supported_gpg
install_data([
    'GPG-KEY-Linux-Foundation-Firmware',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'GPG-KEY-Linux-Foundation-Metadata',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif

if supported_pkcs7
install_data([
    'LVFS-CA.pem',
  ],
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'LVFS-CA.pem',
  ],
  install_tag: 'runtime',
  install_dir: join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif
