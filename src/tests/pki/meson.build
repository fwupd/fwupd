install_data([
    'LVFS-CA.pem',
  ],
  install_dir: join_paths(installed_test_datadir, 'pki'),
)

# generate certificate
if certtool.found()
  pkcs7_config = join_paths(meson.current_source_dir(), 'test.cfg')
  pkcs7_certificate = custom_target('test.pem',
    input: pkcs7_privkey,
    output: 'test.pem',
    command: [certtool, '--generate-self-signed',
                        '--template', pkcs7_config,
                        '--load-privkey', '@INPUT@',
                        '--outfile', '@OUTPUT@'],
    depend_files: pkcs7_config,
    install: true,
    install_dir: join_paths(installed_test_datadir, 'pki'),
)
endif
