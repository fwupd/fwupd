install_data(
  [
    'src/__init__.py',
    'src/fwupd_receive_updates.py',
    'src/qubes_fwupd_common.py',
    'src/qubes_fwupd_heads.py',
    'src/qubes_fwupd_update.py',
    'src/qubes_fwupdmgr.py',
  ],
  install_dir: 'share/qubes-fwupd/src',
)

install_data(
  [
    'test/__init__.py',
    'test/fwupd_logs.py',
    'test/test_qubes_fwupd_heads.py',
    'test/test_qubes_fwupdmgr.py',
  ],
  install_dir: 'share/qubes-fwupd/test',
)

install_data(
  [
    'test/logs/firmware.metainfo.xml',
    'test/logs/get_devices.log',
    'test/logs/get_updates.log',
    'test/logs/help.log',
  ],
  install_dir: 'share/qubes-fwupd/test/logs',
)

install_data(
  ['src/vms/fwupd_common_vm.py', 'src/vms/fwupd_download_updates.py'],
  install_dir: 'libexec/qubes-fwupd',
  install_mode: 'rwxrwxr-x',
)

install_data(
  ['test/logs/metainfo_name/firmware.metainfo.xml'],
  install_dir: 'share/qubes-fwupd/test/logs/metainfo_name',
)

install_data(
  'test/logs/metainfo_version/firmware.metainfo.xml',
  install_dir: 'share/qubes-fwupd/test/logs/metainfo_version',
)

install_symlink(
  'qubes-fwupdmgr',
  pointing_to: '/usr/share/qubes-fwupd/src/qubes_fwupdmgr.py',
  install_dir: '/usr/sbin',
)

if get_option('tests')
  # The tests that need dom0 skip themselves, so the rest can run anywhere.
  # They import the modules under src/ by bare name, the way they do once
  # installed alongside each other in /usr/share/qubes-fwupd/src, and read
  # their fixtures relative to the source directory.
  test(
    'qubes-fwupdmgr-test',
    python3,
    args: ['-m', 'pytest', '-v', 'test/'],
    workdir: meson.current_source_dir(),
    env: {'PYTHONPATH': meson.current_source_dir() / 'src'},
    timeout: 180,
  )
endif
