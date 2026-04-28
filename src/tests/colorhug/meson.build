install_data([
    'firmware.bin',
    'firmware.bin.asc',
    'firmware.bin.p7b',
  ],
  install_dir: join_paths(installed_test_datadir, 'colorhug'),
)

if certtool.found()
  # generate self-signed detached signature
  custom_target('firmware.bin.p7c',
    input: 'firmware.bin',
    output: 'firmware.bin.p7c',
    command: [certtool, '--p7-detached-sign',
                        '--p7-time',
                        '--load-privkey', pkcs7_privkey,
                        '--load-certificate', pkcs7_certificate,
                        '--infile', '@INPUT@',
                        '--outfile', '@OUTPUT@'],
    install: true,
    install_dir: join_paths(installed_test_datadir, 'colorhug'),
  )

  # generate self-signed detached signature *of the checksum*
  custom_target('firmware.bin.sha256.p7c',
    input: 'firmware.bin.sha256',
    output: 'firmware.bin.sha256.p7c',
    command: [certtool, '--p7-detached-sign',
                        '--p7-time',
                        '--load-privkey', pkcs7_privkey,
                        '--load-certificate', pkcs7_certificate,
                        '--infile', '@INPUT@',
                        '--outfile', '@OUTPUT@'],
    install: true,
    install_dir: join_paths(installed_test_datadir, 'colorhug'),
  )

endif
