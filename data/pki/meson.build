# only install files that are going to be used
if libjcat.type_name() != 'internal' and libjcat.version().version_compare('>= 0.1.9')
  supported_gpg = libjcat.get_variable(pkgconfig : 'supported_gpg')
  supported_pkcs7 = libjcat.get_variable(pkgconfig : 'supported_pkcs7')
else
  supported_gpg = '1'
  supported_pkcs7 = '1'
endif

if supported_gpg == '1'
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

if supported_pkcs7 == '1'
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
