if get_option('enable-gpg')
  install_data([
      'GPG-KEY-Hughski-Limited',
      'GPG-KEY-Linux-Vendor-Firmware-Service',
    ],
    install_dir : join_paths(get_option('sysconfdir'), 'pki', 'fwupd')
  )

  install_data([
      'GPG-KEY-Linux-Vendor-Firmware-Service',
    ],
    install_dir : join_paths(get_option('sysconfdir'), 'pki', 'fwupd-metadata')
  )
endif

if get_option('enable-pkcs7')
  install_data([
      'LVFS-CA.pem',
    ],
    install_dir : join_paths(get_option('sysconfdir'), 'pki', 'fwupd')
  )
  install_data([
      'LVFS-CA.pem',
    ],
    install_dir : join_paths(get_option('sysconfdir'), 'pki', 'fwupd-metadata')
  )
endif

