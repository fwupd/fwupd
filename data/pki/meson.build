# only install files that are going to be used
supported_pkcs7 = libjcat.get_variable(
  pkgconfig: 'supported_pkcs7',
  default_value: '1',
) == '1' or host_machine.system() == 'windows'

if supported_pkcs7
  install_data(
    ['LVFS-CA.pem', 'LVFS-CA-2025PQ.pem'],
    install_tag: 'runtime',
    install_dir: join_paths(sysconfdir, 'pki', 'fwupd'),
  )
  install_data(
    ['LVFS-CA.pem', 'LVFS-CA-2025PQ.pem'],
    install_tag: 'runtime',
    install_dir: join_paths(sysconfdir, 'pki', 'fwupd-metadata'),
  )
endif
