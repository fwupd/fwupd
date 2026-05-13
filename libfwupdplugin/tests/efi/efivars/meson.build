if meson.version().version_compare('> 0.64.0')
  fs.copyfile(
    'BootNext-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
  fs.copyfile(
    'fwupd-ddc0ee61-e7f0-4e7d-acc5-c070a398838e-0-0abba7dc-e516-4167-bbf5-4d9d1c739416',
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
  fs.copyfile(
    'SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
else
  configure_file(
    input: 'BootNext-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    output: 'BootNext-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    copy: true,
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
  configure_file(
    input: 'fwupd-ddc0ee61-e7f0-4e7d-acc5-c070a398838e-0-0abba7dc-e516-4167-bbf5-4d9d1c739416',
    output: 'fwupd-ddc0ee61-e7f0-4e7d-acc5-c070a398838e-0-0abba7dc-e516-4167-bbf5-4d9d1c739416',
    copy: true,
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
  configure_file(
    input: 'SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    output: 'SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c',
    copy: true,
    install: true,
    install_dir: join_paths(installed_test_datadir, 'tests/efi/efivars'),
  )
endif
