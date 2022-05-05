# only install files that are going to be used
if libjcat.type_name() != 'internal' and libjcat.version().version_compare('>= 0.1.9')
  supported_gpg = libjcat.get_variable(pkgconfig : 'supported_gpg') == '1'
  supported_pkcs7 = libjcat.get_variable(pkgconfig : 'supported_pkcs7') == '1'
else
  supported_gpg = host_machine.system() != 'windows'
  supported_pkcs7 = true
endif

if supported_gpg
install_data([
    'GPG-KEY-Linux-Foundation-Firmware',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_dir : join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'GPG-KEY-Linux-Foundation-Metadata',
    'GPG-KEY-Linux-Vendor-Firmware-Service',
  ],
  install_dir : join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif

if supported_pkcs7
install_data([
    'LVFS-CA.pem',
  ],
  install_dir : join_paths(sysconfdir, 'pki', 'fwupd')
)
install_data([
    'LVFS-CA.pem',
  ],
  install_dir : join_paths(sysconfdir, 'pki', 'fwupd-metadata')
)
endif
